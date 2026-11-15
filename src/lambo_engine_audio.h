// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <array>
#include <cstddef>
#include <cstdint>

namespace lambo::engine_audio {
constexpr size_t voice_count = 4;
struct Control {
    float pitch = 0.1f; // Original sound-player playback ratio, not vehicle speed.
    float gain = 0.0f;  // Includes the cartridge's effects-volume setting.
    float pan = 64.0f / 127.0f;
    float delay_seconds = 0;
    uint64_t start_revision = 0;
};
using Controls = std::array<Control, voice_count>;

// Audio-thread-owned DSP. No guest memory, devices, allocation or locks.
class Synth {
public:
    void mix(int16_t* stereo, size_t frames, uint32_t rate, const Controls& controls);
private:
    struct Voice {
        double phase = 0;
        float pitch = 0.1f, gain = 0, pan = 0.5f;
        float exhaust = 0, intake = 0, dc = 0;
        uint32_t noise = 0x12345678;
        double delay_seconds = 0;
        uint64_t start_revision = 0;
    };
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
