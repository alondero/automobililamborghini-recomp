// SPDX-License-Identifier: GPL-3.0-or-later
#include "lambo_engine_audio.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <mutex>

namespace lambo::engine_audio {
namespace {
constexpr float tau = 6.2831853f;
std::mutex control_mutex;
Controls targets;
uint64_t next_start_revision = 0;
float finite_clamp(float v, float low, float high) {
    return std::isfinite(v) ? std::clamp(v, low, high) : low;
}
constexpr std::array<float, 12> even(float step, int n) {
    std::array<float, 12> a{};
    for (int i = 0; i < n; ++i) a[i] = step * i;
    return a;
}
constexpr std::array<uint8_t, 12> alternate{0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1};

// Indexed by vehicle category (docs/CAR_DIFFERENCES.md). Idle and limiter come
// from the cartridge engine table; firing geometry follows each road car's
// layout; exhaust dimensions are authored acoustic tuning, not measurements.
const std::array<Profile, profile_count> profiles{{
    // 60-degree V12: even 60-degree firing alternating banks.
    {"diablo", 12, even(60, 12), alternate, 1100, 7000, 1.10f, {0, 0.30f}, {3.2f, 3.5f}, -0.55f,
     95, 1.1f, 420, 0.55f, 1000, 3800, 210, 0.30f, 0.12f, 0, 1.0f},
    // Twin-turbo flat-six: 120-degree firing; turbines damp the exhaust.
    {"959", 6, even(120, 6), alternate, 1080, 6700, 1.25f, {0, 0.45f}, {3.7f, 4.0f}, -0.50f,
     110, 1.0f, 360, 0.50f, 800, 2600, 190, 0.22f, 0.13f, 1.0f, 1.15f},
    // 65-degree V12: firing intervals alternate 65/55 degrees.
    {"f50", 12, {0, 65, 120, 185, 240, 305, 360, 425, 480, 545, 600, 665}, alternate, 1300, 8700,
     0.80f, {0, 0.20f}, {2.4f, 2.6f}, -0.55f, 120, 0.8f, 620, 0.70f, 1300, 4800, 260, 0.35f,
     0.08f, 0, 0.95f},
    {"mclaren-f1", 12, even(60, 12), alternate, 1200, 7600, 1.00f, {0, 0.25f}, {3.0f, 3.3f}, -0.55f,
     105, 1.0f, 480, 0.60f, 1100, 4000, 230, 0.32f, 0.07f, 0, 1.0f},
    // Carburettor induction is louder on the Countach.
    {"countach", 12, even(60, 12), alternate, 1150, 7200, 1.20f, {0, 0.35f}, {3.4f, 3.7f}, -0.55f,
     90, 1.6f, 400, 0.55f, 950, 3600, 180, 0.55f, 0.15f, 0, 1.0f},
    {"eb110", 12, even(60, 12), alternate, 1250, 8200, 0.85f, {0, 0.20f}, {2.4f, 2.6f}, -0.55f,
     115, 0.9f, 720, 0.60f, 1000, 3000, 240, 0.25f, 0.09f, 0.8f, 1.05f},
    // 90-degree V10 on shared crankpins: alternating 90/54-degree intervals.
    {"viper", 10, {0, 90, 144, 234, 288, 378, 432, 522, 576, 666}, alternate, 980, 6000, 1.60f,
     {0, 0.50f}, {4.5f, 4.9f}, -0.60f, 70, 1.4f, 300, 0.50f, 750, 2600, 150, 0.30f, 0.18f, 0,
     1.05f},
    // 180-degree V12 (flat-twelve): even firing, longer and mellower exhaust.
    {"testarossa", 12, even(60, 12), alternate, 1060, 6500, 1.30f, {0, 0.40f}, {3.8f, 4.0f}, -0.50f,
     85, 1.1f, 360, 0.45f, 900, 3200, 200, 0.30f, 0.10f, 0, 1.0f},
}};

Synth::Biquad bandpass(float hz, float q, uint32_t rate) {
    const float w = tau * std::min(hz, 0.45f * rate) / rate;
    const float alpha = std::sin(w) / (2 * q), a0 = 1 + alpha;
    Synth::Biquad f;
    f.b0 = alpha / a0; f.b1 = 0; f.b2 = -alpha / a0;
    f.a1 = -2 * std::cos(w) / a0; f.a2 = (1 - alpha) / a0;
    return f;
}
void retune(Synth::Biquad& f, const Synth::Biquad& to) {
    f.b0 = to.b0; f.b1 = to.b1; f.b2 = to.b2; f.a1 = to.a1; f.a2 = to.a2;
}
float one_pole(float hz, uint32_t rate) { return 1 - std::exp(-tau * hz / rate); }
float uniform(uint32_t& s) {
    s ^= s << 13; s ^= s >> 17; s ^= s << 5;
    return float(s >> 8) / float(1 << 24);
}
float gauss(uint32_t& s) { return (uniform(s) + uniform(s) + uniform(s) - 1.5f) * 2; }
// Exhaust blowdown pressure hump followed by the scavenging rarefaction.
// Both lobes are raised cosines, so the pulse has no edges to alias.
float pulse_shape(float u) {
    if (u < 1) { const float s = std::sin(3.1415927f * u); return s * s; }
    const float s = std::sin(3.1415927f * (u - 1) / 1.5f);
    return -0.3f * s * s;
}
}

float Synth::Biquad::run(float x) {
    const float y = b0 * x + z1;
    z1 = b1 * x - a1 * y + z2;
    z2 = b2 * x - a2 * y;
    return y;
}

const Profile& profile(int category) {
    return category >= 0 && category < int(profile_count) ? profiles[category] : profiles[0];
}
float rpm_from_pitch(float pitch, float idle_rpm) {
    return std::max(0.0f, pitch - 0.1f) * (8000 - idle_rpm) / 1.5f;
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
    if (flags & set_pitch) t.pitch = finite_clamp(c.pitch, 0.1f, 2.0f);
    if (flags & set_gain) t.gain = finite_clamp(c.gain, 0, 1);
    if (flags & set_pan) t.pan = finite_clamp(c.pan, 0, 1);
    // A failed vehicle sample keeps the last known car layout.
    if (flags & set_vehicle) {
        t.rpm = c.rpm; t.throttle = c.throttle;
        if (c.profile != unknown_profile) t.profile = c.profile;
    }
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
void stop(size_t voice) { publish(voice, set_gain, {}); }
void stop_all() {
    std::lock_guard lock(control_mutex);
    targets = {};
}

void Synth::mix(int16_t* stereo, size_t frames, uint32_t rate, const Controls& controls) {
    if (!stereo || rate < 8000 || rate > 192000) return;
    // Two cascaded 15 ms poles turn the game's 30 Hz engine-speed steps into
    // a smooth sweep; 8 ms amplitude ramps avoid clicks.
    const float rpm_slew = 1 - std::exp(-1.0f / (0.015f * rate));
    const float throttle_slew = 1 - std::exp(-1.0f / (0.04f * rate));
    const float gain_slew = 1 - std::exp(-1.0f / (0.008f * rate));
    const float boost_up = 1 - std::exp(-1.0f / (0.7f * rate));
    const float boost_down = 1 - std::exp(-1.0f / (0.2f * rate));
    const float blowoff_decay = std::exp(-1.0f / (0.25f * rate));
    const float tail_hp = std::exp(-tau * 60 / rate);
    const float pipe_damping = one_pole(2500, rate);
    const float roar_filter = one_pole(1200, rate);
    const float flow_filter = one_pole(3000, rate);
    const float wander_filter = one_pole(3, rate);
    for (size_t v = 0; v < voice_count; ++v) {
        auto& s = voices_[v];
        const Control& c = controls[v];
        const int id = c.profile >= 0 && c.profile < int(profile_count) ? c.profile : 0;
        const Profile& p = profiles[id];
        const float gain = finite_clamp(c.gain, 0, 1);
        const float pan = finite_clamp(c.pan, 0, 1);
        const float rpm_target = std::isfinite(c.rpm) && c.rpm > 0
            ? std::clamp(c.rpm, 300.0f, 12000.0f)
            : std::clamp(rpm_from_pitch(finite_clamp(c.pitch, 0.1f, 2.0f), p.idle_rpm), 300.0f, 12000.0f);
        // Without a vehicle sample, assume a driven engine rather than overrun.
        const float throttle_target = std::isfinite(c.throttle) && c.throttle >= 0
            ? std::min(c.throttle, 1.0f) : 0.75f;
        if (s.start_revision != c.start_revision) {
            s.start_revision = c.start_revision;
            s.delay_seconds = finite_clamp(c.delay_seconds, 0, 10);
        }
        if (c.delay_seconds == 0) s.delay_seconds = 0;
        if (gain == 0 && s.gain < 0.00001f) { s.gain = 0; s.active = 0; continue; }
        if (s.coeff_profile != id || s.coeff_rate != rate) {
            if (s.coeff_rate == 0) { s.rpm = s.rpm_mid = rpm_target; s.throttle = throttle_target; }
            s.coeff_profile = id; s.coeff_rate = rate;
            retune(s.boom, bandpass(p.boom_hz, 2.5f, rate));
            retune(s.body, bandpass(p.body_hz, 1.8f, rate));
            retune(s.intake, bandpass(p.intake_hz, 2.0f, rate));
            retune(s.blow, bandpass(1900, 1.4f, rate));
            retune(s.turbo, bandpass(2500, 6, rate));
            s.next_cylinder = 0;
            while (s.next_cylinder < p.cylinders && p.firing_degrees[s.next_cylinder] < s.crank) ++s.next_cylinder;
            // Header lengths and cylinder filling differ slightly. The repeat
            // once per 720-degree cycle puts energy on every half order.
            uint32_t seed = 0x9E3779B9u * uint32_t(id + 1) + uint32_t(v);
            for (int k = 0; k < p.cylinders; ++k) {
                s.cyl_amp[k] = 1 + 0.24f * (uniform(seed) * 2 - 1);
                s.cyl_delay[k] = (p.runner_ms[p.bank[k]] + 0.3f * uniform(seed)) * 0.001f * rate;
                s.cyl_width[k] = 1 + 0.18f * (uniform(seed) * 2 - 1);
            }
        }
        const size_t pipe_len = s.pipe[0].size();
        const float pipe_delay[2] = {
            std::clamp(p.pipe_ms[0] * 0.001f * rate, 2.0f, float(pipe_len - 2)),
            std::clamp(p.pipe_ms[1] * 0.001f * rate, 2.0f, float(pipe_len - 2))};
        float& muffler_filter = s.muffler_coeff;
        float& fuel = s.fuel;
        for (size_t i = 0; i < frames; ++i) {
            if (s.delay_seconds > 0) { s.delay_seconds -= 1.0 / rate; continue; }
            s.rpm_mid += (rpm_target - s.rpm_mid) * rpm_slew;
            s.rpm += (s.rpm_mid - s.rpm) * rpm_slew;
            s.throttle += (throttle_target - s.throttle) * throttle_slew;
            s.gain += (gain - s.gain) * gain_slew;
            s.pan += (pan - s.pan) * gain_slew;
            const float load = std::clamp((s.rpm - 1500) / (p.redline_rpm - 1500), 0.0f, 1.0f);
            // Load-dependent coefficients change slowly; refresh every 32
            // samples of the voice's own clock so chunking cannot alter output.
            if ((uint64_t(s.clock) & 31) == 0 || muffler_filter == 0) {
                const float idle_fuel = std::clamp(1 - (s.rpm - p.idle_rpm) / 900, 0.0f, 1.0f) * 0.35f;
                fuel = std::max(s.throttle, idle_fuel);
                const float open = std::pow(fuel, 1.5f) * (0.6f + 0.4f * load);
                muffler_filter = one_pole(p.muffler_closed_hz + (p.muffler_open_hz - p.muffler_closed_hz) * open, rate);
                if (p.turbo > 0) retune(s.turbo, bandpass(1800 + 3200 * s.boost, 6, rate));
            }
            // Turbo spool lags throttle; a sharp lift with boost vents it.
            const float boost_target = p.turbo * s.throttle *
                std::clamp((s.rpm - 2200) / 2800, 0.0f, 1.0f);
            s.boost += (boost_target - s.boost) * (boost_target > s.boost ? boost_up : boost_down);
            if (p.turbo > 0 && s.prev_throttle - throttle_target > 0.5f && s.boost > 0.3f)
                s.blowoff = std::max(s.blowoff, s.boost);
            s.prev_throttle = throttle_target;
            s.blowoff *= blowoff_decay;
            s.since_lift = throttle_target < 0.05f ? s.since_lift + 1.0f / rate : 0;

            // Crank angle advances 6 * rpm degrees per second. Each firing
            // event schedules a pulse with sub-sample timing at its bank.
            // Driveline and governor wander keeps upper harmonics from locking
            // into a synthetic comb.
            if ((uint64_t(s.clock) & 31) == 0) s.wander = 0.02f * gauss(s.noise);
            s.wander_lp += wander_filter * (s.wander - s.wander_lp);
            const double step = s.rpm * (1 + s.wander_lp) * 6.0 / rate;
            double before = s.crank, after = s.crank + step;
            for (;;) {
                // 720 degrees acts as a sentinel event that wraps the cycle.
                const double angle = s.next_cylinder < p.cylinders ? p.firing_degrees[s.next_cylinder] : 720.0;
                if (angle > after) break;
                if (s.next_cylinder >= p.cylinders) { before -= 720; after -= 720; s.next_cylinder = 0; continue; }
                const int cyl = s.next_cylinder++;
                if (s.active == s.pulses.size()) continue;
                const float frac = float(std::clamp((angle - before) / step, 0.0, 1.0));
                const uint8_t bank = p.bank[cyl];
                // Weak, irregular combustion off throttle; occasional overrun
                // afterfire after a lift from high revs.
                const float variation = p.roughness * (1.8f - fuel);
                float amp = (0.12f + 0.88f * fuel) * s.cyl_amp[cyl] * std::max(0.2f, 1 + variation * gauss(s.noise));
                float sharp = 0;
                if (s.since_lift > 0.1f && s.since_lift < 1.8f && s.rpm > 0.45f * p.redline_rpm &&
                    uniform(s.noise) < 0.006f) { amp = 1.3f; sharp = 1; }
                const float width = p.pulse_ms * 0.001f * rate *
                    std::clamp(0.75f + 0.25f * 3000 / std::max(s.rpm, 800.0f), 0.8f, 1.5f) *
                    s.cyl_width[cyl] * (sharp ? 0.4f : 1);
                const float jitter = p.roughness * 0.15f * gauss(s.noise) * width;
                s.pulses[s.active++] = {s.clock + frac + s.cyl_delay[cyl] + jitter,
                                        amp, std::max(width, 1.0f), sharp, bank};
            }
            s.crank = after;
            float bank_in[2] = {0, 0};
            const float n = uniform(s.noise) * 2 - 1;
            // Gas leaving the valve is turbulent: each pulse carries coloured noise.
            s.flow_lp += flow_filter * (n - s.flow_lp);
            const float flow = 1 + (2.4f + 3.2f * fuel) * s.flow_lp;
            for (size_t k = 0; k < s.active;) {
                auto& pulse = s.pulses[k];
                const float u = float((s.clock - pulse.start) / pulse.width);
                if (u >= 2.5f) { pulse = s.pulses[--s.active]; continue; }
                if (u >= 0) bank_in[pulse.bank] += pulse.amp * pulse_shape(u) * flow;
                ++k;
            }
            // Each bank pipe is a damped waveguide: fixed-length delay with an
            // inverting open-end reflection. Its resonances do not track RPM.
            float collector = 0;
            for (int b = 0; b < 2; ++b) {
                const float read = float(s.pipe_pos) + float(pipe_len) - pipe_delay[b];
                const size_t r0 = size_t(read) % pipe_len, r1 = (r0 + 1) % pipe_len;
                const float t = read - std::floor(read);
                const float delayed = s.pipe[b][r0] * (1 - t) + s.pipe[b][r1] * t;
                s.pipe_lp[b] += pipe_damping * (delayed - s.pipe_lp[b]);
                const float y = bank_in[b] + p.pipe_reflection * s.pipe_lp[b];
                s.pipe[b][s.pipe_pos] = y;
                collector += y;
            }
            s.pipe_pos = (s.pipe_pos + 1) % pipe_len;
            s.muffler_lp += muffler_filter * (collector - s.muffler_lp);
            float exhaust = s.muffler_lp * (1 - 0.25f * p.turbo) +
                s.boom.run(collector) * p.boom_gain + s.body.run(collector) * p.body_gain;
            // Induction: firing-excited airbox resonance plus throttle roar.
            const float revs = s.rpm / p.redline_rpm;
            s.roar_lp += roar_filter * (n - s.roar_lp);
            const float intake = (s.intake.run(n * (0.3f + fuel) + bank_in[0] * 0.5f) +
                s.roar_lp * fuel * 0.6f) * p.intake_gain * revs;
            const float turbo = p.turbo * (s.turbo.run(n) * s.boost * s.boost * 0.35f +
                s.blow.run(n) * s.blowoff * 0.9f);
            // Open-end radiation: first-order high-pass removes pulse DC.
            const float dry = exhaust + intake + turbo;
            s.tail_hp_y = tail_hp * (s.tail_hp_y + dry - s.tail_hp_x);
            s.tail_hp_x = dry;
            // High-amplitude pressure waves steepen as they travel: hard load
            // and high revs add rasp; light load stays clean.
            const float drive = 0.6f + 1.8f * fuel * (0.3f + 0.7f * revs);
            const float shaped = std::tanh(s.tail_hp_y * drive) / drive;
            s.clock += 1;
            const float sample = shaped * p.level * s.gain * 5200;
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
