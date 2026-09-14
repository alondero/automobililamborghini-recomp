#ifndef LAMBO_ANALOG_THROTTLE_H
#define LAMBO_ANALOG_THROTTLE_H

#include <cstdint>

namespace lambo::analog_throttle {

constexpr unsigned kPortCount = 4;

// Host-input publication. effective_value is normalized to 0..1 and already
// includes the digital A/keyboard fallback when analog mode is enabled.
void publish(unsigned port, bool analog_mode, float effective_value);
void disconnect(unsigned port, bool analog_mode);
void set_probe(bool enabled);

// Pure state/scale seams used by deterministic host tests.
bool sample(unsigned port, float& effective_value);
int16_t scale_to_guest(uint16_t normalized, int16_t limit);

} // namespace lambo::analog_throttle

// Paired game-thread hooks at guest addresses 0x80019FBC and 0x8001A120.
// They read one published atomic snapshot and the current vehicle record at
// 0x800B69A8 + index*0x10C, then read/write the s16 throttle at +0xAA using
// N64 word-swapped MEM access. Invalid channels and negative/inactive vehicle
// indices are a no-op. The first hook captures the pre-ROM value; the second runs before
// physics. See docs/analog-throttle.md and docs/architecture.md.
extern "C" void lambo_analog_throttle_begin(uint8_t* rdram);
extern "C" void lambo_analog_throttle_apply(uint8_t* rdram);

#endif
