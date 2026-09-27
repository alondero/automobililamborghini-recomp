#include "lambo_menu_stick.h"

#include <cmath>

// Declared in lambo_config.h. Forward-declared so this hook helper does not pull
// the config header's <filesystem>/<string>/ultramodern/json dependencies into
// every translation unit (and the isolated host test) that includes it.
namespace lambo::config {
double menu_stick_sensitivity();
}

// Issue #238 -- opt-in pre-race menu stick sensitivity.
//
// The ROM's menu input routine func_800427D4 (runtime 0x80041BD4) reads the
// parsed controller pad at 0x800A39E0 + channel*6 (OSContPad shape: buttons +0,
// stick_x +2, stick_y +3) and turns the stick into menu movement two ways:
//   * a cursor-velocity path (deadzone 7, clamped to +/-48 on X and +/-32 on Y,
//     advancing the cursor by velocity>>3 per frame), fed by the stick_x load at
//     0x800424FC and the stick_y load at 0x800426E0; and
//   * a discrete +/-51 direction threshold (slti 0x33 / -0x32) on stick_x at
//     0x800428D0 and 0x80042948 that synthesizes D-right/D-left bits.
//
// The host hooks rewrite the register holding each loaded byte just before its
// consumer runs, so this native only scales one signed stick byte. It runs on the
// game thread inside that routine, and no other consumer (race steering included)
// reads the scaled value. The eventual replacement is a source-level menu input
// interface; none exists yet.
//
// 1.0 is a no-op, so a default build leaves every register untouched.
extern "C" int32_t lambo_menu_stick_scale(int32_t value) noexcept {
    const double scale = lambo::config::menu_stick_sensitivity();
    if (!(scale > 1.0)) return value;
    long scaled = std::lround(static_cast<double>(value) * scale);
    if (scaled > 127) scaled = 127;
    if (scaled < -127) scaled = -127;
    return static_cast<int32_t>(scaled);
}
