// Exact USA texture contracts. Only the two clamp bits in render tile 0 are
// rewritten, on the guest producer before publication. See docs/HUD.md.
#include "lambo_f3dex.h"
#include "lambo_hud_texture_bounds.h"

namespace {
constexpr uint32_t clamp = 0x00080200u;
enum class Kind { Dial, Map, Suffix };
struct TileContract {
    uint32_t source, tile0, tile1, bounds;
    Kind kind;
};
constexpr TileContract tiles[] = {
    {0x160140, 0xf5181400, 0x0000c060, 0x0009c014, Kind::Dial},
    {0x160508, 0xf5180800, 0x00018040, 0x0003c0e0, Kind::Dial},
    {0x121a88, 0xf5600a00, 0x0001c070, 0x0013c13c, Kind::Map},
    {0x122710, 0xf5600a00, 0x0001c070, 0x0013c13c, Kind::Map},
    {0x123398, 0xf5600a00, 0x0001c070, 0x0013c13c, Kind::Map},
    {0x124020, 0xf5600a00, 0x0001c070, 0x0013c13c, Kind::Map},
    {0x124ca8, 0xf5600a00, 0x0001c070, 0x0013c13c, Kind::Map},
    {0x125930, 0xf5600a00, 0x0001c070, 0x0013c13c, Kind::Map},
    {0x165028, 0xf5400c00, 0x00010070, 0x0017c01c, Kind::Suffix},
};
}

void lambo::HudTextureBounds::command(uint32_t offset, uint32_t w0, uint32_t w1) {
    switch (w0 >> 24) {
    case 0xfd:
        // These USA emitters use direct physical/KSEG0 source pointers.
        source = w1 & 0x1fffffff;
        tile = 0;
        break;
    case 0xf5:
        if ((w1 >> 24) == 0) tile = offset;
        break;
    case 0xf2:
        if (w0 != 0xf2000000 || !tile) break;
        for (const auto& contract : tiles) {
            const auto t0 = f3dex::read(ram, tile), t1 = f3dex::read(ram, tile + 4);
            // Map/suffix layouts may mirror both axes. Dial layouts must match
            // exactly. Preserve authored mirror bits when enabling clamp.
            const uint32_t mirror = contract.kind == Kind::Dial ? 0 : 0x00040100;
            if (source != contract.source || t0 != contract.tile0 || w1 != contract.bounds ||
                ((t1 & ~clamp) != contract.tile1 &&
                 (t1 & ~clamp) != (contract.tile1 | mirror))) continue;
            f3dex::write(ram, tile + 4, t1 | clamp);
            switch (contract.kind) {
            case Kind::Dial: ++dial; break;
            case Kind::Map: ++maps; break;
            case Kind::Suffix: ++suffix; break;
            }
            break;
        }
        tile = 0;
        break;
    default: break;
    }
}
