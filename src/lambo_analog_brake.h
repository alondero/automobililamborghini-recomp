#ifndef LAMBO_ANALOG_BRAKE_H
#define LAMBO_ANALOG_BRAKE_H

#include <cstdint>

namespace lambo::analog_brake {

constexpr unsigned kPortCount = 4;

// Host-input publication. effective_value is normalized to 0..1 and already
// includes the digital B/keyboard fallback when analog mode is enabled.
void publish(unsigned port, bool analog_mode, float effective_value);
void set_probe(bool enabled);

// Pure state/scale seams used by deterministic host tests.
bool sample(unsigned port, float& effective_value);
float scale_to_guest(uint16_t normalized);

} // namespace lambo::analog_brake

// Game-thread hook at the stock brake block's merge point (guest address
// 0x8001A9A0). It reads one published atomic snapshot and the current vehicle
// record at 0x800B69A8 + index*0x10C, then writes the float32 demand at +0xA0
// and the s16 brake latch at +0xAC using N64 word-swapped MEM access. Invalid
// channels and negative/inactive vehicle indices are a no-op. The hook runs before physics
// consumes the fields; its measured layout and replacement path are in
// docs/analog-brake.md and docs/architecture.md.
extern "C" void lambo_analog_brake_apply(uint8_t* rdram);

#endif
