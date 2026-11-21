// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <array>
#include <cstddef>
#include <cstdint>

namespace lambo::engine_audio {
constexpr size_t voice_count = 4;
constexpr size_t profile_count = 8;
constexpr int unknown_profile = -1;

// Update flags: bits 1/2/4 mirror the cartridge pitch/volume/pan selectors.
constexpr unsigned set_pitch = 1, set_gain = 2, set_pan = 4, set_vehicle = 8;

struct Control {
    float pitch = 0.1f; // Original sound-player playback ratio, not vehicle speed.
    float gain = 0.0f;  // Includes the cartridge's effects-volume setting.
    float pan = 64.0f / 127.0f;
    float delay_seconds = 0;
    uint64_t start_revision = 0;
    // Vehicle state sampled by the guest bridge. rpm <= 0 derives engine speed
    // from pitch; throttle < 0 is unknown; profile is the vehicle category.
    float rpm = 0;
    float throttle = -1;
    int profile = unknown_profile;
};
using Controls = std::array<Control, voice_count>;

// One engine layout: firing angles within the 720-degree four-stroke cycle,
// the exhaust bank each cylinder feeds, and fixed-frequency exhaust acoustics.
struct Profile {
    const char* name;
    int cylinders;
    std::array<float, 12> firing_degrees;
    std::array<uint8_t, 12> bank;
    float idle_rpm, redline_rpm;
    float pulse_ms;           // Exhaust blowdown pulse width.
    float runner_ms[2];       // Header delay before each bank's collector.
    float pipe_ms[2];         // Bank pipe round-trip delay (fixed resonance).
    float pipe_reflection;    // Open-end reflection is negative.
    float boom_hz, boom_gain; // Silencer cavity resonance.
    float body_hz, body_gain; // Pipe/body resonance.
    float muffler_closed_hz, muffler_open_hz;
    float intake_hz, intake_gain;
    float roughness;          // Cycle-to-cycle combustion variation.
    float turbo;              // 0 = naturally aspirated.
    float level;
};
const Profile& profile(int category);
// Inverse of the cartridge's pitch = 0.1 + rpm * 1.5 / (8000 - idle).
float rpm_from_pitch(float pitch, float idle_rpm);

// Audio-thread-owned DSP. No guest memory, devices, allocation or locks.
class Synth {
public:
    void mix(int16_t* stereo, size_t frames, uint32_t rate, const Controls& controls);
    struct Pulse { double start; float amp, width, sharp; uint8_t bank; };
    struct Biquad {
        float b0 = 0, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
        float run(float x);
    };
    struct Voice {
        double crank = 0, clock = 0;
        int next_cylinder = 0;
        float rpm = 0, rpm_mid = 0, throttle = 0, gain = 0, pan = 0.5f;
        float boost = 0, blowoff = 0, since_lift = 10, prev_throttle = 0;
        float pipe_lp[2]{}, muffler_lp = 0, tail_hp_x = 0, tail_hp_y = 0, intake_lp = 0;
        float roar_lp = 0, muffler_coeff = 0, fuel = 0, flow_lp = 0, wander = 0, wander_lp = 0;
        // Fixed per-cylinder signature: charge, header delay and pulse width.
        std::array<float, 12> cyl_amp{}, cyl_delay{}, cyl_width{};
        std::array<std::array<float, 2048>, 2> pipe{};
        size_t pipe_pos = 0;
        std::array<Pulse, 32> pulses{};
        size_t active = 0;
        Biquad boom, body, intake, turbo, blow;
        int coeff_profile = -2;
        uint32_t coeff_rate = 0, noise = 0x12345678;
        double delay_seconds = 0;
        uint64_t start_revision = 0;
    };
private:
    std::array<Voice, voice_count> voices_{};
};

// Launch-time A/B switch: LAMBO_ENGINE_AUDIO=0 retains cartridge engines.
bool enabled();
// Game-thread publication; audio takes one short, coherent snapshot per buffer.
void publish(size_t voice, unsigned flags, Control control);
void start(size_t voice, Control control, float delay_seconds);
void stop(size_t voice);
void stop_all();
void mix(int16_t* stereo, size_t frames, uint32_t rate, uint64_t generation);
}
