#ifndef LAMBO_ANALOG_BRAKE_H
#define LAMBO_ANALOG_BRAKE_H

#include <cstdint>

namespace lambo::analog_brake {

constexpr unsigned kPortCount = 4;

// Called by the host input thread after profile evaluation. In analog mode,
// effective_value already includes the digital B/keyboard fallback.
void publish(unsigned port, bool analog_mode, float effective_value);
void set_probe(bool enabled);

// Exposed for deterministic tests and instrumentation.
bool sample(unsigned port, float& effective_value);
float scale_to_guest(uint16_t normalized);

} // namespace lambo::analog_brake

// Race-thread hook, invoked at the stock brake block's merge point
// (vram 0x8001A9A0 in func_80019D20), after the ROM's own brake handlers have
// run and before the physics pass consumes the demand. Reads only the atomics
// above and guest RDRAM.
extern "C" void lambo_analog_brake_apply(uint8_t* rdram);

#endif
