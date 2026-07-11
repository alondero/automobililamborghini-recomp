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

#include "lambo_config.h"

extern "C" uint32_t lambo_no_lod_scenery_guard(uint8_t* rdram, uint32_t at) {
    (void)rdram;
    return lambo::config::no_lod() ? 1u : at;
}
