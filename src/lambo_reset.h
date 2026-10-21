#pragma once

#include "recomp.h"

#ifdef __cplusplus
extern "C" {
#endif

// The frontend publishes a request; only the guest dispatcher consumes it.
void lambo_reset_request(void);
// Advances only after the game thread consumes a request and enters title init.
unsigned lambo_reset_generation(void);
// Runs before func_800028D0's stack prologue. A nonzero result asks the caller
// to return so the main loop can drain outstanding graphics completion messages.
int lambo_reset_tick(uint8_t* rdram, recomp_context* ctx);

#ifdef __cplusplus
}
#endif
