#include "lambo_mod_gameplay.h"
#include "librecomp/addresses.hpp"
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace {
int failures = 0;
void expect(bool condition, const char* message) {
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", message);
        ++failures;
    }
}

void register_address(uint8_t* rdram, uint32_t address, bool valid) {
    recomp_context ctx{};
    ctx.r1 = 0x12345678;
    ctx.r2 = 0xABCDEF;
    ctx.r4 = static_cast<int32_t>(address);
    ctx.r5 = 0x87654321;
    ctx.r29 = 0x80000100;
    ctx.r31 = 0x80000200;
    recomp_context expected = ctx;
    expected.r2 = valid ? 1 : 0;
    lambo_register_ai_maneuver_cap_v1(rdram, &ctx);
    expect(std::memcmp(&ctx, &expected, sizeof(ctx)) == 0,
           "registration returns only v0, preserving every other context byte");
}
}

int main() {
    alignas(8) std::array<uint8_t, 32> memory{};
    uint8_t* rdram = memory.data();
    const auto set_cap = [&](uint32_t value) { MEM_W(0, static_cast<int32_t>(0x80000004u)) = value; };
    lambo_reset_mod_gameplay_bridges();
    expect(lambo_rain_settings(nullptr) == 0, "absent rain bridge is inert");
    recomp_context rain_ctx{};
    rain_ctx.r4 = static_cast<int32_t>(0x80000008u);
    lambo_register_rain_v1(rdram, &rain_ctx);
    expect(rain_ctx.r2 == 1, "rain word can be registered");
    for (unsigned style = 0; style <= 2; ++style) {
        for (unsigned intensity = 0; intensity <= 100; ++intensity) {
            unsigned value = style | (intensity << 8) | 0x10000;
            MEM_W(0, static_cast<int32_t>(0x80000008u)) = value;
            expect(lambo_rain_settings(rdram) == value, "rain settings preserve all valid combinations");
        }
    }
    for (uint32_t value : {3u, 0x6501u, 4u, 0x20001u, 0xFFFFFFFFu}) {
        MEM_W(0, static_cast<int32_t>(0x80000008u)) = value;
        expect(lambo_rain_settings(rdram) == 0, "invalid rain words fail closed");
    }
    rain_ctx.r4 = static_cast<int32_t>(0x80000009u);
    lambo_register_rain_v1(rdram, &rain_ctx);
    expect(rain_ctx.r2 == 0, "unaligned rain registration is rejected");
    lambo_reset_mod_gameplay_bridges();
    expect(lambo_rain_settings(nullptr) == 0, "reset clears the rain pointer");
    expect(lambo_ai_maneuver_cap(nullptr) == 100, "absent bridge does not dereference RAM");
    set_cap(60);
    register_address(rdram, 0x80000004u, true);
    expect(lambo_ai_maneuver_cap(rdram) == 60, "reads registered guest word");
    for (const uint32_t invalid : {1u, 0x7FFFFFFCu, 0x80000001u, 0x80000002u,
                                  0x80000003u, 0xA0000000u, 0xFFFFFFFCu, 0xFFFFFFFFu}) {
        register_address(rdram, invalid, false);
        expect(lambo_ai_maneuver_cap(rdram) == 60, "invalid pointer retains prior registration");
    }
    for (const uint32_t value : {0u, 1u, 60u, 100u, 101u, 0x80000000u, 0xFFFFFFFFu}) {
        set_cap(value);
        const auto before = memory;
        expect(lambo_ai_maneuver_cap(rdram) == (value <= 100 ? value : 100),
               "unsigned cap range is checked on every read");
        expect(memory == before, "helper never changes guest memory");
    }
    // Guest-memory restoration changes the effective cap without re-registering.
    set_cap(60);
    const auto snapshot = memory;
    set_cap(100);
    memory = snapshot;
    expect(lambo_ai_maneuver_cap(rdram) == 60, "restored guest state owns the cap value");
    register_address(rdram, 0, true);
    expect(lambo_ai_maneuver_cap(nullptr) == 100, "unregister restores stock cap");
    // Registration checks the complete final word but never dereferences it.
    const uint64_t end = 0x80000000ull + recomp::mem_size;
    const uint32_t limit = static_cast<uint32_t>(end < 0xA0000000ull ? end : 0xA0000000ull);
    register_address(nullptr, limit - 4, true);
    register_address(nullptr, limit - 3, false);
    register_address(nullptr, limit, false);
    lambo_reset_mod_gameplay_bridges();
    expect(lambo_ai_maneuver_cap(nullptr) == 100, "reset removes previous-game registration");
    register_address(rdram, 0x80000000u, true);
    MEM_W(0, static_cast<int32_t>(0x80000000u)) = 25;
    expect(lambo_ai_maneuver_cap(rdram) == 25, "first aligned KSEG0 word is accepted");
    lambo_reset_mod_gameplay_bridges();
    return failures ? 1 : 0;
}
