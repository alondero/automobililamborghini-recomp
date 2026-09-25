#include "lambo_config.h"
#include "recomp.h"

// Game-thread-only hooks inside the USA ROM's pit input handler (func_8006B470).
// Its 0x48-byte stack frame saves signed stick bytes at sp+0x4B/0x4F and the
// zero-based pit slot at sp+0x5B. MEM_* handles the runtime's word-swapped RAM.
// Only these local inputs change; controller snapshots and buttons stay intact.
// No host progress is retained, so toggling or loading a state needs no reset.
// Fixed seams, bounds and disassembly evidence: docs/automatic-pit-stops.md.
namespace {
bool active(uint8_t* rdram, recomp_context* ctx) {
    return lambo::config::automatic_pit_stops() &&
           MEM_BU(0x5B, ctx->r29) < 2;
}
}

extern "C" void lambo_pit_start(uint8_t* rdram, recomp_context* ctx) {
    if (active(rdram, ctx)) MEM_B(0x4F, ctx->r29) = 8;
}

extern "C" void lambo_pit_fuel(uint8_t* rdram, recomp_context* ctx) {
    if (!active(rdram, ctx)) return;
    const unsigned slot = MEM_BU(0x5B, ctx->r29);
    const int limit = MEM_W(0x229CA8 + slot * 4, (gpr)(int32_t)0x80000000u);
    // The ROM subtracts 7, clamps to [0,63], then truncates after multiplying
    // by 0.7. Pick the largest representable pressure below the spill limit, reserving one unit
    // for the limit falling on the next tick as fuel increases.
    // The stock ramp and sine-shaped delivery rate still run unchanged.
    if (limit < 1 || limit > 44) return;
    int stick = 7;
    for (int candidate = 8; candidate <= 70; ++candidate) {
        if (static_cast<int>((candidate - 7) * 0.7) >= limit - 1) break;
        stick = candidate;
    }
    MEM_B(0x4F, ctx->r29) = stick;
}

extern "C" void lambo_pit_tyres(uint8_t* rdram, recomp_context* ctx) {
    if (!active(rdram, ctx)) return;
    // s0 is the ROM's next direction, loaded from its rotation-order table.
    // Follow it rather than keeping a second rotation counter on the host.
    int x = 0, y = 0;
    switch (ctx->r16) {
        case 1: x = -60; break;
        case 2: x = 60; break;
        case 3: y = 60; break;
        case 4: y = -60; break;
        default: return;
    }
    MEM_B(0x4B, ctx->r29) = x;
    MEM_B(0x4F, ctx->r29) = y;
}
