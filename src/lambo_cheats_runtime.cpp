#include "lambo_cheats.h"
#include "recomp.h"

// USA-ROM game-thread seams only; no saved guest data or executable bytes are
// patched. See docs/cheats.md for addresses, widths and source evidence.
extern "C" void lambo_cheat_countdown(uint8_t*, recomp_context* ctx) {
    if (lambo::cheats::enabled(lambo::cheats::Cheat::InfiniteTime)) {
        // Before 0x80001558: replace the decremented seconds with the original
        // signed halfword in t8, before its sign extension and zero/warning tests.
        ctx->r11 = ctx->r24;
    }
}

extern "C" void lambo_cheat_lap_timer(uint8_t*, recomp_context* ctx) {
    if (lambo::cheats::enabled(lambo::cheats::Cheat::FreezeLapTimers)) {
        // Before 0x800015DC: cancel this clock's +2 subsecond ticks. The ROM
        // still owns rollover, lap resets and the four-player loop.
        ctx->r11 = ctx->r24;
    }
}

extern "C" int lambo_cheat_extra_vehicles() {
    // Used only after the selector loads a category's availability halfword.
    // Bypassing the branch preserves both earned availability and save data.
    return lambo::cheats::enabled(lambo::cheats::Cheat::ExtraVehicles);
}

extern "C" void lambo_cheat_current_lap_timer(uint8_t*, recomp_context* ctx) {
    if (lambo::cheats::enabled(lambo::cheats::Cheat::FreezeLapTimers)) {
        // The second clock bank at 0x80098850 tracks the current lap.
        // Before 0x80001754, t7 is the old subsecond halfword and t5 is old+2.
        ctx->r13 = ctx->r15;
    }
}
