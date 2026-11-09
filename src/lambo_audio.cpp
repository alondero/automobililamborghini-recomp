// SPDX-License-Identifier: GPL-3.0-or-later
// SDL2 push-audio backend for the ultramodern runtime. See lambo_audio.h for
// the public contract. The generated RSP audio task source is an input to this
// bridge; it is not hand-written and is absent until the ROM build runs.
//
// Design invariants:
//  * Format: int16 stereo at 48 kHz initially. SDL is asked for AUDIO_S16LSB
//    and 2 channels. The actual obtained spec may differ; queue_samples builds
//    an SDL_AudioCVT when the obtained spec does not match the game's output
//    (rate/format) and runs SDL_ConvertAudio on every submit.
//  * Thread model: the game's audio thread calls queue_samples (via the
//    ultramodern shim). SDL device lifecycle work is pumped from the main
//    thread; submission performs conversion and queueing but never device lifecycle calls.
//  * Locks, in acquisition order. g_state_mtx and g_stream_mtx are never held together.
//      g_lifecycle_mtx  serialises device open/close/recovery (pump, init, shutdown). It may
//                       be held across SDL_OpenAudioDevice; the audio callbacks never take it.
//      g_state_mtx      device fields below. Held for copies and for the short SDL calls that
//                       use g_dev (queue, queued size, clear). Those must not overlap
//                       SDL_CloseAudioDevice on the same id, so a device is detached under
//                       this lock and closed after it is released.
//      g_stream_mtx     the resampler stream, held across SDL_AudioStreamPut/Get.
//    g_generation changes whenever the device is installed, detached, or (un)suspended. A
//    buffer converted under an older generation is dropped rather than queued.
//  * The first non-empty buffer is logged once for diagnosis. A headless run
//    may have no device; that is a normal test boundary, not proof that audio
//    production is complete.

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

std::mutex g_lifecycle_mtx;
std::mutex g_state_mtx;
std::mutex g_stream_mtx;

// File-static device state. The public API in lambo_audio.h is the only entry point; state shared
// by the main-thread pump and the audio callbacks is protected by g_state_mtx.
SDL_AudioDeviceID g_dev = 0;
SDL_AudioSpec     g_obtained{};
uint32_t         g_desired_rate = 0;
uint64_t          g_generation = 0;
// Persistent stream converter: resampling 22050->48000 needs filter state carried
// across submits. The old per-submit SDL_AudioCVT path reset that state every ~21 ms buffer
// (SDL_ConvertAudio is a one-shot API that pads each chunk's edges with silence), which garbled
// the whole mix at chunk rate — Adam's "each chunk sounds played backwards" report. Guarded by
// g_stream_mtx; recreated when the device generation or the game's AI frequency changes.
SDL_AudioStream*  g_stream = nullptr;
uint32_t          g_stream_src_rate = 0;
uint64_t          g_stream_generation = 0;
bool              g_device_opened = false;
bool              g_audio_subsystem_ready = false;
bool              g_initialized = false;
bool              g_suspended = false;
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

// g_state_mtx must be held. Returns the device for the caller to close once the lock is
// released: SDL_CloseAudioDevice joins SDL's device thread.
SDL_AudioDeviceID detach_device_locked() {
    const SDL_AudioDeviceID device = g_dev;
    g_dev = 0;
    g_device_opened = false;
    g_obtained = {};
    ++g_generation;
    return device;
}

void close_detached(SDL_AudioDeviceID device) {
    if (device != 0) {
        SDL_CloseAudioDevice(device);
    }
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
bool queue_audio_locked(const void* data, Uint32 byte_count, const char* queue_context) {
    if (SDL_QueueAudio(g_dev, data, byte_count) == 0) {
        return true;
    }
    g_recovery_requested = true;
    g_next_device_attempt = std::chrono::steady_clock::now() + k_device_retry_interval;
    if (!g_unavailable_logged) {
        g_unavailable_logged = true;
        LAMBO_LOG("probe", "audio: SDL_QueueAudio%s failed: %s; scheduling recovery\n",
                  queue_context, SDL_GetError());
    }
    return false;
}

// Queue a buffer prepared for `generation`. One converted for a device that has since been
// replaced, or before a suspend, is dropped.
void queue_if_current(uint64_t generation, const void* data, Uint32 byte_count,
                      const char* queue_context) {
    std::lock_guard<std::mutex> lock(g_state_mtx);
    if (g_suspended || !g_device_opened || g_dev == 0 || g_generation != generation) {
        return;
    }
    queue_audio_locked(data, byte_count, queue_context);
}

// g_lifecycle_mtx must be held. Opens the default device when none is installed and the
// failure cooldown has passed.
void open_device_if_due() {
    uint32_t desired_rate = 0;
    bool subsystem_ready = false;
    {
        std::lock_guard<std::mutex> lock(g_state_mtx);
        if (!g_initialized || g_headless_sink || g_device_opened) {
            return;
        }
        if (std::chrono::steady_clock::now() < g_next_device_attempt) {
            return;
        }
        desired_rate = g_desired_rate;
        subsystem_ready = g_audio_subsystem_ready;
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
            ++g_generation;
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

// Called from the main-thread event/update loop. SDL device discovery and teardown stay off the
// game's audio submission callback, which only queues data while holding the short state lock.
void pump_device() {
    std::lock_guard<std::mutex> lifecycle(g_lifecycle_mtx);
    SDL_AudioDeviceID live = 0;
    SDL_AudioDeviceID stale = 0;
    {
        std::lock_guard<std::mutex> lock(g_state_mtx);
        if (!g_initialized || g_headless_sink) {
            return;
        }
        if (g_device_opened && g_dev != 0 && !g_recovery_requested) {
            live = g_dev;
        } else if (g_recovery_requested && g_device_opened) {
            stale = detach_device_locked();
        }
    }

    if (live != 0) {
        if (SDL_GetAudioDeviceStatus(live) != SDL_AUDIO_STOPPED) {
            return;
        }
        bool log_stopped = false;
        {
            std::lock_guard<std::mutex> lock(g_state_mtx);
            if (!g_device_opened || g_dev != live) {
                return;
            }
            stale = detach_device_locked();
            g_recovery_requested = true;
            if (!g_unavailable_logged) {
                g_unavailable_logged = true;
                log_stopped = true;
            }
        }
        if (log_stopped) {
            LAMBO_LOG("probe", "audio: output device stopped; scheduling recovery\n");
        }
    }
    close_detached(stale);
    open_device_if_due();
}

void submit(const int16_t* pcm, size_t sample_count) {
    if (pcm == nullptr || sample_count == 0) {
        return;
    }
    // Bounds guard: early boot submits one garbage-sized buffer (measured:
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
    uint64_t generation = 0;
    SDL_AudioSpec obtained{};
    uint32_t desired_rate = 0;
    {
        std::lock_guard<std::mutex> lock(g_state_mtx);
        headless = g_headless_sink;
        if (g_suspended || !g_initialized || (!headless && (!g_device_opened || g_dev == 0))) {
            return;
        }
        generation = g_generation;
        obtained = g_obtained;
        desired_rate = g_desired_rate;
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

    // Un-swizzle the guest sample order. N64Recomp stores RDRAM as byte-swapped
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

    // Cheap passthrough: native format + native channels + native rate.
    const bool native_rate  = (uint32_t)obtained.freq == desired_rate;
    const bool native_fmt   = obtained.format == AUDIO_S16LSB;
    const bool native_chan  = obtained.channels == 2;
    if (native_rate && native_fmt && native_chan) {
        queue_if_current(generation, swapped.data(), byte_count, "");
    } else {
        static thread_local std::vector<uint8_t> out;
        int got = 0;
        bool fallback = false;
        {
            std::lock_guard<std::mutex> stream_lock(g_stream_mtx);
            // Convert via a PERSISTENT SDL_AudioStream (stateful resampler — see the note at
            // g_stream). Recreate only for a new device generation or AI frequency (rare).
            if (g_stream == nullptr || g_stream_generation != generation ||
                g_stream_src_rate != desired_rate) {
                if (g_stream != nullptr) {
                    SDL_FreeAudioStream(g_stream);
                }
                g_stream = SDL_NewAudioStream(AUDIO_S16LSB, 2, (int)desired_rate,
                                              obtained.format, obtained.channels,
                                              obtained.freq);
                g_stream_src_rate = desired_rate;
                g_stream_generation = generation;
                if (g_stream == nullptr) {
                    LAMBO_LOG("probe", "audio: SDL_NewAudioStream failed: %s\n",
                                 SDL_GetError());
                }
            }
            if (g_stream == nullptr) {
                fallback = true;
            } else if (SDL_AudioStreamPut(g_stream, swapped.data(), (int)byte_count) != 0) {
                LAMBO_LOG("probe", "audio: SDL_AudioStreamPut failed: %s\n", SDL_GetError());
                return;
            } else {
                const int avail = SDL_AudioStreamAvailable(g_stream);
                if (avail > 0) {
                    out.resize((size_t)avail);
                    got = SDL_AudioStreamGet(g_stream, out.data(), avail);
                }
            }
        }
        if (fallback) {
            // Degraded fallback: queue unconverted (wrong rate beats silence).
            queue_if_current(generation, swapped.data(), byte_count, " (fallback)");
            return;
        }
        if (got > 0) {
            queue_if_current(generation, out.data(), (Uint32)got, " (stream)");
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
    if (g_suspended || !g_device_opened || g_dev == 0) {
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

    // Cushion: the game keeps the AI buffer only marginally ahead (correct on real
    // hardware, where the AI FIFO adds its own latency), but SDL pulls a whole device callback
    // (g_obtained.samples frames, ~10 ms at 48 kHz/480) at once — so a queue that hovers near
    // one callback's worth audibly underruns at pull boundaries (measured: 250+ queue-empty
    // events in a 1400-VI run = Adam's "broken up" SFX). Under-report by 3 callbacks so the
    // game's own backpressure maintains ~30 ms of real headroom. Same knob as ultramodern's
    // buffer_offset_frames ("if there's ever any audio popping, check here first"), sized to
    // the actual device pull granularity instead of a fixed VI fraction.
    const uint64_t cushion = 3ull * (g_obtained.samples ? g_obtained.samples : 512);
    device_frames = (device_frames > cushion) ? (device_frames - cushion) : 0;

    // Rate-convert to game frames. The queue holds resampled audio at the device
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

// The runtime may call this before init (init_audio does); the rate is kept for the first open.
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

void set_suspended(bool suspended) {
    std::lock_guard<std::mutex> lock(g_state_mtx);
    if (g_suspended == suspended) return;
    g_suspended = suspended;
    // A new generation drops in-flight buffers and makes the next submit start a fresh
    // converter, so no resampler history crosses the suspend.
    ++g_generation;
    if (g_dev != 0) SDL_ClearQueuedAudio(g_dev);
}

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

    // Headless harness runs have no audio device. In a headless
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
    std::lock_guard<std::mutex> lifecycle(g_lifecycle_mtx);
    SDL_AudioDeviceID stale = 0;
    {
        std::lock_guard<std::mutex> lock(g_state_mtx);
        stale = detach_device_locked();
        g_initialized = false;
        g_suspended = false;
        g_headless_sink = false;
        g_audio_subsystem_ready = false;
        g_recovery_requested = false;
        g_unavailable_logged = false;
        g_desired_rate = 0;
        g_next_device_attempt = {};
    }
    close_detached(stale);
    std::lock_guard<std::mutex> stream_lock(g_stream_mtx);
    if (g_stream != nullptr) {
        SDL_FreeAudioStream(g_stream);
        g_stream = nullptr;
    }
    g_stream_src_rate = 0;
    g_stream_generation = 0;
}

} // namespace lambo::audio
