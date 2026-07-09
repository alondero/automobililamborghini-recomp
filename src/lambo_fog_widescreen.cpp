// 3P/4P fog widening (#83). Split-screen races program a dense, near-black fog
// (fm=6400, colour ~4,4,16) that pulls the horizon in far shorter than 1P's open
// dusk vista (fm=25600, colour 57,48,55) -- a performance mask for the four
// quadrant viewports. Measurement (issue #83) showed the underlying draw distance
// is largely intact, so widening the fog to the 1P window/colour reveals it.
//
// This is an ENHANCEMENT, not a faithful translation: it rewrites the game-built
// display list in RDRAM before the renderer consumes it. It runs on the shared DL
// (both RT64 and the headless swrender read the same RDRAM), only when the live
// player count is >= 3, and only when the "widescreen_fog_match" graphics.json key
// is enabled (default on; LAMBO_FOG_MATCH_1P=1/0 overrides). 1P/2P are untouched.

#include <cstdint>
#include <cstdlib>

#include "lambo_config.h"

namespace {

// F3DEX opcodes / moveword indices (mirror src/stub_renderer.cpp's walker).
constexpr uint8_t G_MOVEWORD    = 0xBC;
constexpr uint8_t G_DL          = 0x06;
constexpr uint8_t G_ENDDL       = 0xB8;
constexpr uint8_t G_SETFOGCOLOR = 0xF8;
constexpr uint8_t G_MW_SEGMENT  = 0x06;
constexpr uint8_t G_MW_FOG      = 0x08;

// 1P race fog, measured live (issue #83, Measurement 1):
//   gSPFogPosition -> fm=25600 (0x6400), fo=-25344 (s16 0x9D00) => w1 0x64009D00
//   gDPSetFogColor -> (57,48,55) = 0x393037 in the top 24 bits (alpha preserved)
constexpr uint32_t FOG_MW_1P   = 0x64009D00u;
constexpr uint32_t FOG_RGB_1P  = 0x39303700u;

// Segmented/direct address -> RDRAM offset (identical rules to the swrender's resolve()).
bool resolve(uint32_t addr, const uint32_t seg[16], uint32_t* out_off) {
    uint32_t hi = (addr >> 24) & 0xFF, phys;
    if (hi >= 0x80 && hi <= 0x9F)      phys = addr & 0x1FFFFFFF;      // KSEG0/1 direct
    else if (hi < 0x10)                phys = (seg[hi] & 0x1FFFFFFF) + (addr & 0x00FFFFFF);
    else                               return false;
    if (phys >= 0x00800000) return false;
    *out_off = phys;
    return true;
}

// Walk one DL, following G_DL calls/branches and tracking segments, rewriting every
// fog moveword and fog colour to the 1P values. Depth- and iteration-bounded.
void rewrite(uint8_t* rdram, uint32_t start_addr, uint32_t seg[16], int depth) {
    if (depth > 12) return;
    uint32_t off;
    if (!resolve(start_addr, seg, &off)) return;
    for (uint32_t i = 0; i < 200000; ++i) {
        if (off + 8 > 0x00800000) return;
        uint32_t w0 = *(uint32_t*)(rdram + off);
        uint8_t  op = (w0 >> 24) & 0xFF;
        switch (op) {
            case G_MOVEWORD:
                if ((w0 & 0xFF) == G_MW_SEGMENT) {
                    uint32_t segnum = (((w0 >> 8) & 0xFFFF) >> 2) & 0xF;
                    seg[segnum] = *(uint32_t*)(rdram + off + 4);
                } else if ((w0 & 0xFF) == G_MW_FOG) {
                    *(uint32_t*)(rdram + off + 4) = FOG_MW_1P;
                }
                break;
            case G_SETFOGCOLOR: {
                uint32_t* w1 = (uint32_t*)(rdram + off + 4);
                *w1 = FOG_RGB_1P | (*w1 & 0xFFu);   // keep the authored alpha byte
                break;
            }
            case G_DL: {
                uint32_t target = *(uint32_t*)(rdram + off + 4);
                if (((w0 >> 16) & 0xFF) != 0) {     // branch (jump): retarget, no return
                    if (!resolve(target, seg, &off)) return;
                    continue;
                }
                rewrite(rdram, target, seg, depth + 1);   // call (push): recurse
                break;
            }
            case G_ENDDL:
                return;
            default:
                break;
        }
        off += 8;
    }
}

} // namespace

// Called at the top of every send_dl (both render paths) with the graphics OSTask's
// display-list pointer. No-op unless enabled and the race is 3P/4P.
extern "C" void lambo_fog_match_1p(uint8_t* rdram, uint32_t dl_addr) {
    if (rdram == nullptr || !lambo::config::widescreen_fog_match()) return;
    // Live player count at 0x800CE6A4 (high 16 bits when read as an aligned word,
    // matching the swrender's state read at 0x800CE6AC). 1P/2P stay faithful.
    uint32_t players = (*(uint32_t*)(rdram + (0x800CE6A4u & 0x1FFFFFFF)) >> 16) & 0xFFFF;
    if (players < 3) return;
    uint32_t seg[16] = {0};
    rewrite(rdram, dl_addr, seg, 0);
}
