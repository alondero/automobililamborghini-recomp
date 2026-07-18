// SPDX-License-Identifier: GPL-3.0-or-later
// SDL2 push-audio backend for the ultramodern pivot. See lambo_audio.h for the contract.
// NOTE (W135, 2026-07-04): the old claim here that "this game uses a CPU/FPU synth, not an RSP
// audio ucode -- there is no aspMain to translate" is FALSIFIED (graveyarded). The game submits
// real M_AUDTASKs with a ~0x738-byte ACMD list every audio frame (measured at title, matching the
// ares dump's list at 0x800df2f0 opcode-for-opcode); PCM is synthesised by the RSP aspMain at ROM
// 0x88B90, now RSPRecomp'd into src/aspMain.cpp (see recomp/aspMain.us.toml).
//
// Design notes:
//  * Format: int16 stereo at 48 kHz initially. SDL is asked for AUDIO_S16LSB
//    and 2 channels. The actual obtained spec may differ; queue_samples builds
//    an SDL_AudioCVT when the obtained spec does not match the game's output
//    (rate/format) and runs SDL_ConvertAudio on every submit.
//  * Thread model: the game's audio thread calls queue_samples (via the
//    ultramodern shim). SDL_QueueAudio and SDL_GetQueuedAudioSize are
//    thread-safe (per SDL2 docs) -- no extra lock needed.
//  * First-AICall tripwire: queue_samples logs once the first time it sees a
//    non-empty buffer. The producer cluster is currently stubbed (W96), so
//    the log will not fire under the current headless boot. It becomes
//    meaningful when the producer un-stub lands (Phase E.2).

#include "lambo_audio.h"

#include "lambo_log.h"

#include <SDL.h>
#include <ultramodern/ultramodern.hpp>
#include "recomp.h" // recomp_context + MEM_W for the func_80079720 native override below

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <vector>

namespace {

// File-static device state. The struct is intentionally not in a singleton --
// the public API in lambo_audio.h is the only entry point. Mutating the static
// vars is safe because all three callbacks plus init/shutdown are called from
// the same OS process and SDL2's queue API is internally serialised.
SDL_AudioDeviceID g_dev = 0;
SDL_AudioSpec     g_obtained{};
uint32_t         g_desired_rate = 0;
// Persistent stream converter (W137, #53): resampling 22050->48000 needs filter STATE carried
// across submits. The old per-submit SDL_AudioCVT path reset that state every ~21 ms buffer
// (SDL_ConvertAudio is a one-shot API that pads each chunk's edges with silence), which garbled
// the whole mix at chunk rate — Adam's "each chunk sounds played backwards" report. Guarded by
// g_state_mtx; recreated when the game changes the AI frequency.
SDL_AudioStream*  g_stream = nullptr;
uint32_t          g_stream_src_rate = 0;
std::atomic<bool> g_device_opened{false};
std::atomic<bool> g_init_logged{false};
std::atomic<bool> g_first_hit_logged{false};
std::atomic<bool> g_first_nonsilent_logged{false};

// Tiny guard for the rare case the runtime calls set_frequency before init
// (init_audio does this). We accept whatever was last set; if init never ran,
// we just store into a dead local and the device path is never taken (the
// runtime reports get_remaining_audio_bytes=100 -- see ultramodern/src/audio.cpp:52).
std::mutex g_state_mtx;

void log_opened_once() {
    bool expected = false;
    if (g_init_logged.compare_exchange_strong(expected, true)) {
        LAMBO_LOG("probe", "audio: opened SDL2 device freq=%u fmt=%d ch=%u samples=%u\n",
                     (unsigned)g_obtained.freq, (int)g_obtained.format,
                     (unsigned)g_obtained.channels, (unsigned)g_obtained.samples);
    }
}

void submit(const int16_t* pcm, size_t sample_count) {
    if (pcm == nullptr || sample_count == 0) {
        return;
    }
    // One-shot content tripwire (PERMANENT harness instrumentation): distinguishes "sink receives
    // buffers" from "sink receives AUDIBLE PCM" in headless logs. Runs BEFORE the device check so
    // a headless run without a drainable audio device (e.g. SDL_AUDIODRIVER=dummy under WSL, where
    // an undrained Pulse queue makes the game's backpressure stop synthesis) still reports whether
    // the game produced real PCM.
    if (!g_first_nonsilent_logged.load() && sample_count <= (256u * 1024u) / sizeof(int16_t)) {
        for (size_t i = 0; i < sample_count; i++) {
            if (pcm[i] != 0) {
                bool exp2 = false;
                if (g_first_nonsilent_logged.compare_exchange_strong(exp2, true)) {
                    LAMBO_LOG("probe", "audio: first NON-SILENT buffer (sample[%zu]=%d of %zu)\n",
                                 i, (int)pcm[i], sample_count);
                }
                break;
            }
        }
    }
    if (!g_device_opened.load() || g_dev == 0) {
        // Graceful degradation: drop. This is the same shape peer projects use
        // for headless builds where no audio device is available.
        return;
    }
    // Bounds guard (W135, #53): early boot submits one garbage-sized buffer (measured:
    // byte_count 0xFFFF5000 = -0xB000 as a signed AI length) which overflowed the conversion
    // buffer size below into a std::length_error abort. Real AI hardware masks the length
    // register to 18 bits (max DMA 256 KB); anything above that ceiling is not a real audio
    // frame, so drop it rather than reinterpret it.
    if (sample_count > (256u * 1024u) / sizeof(int16_t)) {
        static std::atomic<bool> s_oversize_logged{false};
        bool exp = false;
        if (s_oversize_logged.compare_exchange_strong(exp, true)) {
            LAMBO_LOG("probe", "audio: dropped oversize submit (%zu samples > AI max)\n",
                         sample_count);
        }
        return;
    }

    // sample_count is total int16 samples (stereo: 2 per frame). Bytes =
    // sample_count * sizeof(int16_t).
    const uint32_t byte_count = (uint32_t)(sample_count * sizeof(int16_t));

    // Un-swizzle the guest sample order (W137, #53). N64Recomp stores RDRAM as byte-swapped
    // 32-bit words (guest byte A lives at host A^3), and the RSP DMA writes the finished PCM
    // through that convention. A raw int16 view of the buffer therefore yields each aligned
    // word's two samples in REVERSED order (values intact) — i.e. the L/R channels swapped.
    // Swapping each pair restores the guest (hardware) L,R interleave; peer ports do the same
    // in their queue_samples callbacks. AI buffers are 8-byte aligned, so pairs line up with
    // guest words.
    static std::vector<int16_t> swapped;
    swapped.resize(sample_count);
    for (size_t i = 0; i + 1 < sample_count; i += 2) {
        swapped[i + 0] = pcm[i + 1];
        swapped[i + 1] = pcm[i + 0];
    }
    if (sample_count & 1) {
        swapped[sample_count - 1] = pcm[sample_count - 1];
    }

    std::lock_guard<std::mutex> lock(g_state_mtx);
    // Cheap passthrough: native format + native channels + native rate.
    const bool native_rate  = (uint32_t)g_obtained.freq == g_desired_rate;
    const bool native_fmt   = g_obtained.format == AUDIO_S16LSB;
    const bool native_chan  = g_obtained.channels == 2;
    if (native_rate && native_fmt && native_chan) {
        if (SDL_QueueAudio(g_dev, swapped.data(), byte_count) != 0) {
            LAMBO_LOG("probe", "audio: SDL_QueueAudio failed: %s\n", SDL_GetError());
        }
    } else {
        // Convert via a PERSISTENT SDL_AudioStream (stateful resampler — see the note at
        // g_stream). Recreate only when the game's AI frequency changes (rare: once at boot).
        if (g_stream == nullptr || g_stream_src_rate != g_desired_rate) {
            if (g_stream != nullptr) {
                SDL_FreeAudioStream(g_stream);
            }
            g_stream = SDL_NewAudioStream(AUDIO_S16LSB, 2, (int)g_desired_rate,
                                          g_obtained.format, g_obtained.channels,
                                          g_obtained.freq);
            g_stream_src_rate = g_desired_rate;
            if (g_stream == nullptr) {
                LAMBO_LOG("probe", "audio: SDL_NewAudioStream failed: %s\n",
                             SDL_GetError());
            }
        }
        if (g_stream == nullptr) {
            // Degraded fallback: queue unconverted (wrong rate beats silence).
            if (SDL_QueueAudio(g_dev, swapped.data(), byte_count) != 0) {
                LAMBO_LOG("probe", "audio: SDL_QueueAudio (fallback) failed: %s\n",
                             SDL_GetError());
            }
            return;
        }
        if (SDL_AudioStreamPut(g_stream, swapped.data(), (int)byte_count) != 0) {
            LAMBO_LOG("probe", "audio: SDL_AudioStreamPut failed: %s\n", SDL_GetError());
            return;
        }
        const int avail = SDL_AudioStreamAvailable(g_stream);
        if (avail > 0) {
            static std::vector<uint8_t> out;
            out.resize((size_t)avail);
            const int got = SDL_AudioStreamGet(g_stream, out.data(), avail);
            if (got > 0) {
                if (SDL_QueueAudio(g_dev, out.data(), (Uint32)got) != 0) {
                    LAMBO_LOG("probe", "audio: SDL_QueueAudio (stream) failed: %s\n",
                                 SDL_GetError());
                }
            }
        }
    }

    bool expected = false;
    if (g_first_hit_logged.compare_exchange_strong(expected, true)) {
        const uint32_t frames = (uint32_t)(sample_count / 2);
        LAMBO_LOG("probe", "audio: first osAiSetNextBuffer routed (%u samples, %u frames)\n",
                     (unsigned)sample_count, (unsigned)frames);
    }
}

void queue_samples(int16_t* pcm, size_t sample_count) {
    submit(pcm, sample_count);
}

size_t get_frames_remaining() {
    if (!g_device_opened.load() || g_dev == 0) {
        return 0;
    }
    const Uint32 bytes = SDL_GetQueuedAudioSize(g_dev);
    // Bytes -> frames. Guard against /0.
    const uint32_t bytes_per_frame =
        (uint32_t)g_obtained.channels * (uint32_t)(SDL_AUDIO_BITSIZE(g_obtained.format) / 8);
    if (bytes_per_frame == 0) {
        return 0;
    }
    uint64_t device_frames = bytes / bytes_per_frame;

    // Cushion (W137, #53): the game keeps the AI buffer only marginally ahead (correct on real
    // hardware, where the AI FIFO adds its own latency), but SDL pulls a whole device callback
    // (g_obtained.samples frames, ~10 ms at 48 kHz/480) at once — so a queue that hovers near
    // one callback's worth audibly underruns at pull boundaries (measured: 250+ queue-empty
    // events in a 1400-VI run = Adam's "broken up" SFX). Under-report by 3 callbacks so the
    // game's own backpressure maintains ~30 ms of real headroom. Same knob as ultramodern's
    // buffer_offset_frames ("if there's ever any audio popping, check here first"), sized to
    // the actual device pull granularity instead of a fixed VI fraction.
    const uint64_t cushion = 3ull * (g_obtained.samples ? g_obtained.samples : 512);
    device_frames = (device_frames > cushion) ? (device_frames - cushion) : 0;

    // Rate-convert to GAME frames (W137, #53). The queue holds RESAMPLED audio at the device
    // rate (e.g. 48000), but the caller — ultramodern::get_remaining_audio_bytes, and through
    // it the game's mixer backpressure — reasons in the game's AI rate (e.g. 22050). Reporting
    // device frames overstates the buffered audio by freq_device/freq_game (~2.18x), so the
    // game synthesized only ~46% of real time: constant underruns, music stretched ~2.2x slow.
    std::lock_guard<std::mutex> lock(g_state_mtx);
    if (g_desired_rate != 0 && g_obtained.freq > 0 &&
        (uint32_t)g_obtained.freq != g_desired_rate) {
        device_frames = device_frames * g_desired_rate / (uint32_t)g_obtained.freq;
    }
    return (size_t)device_frames;
}

void set_frequency(uint32_t freq) {
    std::lock_guard<std::mutex> lock(g_state_mtx);
    if (freq != g_desired_rate) {
        LAMBO_LOG("probe", "audio: set_frequency %u -> %u\n", g_desired_rate, freq);
    }
    g_desired_rate = freq;
    // We do not reopen the device on every set_frequency. SDL honours the
    // requested rate via SDL_AUDIO_ALLOW_ANY_CHANGE at open time. If the
    // obtained spec rate does not match what the game asks for, the CVT path
    // in submit() handles the conversion. This keeps the audio path light --
    // a reopen is heavy and would stall the game thread for tens of ms.
}

} // anonymous namespace

namespace lambo::audio {

void init(uint32_t desired_sample_rate) {
    std::lock_guard<std::mutex> lock(g_state_mtx);
    if (g_device_opened.load()) {
        return;  // idempotent
    }
    g_desired_rate = desired_sample_rate ? desired_sample_rate : 48000;

    // HEADLESS harness runs get NO audio device (W135, #53). Rationale: in a headless/WSL
    // environment the SDL queue never drains (Pulse has no real sink; SDL's dummy driver buffers
    // forever), so SDL_GetQueuedAudioSize grows unbounded, get_frames_remaining reports a full
    // queue, and the GAME'S OWN backpressure (frame count = target - remaining in the mixer body
    // func_80067CF0) correctly stops synthesising -- silently masking whether the audio pipeline
    // works. No device = get_frames_remaining()==0 = an ideal AI that always drains, so headless
    // logs report real synthesis state (see the NON-SILENT tripwire in submit()).
    {
        const char* headless = std::getenv("LAMBO_HEADLESS");
        if (headless && headless[0] && headless[0] != '0') {
            LAMBO_LOG("probe", "audio: headless -- no SDL device (ideal-drain sink)\n");
            return;
        }
    }

    // Windows driver hint: bypass DirectSound for the lower-latency WASAPI
    // backend. Mirrors the peer pattern in Zelda64Recomp/SnowboardKids2/
    // BM64Recomp. No-op on Linux/macOS.
#if defined(_WIN32)
    SDL_setenv("SDL_AUDIODRIVER", "wasapi", true);
#endif

    if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0) {
        LAMBO_LOG("probe", "audio: SDL_InitSubSystem(SDL_INIT_AUDIO) failed: %s\n",
                     SDL_GetError());
        return;
    }

    SDL_AudioSpec want{};
    want.freq     = (int)g_desired_rate;
    want.format   = AUDIO_S16LSB;  // int16 little-endian, host-native on x86
    want.channels = 2;            // stereo, matching the N64 AI output
    want.samples  = 0x100;        // 256 frames ~= 5.3 ms at 48 kHz; the value
                                  // ultramodern's buffer_offset_frames heuristic
                                  // (ultramodern/src/audio.cpp:41) plays nicely with
    want.callback = nullptr;      // use SDL_QueueAudio, not a callback

    g_dev = SDL_OpenAudioDevice(/*device=*/nullptr, /*iscapture=*/0,
                                &want, &g_obtained,
                                SDL_AUDIO_ALLOW_ANY_CHANGE);
    if (g_dev == 0) {
        LAMBO_LOG("probe", "audio: SDL_OpenAudioDevice failed: %s\n", SDL_GetError());
        return;
    }
    SDL_PauseAudioDevice(g_dev, 0);  // start playback immediately
    g_device_opened.store(true);
    log_opened_once();
}

void get_callbacks(ultramodern::audio_callbacks_t* out) {
    out->queue_samples        = &queue_samples;
    out->get_frames_remaining = &get_frames_remaining;
    out->set_frequency        = &set_frequency;
}

void shutdown() {
    std::lock_guard<std::mutex> lock(g_state_mtx);
    if (g_stream != nullptr) {
        SDL_FreeAudioStream(g_stream);
        g_stream = nullptr;
        g_stream_src_rate = 0;
    }
    if (g_dev != 0) {
        SDL_CloseAudioDevice(g_dev);
        g_dev = 0;
    }
    g_device_opened.store(false);
}

} // namespace lambo::audio

// -----------------------------------------------------------------------------------------------
// func_80079720 (runtime 0x80078B20) = the sound-player STATUS GETTER (`jr $ra; lw $v0,0x2C($a0)`)
// -- NATIVE OVERRIDE (W135, #53; routed via NATIVE_OVERRIDES in gen_syms_toml.py). Its ONLY two
// call sites are the boot stop-and-drain waiter func_800676B4 (funcs_8.c): boot posts a
// sequence-stop to the music player, then BUSY-SPINS `while (func_80079720(obj) != 0)`. The
// recompiled spin has NO dispatch point (no osSetIntMask inside the loop), so under ultramodern's
// cooperative scheduler the spinning boot thread never yields, the audio thread never runs the
// handler-A stop arm (guest 0x80077EB0) that stores 0 to obj->0x2C, and boot livelocks at state 6
// (measured W135: exit_group backtrace pins Game Thread 3 in func_800676B4/funcs_8.c:2332; the
// whole port wedges to 0 fps). This is exactly LIMITATION (a) of the W97 yield model
// (ultramodern/src/mesgqueue.cpp): "a lower-priority non-yielding busy loop could livelock."
// On real hardware the AI/retrace interrupt preempts this spin at ANY instruction, so making the
// poll itself a dispatch point is a FAITHFUL rendering of preemption, not a behaviour change:
// deliver pending external messages, hand the CPU to any ready thread (the audio worker), then
// perform the ROM's one-instruction load verbatim. Same class as the W97 osSetIntMask hook.
extern "C" void func_80079720(uint8_t* rdram, recomp_context* ctx) {
    ultramodern::deliver_external_and_yield(rdram);
    ctx->r2 = MEM_W(ctx->r4, 0X2C); // ROM body: jr $ra; lw $v0, 0x2C($a0)
}
