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
//    ultramodern shim). SDL device lifecycle work is pumped from the main
//    thread; submission only queues already-prepared data.
//  * First-AICall tripwire: queue_samples logs once the first time it sees a
//    non-empty buffer. The producer cluster is currently stubbed (W96), so
//    the log will not fire under the current headless boot. It becomes
//    meaningful when the producer un-stub lands (Phase E.2).

#include "lambo_audio.h"

#include "lambo_log.h"

#include <SDL.h>
#include <ultramodern/ultramodern.hpp>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <vector>

namespace {

// File-static device state. The public API in lambo_audio.h is the only entry point; state shared
// by the main-thread pump and the audio callbacks is protected by g_state_mtx.
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
bool              g_device_opened = false;
bool              g_audio_subsystem_ready = false;
bool              g_initialized = false;
bool              g_headless_sink = false;
bool              g_recovery_requested = false;
bool              g_unavailable_logged = false;
std::atomic<bool> g_first_hit_logged{false};
std::atomic<bool> g_first_nonsilent_logged{false};

#if defined(LAMBO_AUDIO_RETRY_INTERVAL_MS)
constexpr auto k_device_retry_interval =
    std::chrono::milliseconds(LAMBO_AUDIO_RETRY_INTERVAL_MS);
#else
constexpr auto k_device_retry_interval = std::chrono::seconds(1);
#endif
std::chrono::steady_clock::time_point g_next_device_attempt{};

// Tiny guard for the rare case the runtime calls set_frequency before init
// (init_audio does this). We accept whatever was last set; if init never ran,
// we just store into a dead local and the device path is never taken (the
// runtime reports get_remaining_audio_bytes=100 -- see ultramodern/src/audio.cpp:52).
std::mutex g_state_mtx;

// g_state_mtx must be held.
void close_device_locked() {
    if (g_stream != nullptr) {
        SDL_FreeAudioStream(g_stream);
        g_stream = nullptr;
        g_stream_src_rate = 0;
    }
    if (g_dev != 0) {
        SDL_CloseAudioDevice(g_dev);
        g_dev = 0;
    }
    g_device_opened = false;
    g_obtained = {};
}

void log_opened(const SDL_AudioSpec& obtained) {
    LAMBO_LOG("probe", "audio: opened SDL2 device freq=%u fmt=%d ch=%u samples=%u\n",
              (unsigned)obtained.freq, (int)obtained.format,
              (unsigned)obtained.channels, (unsigned)obtained.samples);
}

void mark_open_failure(const char* operation, bool subsystem_ready) {
    bool log_failure = false;
    {
        std::lock_guard<std::mutex> lock(g_state_mtx);
        g_audio_subsystem_ready = subsystem_ready;
        g_next_device_attempt = std::chrono::steady_clock::now() + k_device_retry_interval;
        if (!g_unavailable_logged) {
            g_unavailable_logged = true;
            log_failure = true;
        }
    }
    if (log_failure) {
        LAMBO_LOG("probe", "audio: %s failed: %s; recovery will be retried\n",
                  operation, SDL_GetError());
    }
}

// g_state_mtx must be held. Queueing itself is short and thread-safe in SDL; lifecycle recovery is
// only requested here and performed later by pump() on the main thread.
bool queue_audio_locked(const void* data, Uint32 byte_count, const char* path) {
    if (SDL_QueueAudio(g_dev, data, byte_count) == 0) {
        return true;
    }
    g_recovery_requested = true;
    g_next_device_attempt = std::chrono::steady_clock::now() + k_device_retry_interval;
    if (!g_unavailable_logged) {
        g_unavailable_logged = true;
        LAMBO_LOG("probe", "audio: SDL_QueueAudio%s failed: %s; scheduling recovery\n",
                  path, SDL_GetError());
    }
    return false;
}

// Called from the main-thread event/update loop. SDL device discovery and teardown stay off the
// game's audio submission callback, which only queues data while holding the short state lock.
void pump_device() {
    SDL_AudioDeviceID device = 0;
    uint32_t desired_rate = 0;
    bool subsystem_ready = false;
    bool need_open = false;
    {
        std::lock_guard<std::mutex> lock(g_state_mtx);
        if (!g_initialized || g_headless_sink) {
            return;
        }
        if (g_device_opened && g_dev != 0 && !g_recovery_requested) {
            device = g_dev;
        } else {
            if (g_recovery_requested && g_device_opened) {
                close_device_locked();
            }
            const auto now = std::chrono::steady_clock::now();
            if (now < g_next_device_attempt) {
                return;
            }
            desired_rate = g_desired_rate;
            subsystem_ready = g_audio_subsystem_ready;
            need_open = true;
        }
    }

    if (device != 0) {
        if (SDL_GetAudioDeviceStatus(device) != SDL_AUDIO_STOPPED) {
            return;
        }
        bool log_stopped = false;
        {
            std::lock_guard<std::mutex> lock(g_state_mtx);
            if (!g_device_opened || g_dev != device) {
                return;
            }
            close_device_locked();
            g_recovery_requested = true;
            if (!g_unavailable_logged) {
                g_unavailable_logged = true;
                log_stopped = true;
            }
            desired_rate = g_desired_rate;
            subsystem_ready = g_audio_subsystem_ready;
            need_open = true;
        }
        if (log_stopped) {
            LAMBO_LOG("probe", "audio: output device stopped; scheduling recovery\n");
        }
    }
    if (!need_open) {
        return;
    }

    // Windows driver hint: bypass DirectSound for the lower-latency WASAPI backend. No-op on
    // Linux/macOS.
#if defined(_WIN32)
    SDL_setenv("SDL_AUDIODRIVER", "wasapi", true);
#endif

    if (!subsystem_ready) {
        if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0) {
            mark_open_failure("SDL_InitSubSystem(SDL_INIT_AUDIO)", false);
            return;
        }
        subsystem_ready = true;
    }

    SDL_AudioSpec want{};
    want.freq     = (int)desired_rate;
    want.format   = AUDIO_S16LSB;
    want.channels = 2;
    want.samples  = 0x100;
    want.callback = nullptr;
    SDL_AudioSpec obtained{};
    const SDL_AudioDeviceID opened = SDL_OpenAudioDevice(/*device=*/nullptr, /*iscapture=*/0,
                                                        &want, &obtained,
                                                        SDL_AUDIO_ALLOW_ANY_CHANGE);
    if (opened == 0) {
        mark_open_failure("SDL_OpenAudioDevice", subsystem_ready);
        return;
    }
    SDL_PauseAudioDevice(opened, 0);

    bool keep_device = false;
    {
        std::lock_guard<std::mutex> lock(g_state_mtx);
        if (g_initialized && !g_headless_sink && !g_device_opened && g_dev == 0) {
            g_dev = opened;
            g_obtained = obtained;
            g_device_opened = true;
            g_audio_subsystem_ready = subsystem_ready;
            g_recovery_requested = false;
            g_unavailable_logged = false;
            // Cooldown is a failure guard, never a residual delay after success.
            g_next_device_attempt = {};
            keep_device = true;
        }
    }
    if (keep_device) {
        log_opened(obtained);
    } else {
        SDL_CloseAudioDevice(opened);
    }
}

void submit(const int16_t* pcm, size_t sample_count) {
    if (pcm == nullptr || sample_count == 0) {
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

    bool headless = false;
    {
        std::lock_guard<std::mutex> lock(g_state_mtx);
        headless = g_headless_sink;
        if (!g_initialized || (!headless && (!g_device_opened || g_dev == 0))) {
            return;
        }
    }

    // Keep the diagnostic scan for the intentional headless ideal-drain path, but never spend
    // time preparing a buffer that has no live device to receive it.
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
    if (headless) {
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
    static thread_local std::vector<int16_t> swapped;
    swapped.resize(sample_count);
    for (size_t i = 0; i + 1 < sample_count; i += 2) {
        swapped[i + 0] = pcm[i + 1];
        swapped[i + 1] = pcm[i + 0];
    }
    if (sample_count & 1) {
        swapped[sample_count - 1] = pcm[sample_count - 1];
    }

    std::lock_guard<std::mutex> lock(g_state_mtx);
    if (!g_device_opened || g_dev == 0) {
        return;
    }
    // Cheap passthrough: native format + native channels + native rate.
    const bool native_rate  = (uint32_t)g_obtained.freq == g_desired_rate;
    const bool native_fmt   = g_obtained.format == AUDIO_S16LSB;
    const bool native_chan  = g_obtained.channels == 2;
    if (native_rate && native_fmt && native_chan) {
        queue_audio_locked(swapped.data(), byte_count, "");
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
            queue_audio_locked(swapped.data(), byte_count, " (fallback)");
            return;
        }
        if (SDL_AudioStreamPut(g_stream, swapped.data(), (int)byte_count) != 0) {
            LAMBO_LOG("probe", "audio: SDL_AudioStreamPut failed: %s\n", SDL_GetError());
            return;
        }
        const int avail = SDL_AudioStreamAvailable(g_stream);
        if (avail > 0) {
            static thread_local std::vector<uint8_t> out;
            out.resize((size_t)avail);
            const int got = SDL_AudioStreamGet(g_stream, out.data(), avail);
            if (got > 0) {
                queue_audio_locked(out.data(), (Uint32)got, " (stream)");
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
    std::lock_guard<std::mutex> lock(g_state_mtx);
    if (!g_device_opened || g_dev == 0) {
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
    {
        std::lock_guard<std::mutex> lock(g_state_mtx);
        if (g_initialized) {
            return;  // idempotent
        }
        g_desired_rate = desired_sample_rate ? desired_sample_rate : 48000;
        g_initialized = true;
        g_headless_sink = false;

        const char* headless = std::getenv("LAMBO_HEADLESS");
        if (headless && headless[0] && headless[0] != '0') {
            g_headless_sink = true;
        }
    }

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
    pump_device();
}

void get_callbacks(ultramodern::audio_callbacks_t* out) {
    out->queue_samples        = &queue_samples;
    out->get_frames_remaining = &get_frames_remaining;
    out->set_frequency        = &set_frequency;
}

void pump() {
    pump_device();
}

void shutdown() {
    std::lock_guard<std::mutex> lock(g_state_mtx);
    close_device_locked();
    g_initialized = false;
    g_headless_sink = false;
    g_audio_subsystem_ready = false;
    g_recovery_requested = false;
    g_unavailable_logged = false;
    g_desired_rate = 0;
    g_next_device_attempt = {};
}

} // namespace lambo::audio
