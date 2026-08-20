#ifndef LAMBO_ANALOG_THROTTLE_H
#define LAMBO_ANALOG_THROTTLE_H

#include <cstdint>

namespace lambo::analog_throttle {

constexpr unsigned kPortCount = 4;

// Called by the host input thread after profile evaluation. In analog mode,
// effective_value already includes the digital A/keyboard fallback.
void publish(unsigned port, bool analog_mode, float effective_value);
void disconnect(unsigned port, bool analog_mode);
void set_probe(bool enabled);

// Exposed for deterministic tests and instrumentation.
bool sample(unsigned port, float& effective_value);
int16_t scale_to_guest(uint16_t normalized, int16_t limit);

} // namespace lambo::analog_throttle

// Paired race-thread hooks. Reads only the atomics above and guest RDRAM.
extern "C" void lambo_analog_throttle_begin(uint8_t* rdram);
extern "C" void lambo_analog_throttle_apply(uint8_t* rdram);

#endif
