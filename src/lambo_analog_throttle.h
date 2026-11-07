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

// Native sampling plus the exact packed interface consumed by guest source.
bool sample(unsigned port, float& effective_value);
// Guest source consumes exact quantized input: bit31 analog, bit30 probe,
// low16 normalized pedal. No guest pointer or memory writes cross this seam.
uint32_t guest_snapshot(unsigned port);

} // namespace lambo::analog_throttle

// Read-only game-thread diagnostic after the compiled guest update. Guest
// demand/ramp/latch writes live in guest/vehicle_update.c, not this adapter.
extern "C" void lambo_analog_throttle_observe(uint8_t* rdram, uint32_t snapshot);

#endif
