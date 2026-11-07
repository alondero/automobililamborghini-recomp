#include "lambo_config.h"
#include "lambo_guest_calls.h"

// The guest patch owns pit-service state. This host ABI adapter only returns
// the atomic UI setting; it does not read or write guest memory.
extern "C" void lambo_get_automatic_pit_stops(uint8_t*, recomp_context* ctx) {
    ctx->r2 = lambo::config::automatic_pit_stops();
}
