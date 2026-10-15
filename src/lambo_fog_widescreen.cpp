// Fog policy applied by the shared guest-producer walk before publication.
// Player count/circuit are producer-owned USA halfwords; see docs/HUD.md.
#include "lambo_config.h"
#include "lambo_f3dex.h"
#include "lambo_fog_widescreen.h"

namespace {
constexpr uint32_t FOG_MW_1P = 0x64009D00u;
constexpr uint32_t FOG_RGB_1P = 0x39303700u;

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

} // namespace

lambo::FogPolicy lambo::fog_policy(const uint8_t* ram) {
    const auto players = f3dex::read(ram, 0xce6a4) >> 16;
    const auto circuit = int16_t(f3dex::read(ram, 0xce794) >> 16);
    return {config::widescreen_fog_match() && players >= 3, config::fog_scale(circuit)};
}

void lambo::FogPolicy::command(uint8_t* ram, uint32_t offset, uint32_t w0, uint32_t w1) const {
    if ((w0 >> 24) == 0xbc && (w0 & 255) == 8 && (match_1p || scale != 1.0)) {
        const auto value = match_1p ? FOG_MW_1P : w1;
        f3dex::write(ram, offset + 4, scale == 1.0 ? value : scale_fog_mw(value, scale));
    } else if ((w0 >> 24) == 0xf8 && match_1p) {
        f3dex::write(ram, offset + 4, FOG_RGB_1P | (w1 & 255));
    }
}
