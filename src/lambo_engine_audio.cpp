// SPDX-License-Identifier: GPL-3.0-or-later
#include "lambo_engine_audio.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <mutex>

namespace lambo::engine_audio {
namespace {
constexpr double tau = 6.283185307179586;
std::mutex control_mutex;
Controls targets;
uint64_t next_start_revision = 0;
float finite_clamp(float v, float low, float high) {
    return std::isfinite(v) ? std::clamp(v, low, high) : low;
}
// Polynomial band-limited step correction at the combustion pulse boundary.
double blep(double phase, double step) {
    if (phase < step) { const double t = phase / step; return 2 * t - t * t - 1; }
    if (phase > 1 - step) { const double t = (phase - 1) / step; return t * t + 2 * t + 1; }
    return 0;
}
}

bool enabled() {
    static const bool value = [] {
        const char* option = std::getenv("LAMBO_ENGINE_AUDIO");
        return !option || option[0] != '0';
    }();
    return value;
}

void publish(size_t voice, unsigned flags, Control c) {
    if (voice >= voice_count) return;
    std::lock_guard lock(control_mutex);
    auto& t = targets[voice];
    // A guest update is accepted only after its scheduled voice starts playing.
    t.delay_seconds = 0;
    if (flags & 1) t.pitch = finite_clamp(c.pitch, 0.1f, 2.0f);
    if (flags & 2) t.gain = finite_clamp(c.gain, 0, 1);
    if (flags & 4) t.pan = finite_clamp(c.pan, 0, 1);
}
void start(size_t voice, Control c, float delay_seconds) {
    if (voice >= voice_count) return;
    std::lock_guard lock(control_mutex);
    c.pitch = finite_clamp(c.pitch, 0.1f, 2.0f);
    c.gain = finite_clamp(c.gain, 0, 1);
    c.pan = finite_clamp(c.pan, 0, 1);
    c.delay_seconds = finite_clamp(delay_seconds, 0, 10);
    c.start_revision = ++next_start_revision;
    targets[voice] = c;
}
void stop(size_t voice) { publish(voice, 2, {}); }
void stop_all() {
    std::lock_guard lock(control_mutex);
    targets = {};
}

void Synth::mix(int16_t* stereo, size_t frames, uint32_t rate, const Controls& controls) {
    if (!stereo || rate < 8000 || rate > 192000) return;
    // 18 ms pitch slew follows shifts without zipper noise; 8 ms amplitude ramps
    // avoid clicks. Filter coefficients are recalculated for the actual AI rate.
    const float pitch_slew = 1 - std::exp(-1.0f / (0.018f * rate));
    const float gain_slew = 1 - std::exp(-1.0f / (0.008f * rate));
    const float dc_slew = 1 - std::exp(-float(tau) * 25 / rate);
    const float intake_filter = 1 - std::exp(-float(tau) * 900 / rate);
    for (size_t v = 0; v < voice_count; ++v) {
        auto& s = voices_[v];
        const float pitch = finite_clamp(controls[v].pitch, 0.1f, 2.0f);
        const float gain = finite_clamp(controls[v].gain, 0, 1);
        const float pan = finite_clamp(controls[v].pan, 0, 1);
        if (s.start_revision != controls[v].start_revision) {
            s.start_revision = controls[v].start_revision;
            s.delay_seconds = finite_clamp(controls[v].delay_seconds, 0, 10);
        }
        if (controls[v].delay_seconds == 0) s.delay_seconds = 0;
        if (gain == 0 && s.gain < 0.00001f) { s.gain = 0; continue; }
        for (size_t i = 0; i < frames; ++i) {
            if (s.delay_seconds > 0) { s.delay_seconds -= 1.0 / rate; continue; }
            s.pitch += (pitch - s.pitch) * pitch_slew;
            s.gain += (gain - s.gain) * gain_slew;
            s.pan += (pan - s.pan) * gain_slew;
            // Authored mapping: the game's 0.1..1.6 ratio becomes 900..8250 rpm.
            // Twelve combustion events per four-stroke cycle, with bank/cylinder
            // variation. Phase is continuous through parameter and buffer changes.
            const float rpm = 410 + 4900 * s.pitch;
            s.phase += rpm / (120.0 * rate);
            s.phase -= std::floor(s.phase);
            const double firing = s.phase * 12;
            const double p = firing - std::floor(firing);
            s.noise ^= s.noise << 13; s.noise ^= s.noise >> 17; s.noise ^= s.noise << 5;
            const float noise = float(s.noise & 0xffff) / 32768 - 1;
            const float variation = 1 + 0.10f * std::sin(tau * s.phase * 5)
                                      + 0.06f * std::sin(tau * s.phase * 7 + v);
            // Smooth pressure pulse plus its reflected rarefaction. Finite width
            // and the exhaust low-pass suppress the brittle sawtooth/buzzer edge.
            const float pulse = float(std::exp(-p * 15) - 0.32 * std::exp(-p * 4)
                + 0.34293035 * blep(p, rpm / (10.0 * rate)));
            const float load = std::clamp(s.gain * 1.8f, 0.0f, 1.0f);
            const float cutoff = 850 + 2100 * load + 0.10f * rpm;
            const float filter = 1 - std::exp(-float(tau) * cutoff / rate);
            const float pressure = pulse * variation + noise * (0.015f + 0.045f * load);
            s.exhaust += filter * (pressure - s.exhaust);
            s.intake += intake_filter * (noise - s.intake);
            const float body = 0.12f * std::sin(tau * s.phase * 6)
                             + 0.06f * std::sin(tau * s.phase * 3);
            const float raw = s.exhaust + body + s.intake * 0.035f * load;
            s.dc += dc_slew * (raw - s.dc);
            const float sample = (raw - s.dc) * s.gain * 12500;
            const float left = sample * std::sqrt(1 - s.pan);
            const float right = sample * std::sqrt(s.pan);
            stereo[i * 2] = int16_t(std::clamp(float(stereo[i * 2]) + left, -32768.0f, 32767.0f));
            stereo[i * 2 + 1] = int16_t(std::clamp(float(stereo[i * 2 + 1]) + right, -32768.0f, 32767.0f));
        }
    }
}

void mix(int16_t* stereo, size_t frames, uint32_t rate, uint64_t generation) {
    if (!enabled()) return;
    Controls snapshot;
    { std::lock_guard lock(control_mutex); snapshot = targets; }
    static thread_local Synth synth;
    static thread_local uint64_t previous_generation = ~uint64_t(0);
    if (generation != previous_generation) {
        synth = {};
        previous_generation = generation;
    }
    synth.mix(stereo, frames, rate, snapshot);
}
}
