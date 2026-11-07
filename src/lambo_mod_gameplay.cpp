#include "lambo_mod_gameplay.h"
#include "librecomp/addresses.hpp"

namespace {
uint32_t maneuver_cap_address = 0;
uint32_t rain_address = 0;
}

extern "C" void lambo_register_ai_maneuver_cap_v1(uint8_t*, recomp_context* ctx) {
    const uint32_t address = static_cast<uint32_t>(ctx->r4);
    ctx->r2 = 0;
    if (address != 0 &&
        ((address & 3u) != 0 || address < 0x80000000u || address >= 0xA0000000u ||
         uint64_t(address) + sizeof(uint32_t) > 0x80000000ull + recomp::mem_size))
        return;
    maneuver_cap_address = address;
    ctx->r2 = 1;
}

extern "C" unsigned lambo_ai_maneuver_cap(uint8_t* rdram) {
    if (maneuver_cap_address == 0)
        return 100;
    const uint32_t value = static_cast<uint32_t>(
        MEM_W(0, static_cast<int32_t>(maneuver_cap_address)));
    return value <= 100 ? value : 100;
}

extern "C" void lambo_reset_mod_gameplay_bridges(void) {
    maneuver_cap_address = 0;
    rain_address = 0;
}

extern "C" void lambo_register_rain_v1(uint8_t*, recomp_context* ctx) {
    const uint32_t address = static_cast<uint32_t>(ctx->r4);
    ctx->r2 = 0;
    if (address != 0 &&
        ((address & 3u) != 0 || address < 0x80000000u || address >= 0xA0000000u ||
         uint64_t(address) + sizeof(uint32_t) > 0x80000000ull + recomp::mem_size))
        return;
    rain_address = address;
    ctx->r2 = 1;
}

extern "C" unsigned lambo_rain_settings(uint8_t* rdram) {
    if (rain_address == 0) return 0;
    const uint32_t value = static_cast<uint32_t>(MEM_W(0, static_cast<int32_t>(rain_address)));
    if ((value & ~0x17F03u) || (value & 3u) > 2 || ((value >> 8) & 127u) > 100) return 0;
    return value;
}
