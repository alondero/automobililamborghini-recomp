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

void lambo::FogRewriter::begin(const uint8_t* ram) {
    const auto identity = reinterpret_cast<uintptr_t>(ram);
    if (identity != memory_identity) {
        words.clear();
        memory_identity = identity;
    }
}

void lambo::FogRewriter::command(uint8_t* ram, uint32_t offset, uint32_t w0, uint32_t w1,
                                FogPolicy policy) {
    const bool position = (w0 >> 24) == 0xbc && (w0 & 255) == 8;
    if (!position && (w0 >> 24) != 0xf8) {
        // A rebuilt arena can reuse the address for another command entirely.
        if (!words.empty()) words.erase(offset);
        return;
    }
    auto previous = words.find(offset);
    // A fresh opcode/value replaces the baseline. An unchanged previous output
    // is our own correction, so scale its authored word again, never that output.
    const bool ours = previous != words.end() && previous->second.opcode == w0 &&
        previous->second.rewritten == w1;
    const uint32_t authored = ours ? previous->second.authored : w1;
    uint32_t value;
    if (position) {
        const auto base = policy.match_1p ? FOG_MW_1P : authored;
        value = policy.scale == 1.0 ? base : scale_fog_mw(base, policy.scale);
    } else {
        value = policy.match_1p ? FOG_RGB_1P | (authored & 255) : authored;
    }
    if (value == authored) {
        if (previous != words.end()) words.erase(previous);
    } else {
        words.insert_or_assign(offset, Word{w0, authored, value});
    }
    if (value != w1) f3dex::write(ram, offset + 4, value);
}
