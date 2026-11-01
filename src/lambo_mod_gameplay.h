#ifndef LAMBO_MOD_GAMEPLAY_H
#define LAMBO_MOD_GAMEPLAY_H

#include "recomp.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Versioned guest ABI: a0 is an aligned KSEG0 pointer to one unsigned guest
 * 32-bit maneuver-cap value in native throttle-demand units (0..100), or zero
 * to unregister. v0 is 1 on success, 0 for an invalid pointer; all other
 * registers are unchanged. Invalid registration retains the previous pointer.
 * The whole word must lie in recomp::mem_size. Guest word order is handled by
 * MEM_W. Registration does not read the word or retain a host address.
 *
 * One mod owns the registration; the last successful registration wins. It
 * must keep the guest storage alive until unregistering or restarting. Guest
 * registration, updates and helper reads all run on the game thread; no host
 * thread may mutate the registration concurrently. The cap remains in guest
 * memory and is read afresh, rather than retained in a stale host-side copy.
 * A snapshot must include that storage to restore its value: the current debug
 * savestate captures only low 8 MiB and excludes normal mod allocations. This
 * bridge does not extend that format. No fixed guest location is reserved.
 */
void lambo_register_ai_maneuver_cap_v1(uint8_t* rdram, recomp_context* ctx);

/* Called at the measured AI maneuver branch, not a global throttle clamp.
 * With no registration, or a guest value greater than 100, returns the stock
 * cap 100. It changes neither guest memory nor registers.
 */
unsigned lambo_ai_maneuver_cap(uint8_t* rdram);

/* Runtime initialization only, before mods execute and before a game thread
 * can read the bridge. A new game must not inherit a previous mod's pointer.
 */
void lambo_reset_mod_gameplay_bridges(void);

#ifdef __cplusplus
}
#endif

#endif
