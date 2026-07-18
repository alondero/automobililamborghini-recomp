// #87/#91: remove the ROM's per-mode LOD reductions. The scene builder
// func_8000A6C0 draws each track segment as up to three sub-DLs from the
// segment record (+0x4 road, +0x8 walls, +0xC far scenery) but emits the
// scenery layer only when the player count at 0x800CE6A4 is < 2
// (`slti $at, players, 2` / `beq $at, $zero` at 0x8000CFA0/0x8000CFA4 in the
// segment loop and 0x8000D834/0x8000D838 for the camera's own segment), so
// 2P-4P races lose the distant canyon walls entirely -- seen as "short draw
// distance" and pop-in. [[patches.hook]]s before each beq route $at through
// here: returning 1 makes every mode take the branch the way 1P takes it.
// The emit still self-gates on the record's scenery pointer being non-null,
// and the scenery DLs are streamed in all modes (verified from a 3P save
// state: record+0xC pointers populated), so no geometry is synthesised here.

#include <cstdint>

#include "recomp.h"

#include "lambo_config.h"

extern "C" uint32_t lambo_no_lod_scenery_guard(uint8_t* rdram, uint32_t at) {
    (void)rdram;
    return lambo::config::no_lod() ? 1u : at;
}

// Distance pop-in (the other half of the same builder): each entry of a segment's
// 10-slot visibility list is culled against a per-circuit, per-player-count radius
// from a float[6][5] table at 0x80088FD0 (coarse test 0x8000D370, fine 16-sub-point
// test 0x8000D568). The radii are N64 fill-rate budgets -- the city circuits are
// authored shortest (35000 vs 55000 on circuit 1), so whole blocks pop in at the
// radius edge. Hooked per frame on the world-draw path (0x8000CD3C, before the first
// table read) rather than once at load because a savestate restore brings the ROM
// values back. Only the radius is lifted: the forward-cone/half-plane tests and the
// authored per-segment visibility lists still decide what is drawn.
extern "C" void lambo_no_lod_draw_distance(uint8_t* rdram) {
    if (!lambo::config::no_lod()) {
        return;
    }
    constexpr uint32_t kTableAddr = 0x80088FD0u;  // float[6][5]: [circuit][player-count]
    constexpr int32_t kFarBits = 0x4E6E6B28;      // 1e9f, beyond any on-track distance
    for (uint32_t i = 0; i < 6 * 5; i++) {
        MEM_W(i * 4, (gpr)(int32_t)kTableAddr) = kFarBits;
    }
}
