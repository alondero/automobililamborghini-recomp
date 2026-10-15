#pragma once
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Guest producer / frame-boundary save-state API. snapshot is a separate 8 MiB
// word-swapped RAM copy. Normalize only known corrected fog words in that copy;
// never modify the live lists a pending task may still reference.
void lambo_graphics_fog_snapshot(const uint8_t* rdram, uint8_t* snapshot);
void lambo_graphics_fog_loaded(uint8_t* rdram);
// Legacy snapshots carry no authored-word marker. Reject them when the current
// policy would multiply an unknown, potentially already scaled fog value.
int lambo_graphics_fog_legacy_safe(const uint8_t* snapshot);

#ifdef __cplusplus
}
#endif
