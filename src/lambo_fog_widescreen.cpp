// #83: widen 3P/4P split-screen fog to the 1P window/colour, and (fog_scale) scale
// fog density per track. Enhancement — rewrites the game-built display list in RDRAM
// at the top of every send_dl (both renderers read the same RDRAM). The 3P/4P match
// self-gates on live player count 0x800CE6A4 >= 3; the density scale applies whenever
// it is != 1 for the live circuit (0x800CE794).

#include <cstdint>
#include <cstdlib>

#include "lambo_config.h"

namespace {

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

// Fog moveword w1 packs fm (hi s16) and fo (lo s16); per-vertex fog alpha is
// clamp(fm * z + fo), so scaling BOTH by s scales the pre-clamp alpha by s: the
// fog thins uniformly (s=0 removes it) while the distance where it first appears
// (the alpha zero-crossing) stays where the ROM authored it.
uint32_t scale_fog_mw(uint32_t w1, double s) {
    double fm = (double)(int16_t)(w1 >> 16) * s;
    double fo = (double)(int16_t)(w1 & 0xFFFF) * s;
    if (fm > 32767.0) fm = 32767.0;
    if (fm < -32768.0) fm = -32768.0;
    if (fo > 32767.0) fo = 32767.0;
    if (fo < -32768.0) fo = -32768.0;
    return ((uint32_t)(uint16_t)(int16_t)fm << 16) | (uint16_t)(int16_t)fo;
}

// Walk one DL, following G_DL calls/branches and tracking segments, rewriting every
// fog moveword (and, for the 3P/4P match, fog colour) in place. Depth- and
// iteration-bounded.
void rewrite(uint8_t* rdram, uint32_t start_addr, uint32_t seg[16], int depth,
             bool match_1p, double fog_scale) {
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
                    uint32_t* w1 = (uint32_t*)(rdram + off + 4);
                    uint32_t v = match_1p ? FOG_MW_1P : *w1;
                    if (fog_scale != 1.0) v = scale_fog_mw(v, fog_scale);
                    *w1 = v;
                }
                break;
            case G_SETFOGCOLOR: {
                if (match_1p) {
                    uint32_t* w1 = (uint32_t*)(rdram + off + 4);
                    *w1 = FOG_RGB_1P | (*w1 & 0xFFu);   // keep the authored alpha byte
                }
                break;
            }
            case G_DL: {
                uint32_t target = *(uint32_t*)(rdram + off + 4);
                if (((w0 >> 16) & 0xFF) != 0) {     // branch (jump): retarget, no return
                    if (!resolve(target, seg, &off)) return;
                    continue;
                }
                rewrite(rdram, target, seg, depth + 1, match_1p, fog_scale);  // call (push)
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
// display-list pointer. No-op unless the 3P/4P fog match applies or the fog density
// scale for the live circuit differs from 1.
extern "C" void lambo_fog_match_1p(uint8_t* rdram, uint32_t dl_addr) {
    if (rdram == nullptr) return;
    // Live player count at 0x800CE6A4 (high 16 bits when read as an aligned word,
    // matching the swrender's state read at 0x800CE6AC). 1P/2P stay faithful.
    uint32_t players = (*(uint32_t*)(rdram + (0x800CE6A4u & 0x1FFFFFFF)) >> 16) & 0xFFFF;
    bool match_1p = lambo::config::widescreen_fog_match() && players >= 3;
    int circuit = (int16_t)((*(uint32_t*)(rdram + (0x800CE794u & 0x1FFFFFFF)) >> 16) & 0xFFFF);
    double fog_scale = lambo::config::fog_scale(circuit);
    if (!match_1p && fog_scale == 1.0) return;
    uint32_t seg[16] = {0};
    rewrite(rdram, dl_addr, seg, 0, match_1p, fog_scale);
}
