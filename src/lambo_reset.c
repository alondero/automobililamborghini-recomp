#include "lambo_reset.h"

#include <stdatomic.h>

#include "lambo_log.h"
#include "lambo_vehicle.h"

void func_800676B4(uint8_t* rdram, recomp_context* ctx);
void func_80008ECC(uint8_t* rdram, recomp_context* ctx);

static _Atomic int pending;
static _Atomic unsigned generation;

unsigned lambo_reset_generation(void) {
    return atomic_load_explicit(&generation, memory_order_acquire);
}

void lambo_reset_request(void) {
    atomic_store_explicit(&pending, 1, memory_order_release);
}

int lambo_reset_tick(uint8_t* rdram, recomp_context* ctx) {
    if (!atomic_load_explicit(&pending, memory_order_acquire)) return 0;

    // USA s16 game-state halfword, in guest byte order. At the top-level
    // dispatcher boundary, boot and the state-7 race loader must finish before
    // another transition can safely replace their assets. Unknown states fail
    // closed, leaving the request queued. All guest accesses here belong to
    // the game thread; the frontend never reads or writes guest memory.
    const int state = MEM_H(0, (gpr)(int32_t)LAMBO_GUEST_GAME_STATE_ADDR);
    if (state < 3 || state > 16 || state == 7) return 0;

    // USA s16 outstanding graphics-task count. As with the race-loader fence,
    // return before the prologue while old tasks still reference scene memory;
    // blocking would prevent the main loop receiving their completion messages.
    // Address/lifetime evidence: docs/sky-panorama.md. Replace this bridge with
    // a typed game transition once the dispatcher is decompiled.
    if (MEM_H(0, (gpr)(int32_t)0x80098270u) > 0) return 1;

    atomic_exchange_explicit(&pending, 0, memory_order_acq_rel);
    // The retail transition pair stops/drains audio (e.g. USA 0x80008290).
    // State 3 is the title initializer: dispatcher 0x80001D50 calls USA
    // 0x80037B94 (generated symbol func_80038794), clears the attract cursor
    // and advances to state 4.
    // Let that retail path rebuild title assets, timers and input state.
    func_800676B4(rdram, ctx);
    func_80008ECC(rdram, ctx);
    MEM_H(0, (gpr)(int32_t)LAMBO_GUEST_GAME_STATE_ADDR) = 3;
    atomic_fetch_add_explicit(&generation, 1, memory_order_release);
    LAMBO_LOG_INFO("reset", "returning to title from state %d\n", state);
    return 0;
}
