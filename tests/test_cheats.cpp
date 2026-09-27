#include "lambo_cheats.h"
#include "recomp.h"
#include <cstdlib>
#include <iostream>
#include <vector>

extern "C" void cheat_test_timer(uint8_t*, recomp_context*);
extern "C" void func_8003F40C(uint8_t*, recomp_context*);
extern "C" void func_8003F56C(uint8_t*, recomp_context*);
extern "C" void func_80066F84(uint8_t*, recomp_context*) {} // Countdown warning sound.

static void require(bool condition, const char* message) {
    if (!condition) { std::cerr << message << '\n'; std::exit(1); }
}

int main() {
    using namespace lambo::cheats;
    std::vector<uint8_t> memory(8 * 1024 * 1024);
    uint8_t* rdram = memory.data();
    const gpr base = static_cast<int32_t>(0x80000000u);
    recomp_context ctx{};
    ctx.r29 = base + 0x700000;
    for (const auto& entry : catalog) require(!enabled(entry.cheat), "cheat defaults on");
    // Synthetic guest-RAM fixtures follow the USA dispatcher and data tables:
    // 0xCE6B0/0xCE6B4 (halfwords) control countdown warning branches; values
    // 3/1 enter the timed race path while skipping the optional warning sound.
    // 0xCE76E is remaining seconds and 0xCE772 is its 1/60-second tick count.
    MEM_H(0xCE6B0, base) = 3;
    MEM_H(0xCE6B4, base) = 1;
    MEM_H(0xCE76E, base) = 40;
    MEM_H(0xCE772, base) = 58;
    // Four player race-clock records at 0x98768 + p*8 and current-lap
    // records at 0x98850 + p*8 each store seconds at +4 and sixtieths at +6.
    // The active-player gates are halfwords at 0xA5FBC + p*0x84.
    for (int p = 0; p < 4; ++p) {
        MEM_H(0xA5FBC + p * 0x84, base) = 1;
        MEM_H(0x9876E + p * 8, base) = 58;
        MEM_H(0x98856 + p * 8, base) = 58;
    }
    set_enabled(Cheat::InfiniteTime, true);
    set_enabled(Cheat::FreezeLapTimers, true);
    for (int tick = 0; tick < 120; ++tick) cheat_test_timer(rdram, &ctx);
    require(MEM_H(0xCE76E, base) == 40, "countdown advanced while frozen");
    for (int p = 0; p < 4; ++p) {
        require(MEM_H(0x9876E + p * 8, base) == 58 && MEM_H(0x9876C + p * 8, base) == 0,
                "total clock advanced or rolled over while frozen");
        require(MEM_H(0x98856 + p * 8, base) == 58 && MEM_H(0x98854 + p * 8, base) == 0,
                "current lap clock advanced or rolled over while frozen");
    }
    set_enabled(Cheat::InfiniteTime, false);
    set_enabled(Cheat::FreezeLapTimers, false);
    cheat_test_timer(rdram, &ctx);
    require(MEM_H(0xCE76E, base) == 39, "countdown did not resume");
    for (int p = 0; p < 4; ++p) {
        require(MEM_H(0x9876E + p * 8, base) == 0 && MEM_H(0x9876C + p * 8, base) == 1,
                "total clock failed to resume/roll over");
        require(MEM_H(0x98856 + p * 8, base) == 0 && MEM_H(0x98854 + p * 8, base) == 1,
                "lap clock failed to resume/roll over");
    }
    // Dispatcher pause gate at 0xCE808; timer body must preserve all RDRAM.
    MEM_H(0xCE808, base) = 1;
    const auto paused = memory;
    cheat_test_timer(rdram, &ctx);
    require(memory == paused, "paused timer changed RAM");
    MEM_H(0xCE808, base) = 0;
    MEM_H(0xCE76E, base) = 0;
    set_enabled(Cheat::InfiniteTime, true);
    cheat_test_timer(rdram, &ctx);
    require(MEM_H(0xCE76E, base) == 0, "expired countdown was resurrected");

    // Real selector functions, synthetic descriptor table with the measured
    // category layout. Selection must skip locked cars normally in both
    // directions, include all variants with the cheat, and preserve unlocks.
    // 0x13D490 holds 24 descriptors, stride 24 bytes, category halfword first
    // (see docs/CAR_DIFFERENCES.md). Cursor halfwords are at 0xCE7E8 + index*2;
    // this handler's selected-player index is the one-based halfword at CE6A6.
    // Availability is 8 halfwords from 0x985C0. 0xA4170 holds progress bits;
    // a sentinel proves the cheat does not award or erase saved unlock progress.
    constexpr int categories[] = {0, 4, 1, 2, 3, 5, 6, 7};
    for (int model = 0; model < 24; ++model)
        MEM_H(0x13D490 + model * 24, base) = categories[model / 3];
    MEM_H(0x985C0, base) = 1;
    MEM_H(0x985C8, base) = 1;
    MEM_H(0xA4170, base) = 0x1234; // Sentinel saved progress.
    for (int player = 1; player <= 4; ++player) {
        MEM_H(0xCE6A6, base) = player;
        const int cursor = 0xCE7E8 + (player - 1) * 2;
        MEM_H(cursor, base) = 5;
        func_8003F56C(rdram, &ctx);
        require(MEM_H(cursor, base) == 0, "stock right selector did not skip locked models");
        func_8003F40C(rdram, &ctx);
        require(MEM_H(cursor, base) == 5, "stock left selector did not skip locked models");
        set_enabled(Cheat::ExtraVehicles, true);
        for (int step = 1; step <= 24; ++step) {
            func_8003F56C(rdram, &ctx);
            require(MEM_H(cursor, base) == (5 + step) % 24, "cheat skipped a model going right");
        }
        for (int step = 1; step <= 24; ++step) {
            func_8003F40C(rdram, &ctx);
            require(MEM_H(cursor, base) == (29 - step) % 24, "cheat skipped a model going left");
        }
        set_enabled(Cheat::ExtraVehicles, false);
        func_8003F56C(rdram, &ctx);
        require(MEM_H(cursor, base) == 0, "disabling cheat did not restore selection gate");
    }
    for (int category = 0; category < 8; ++category)
        require(MEM_H(0x985C0 + category * 2, base) == (category == 0 || category == 4),
                "availability table was modified");
    require(MEM_H(0xA4170, base) == 0x1234, "saved progress was modified");
    std::cout << "PASS native cheats against generated USA-ROM timer and selectors\n";
}
