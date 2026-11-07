#ifndef LAMBO_ANALOG_BRAKE_H
#define LAMBO_ANALOG_BRAKE_H

#include <cstdint>

namespace lambo::analog_brake {

constexpr unsigned kPortCount = 4;

// Host-input publication. effective_value is normalized to 0..1 and already
// includes the digital B/keyboard fallback when analog mode is enabled.
void publish(unsigned port, bool analog_mode, float effective_value);
void set_probe(bool enabled);

// Native sampling plus the exact packed interface consumed by guest source.
bool sample(unsigned port, float& effective_value);
// Exact packed guest input: bit31 analog, bit30 probe, low16 normalized.
uint32_t guest_snapshot(unsigned port);

} // namespace lambo::analog_brake

// Read-only game-thread diagnostic after the compiled guest update. Guest
// demand/ramp/latch writes live in guest/vehicle_update.c, not this adapter.
extern "C" void lambo_analog_brake_observe(uint8_t* rdram, uint32_t snapshot);

#endif
