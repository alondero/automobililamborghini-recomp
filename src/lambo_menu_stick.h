#ifndef LAMBO_MENU_STICK_H
#define LAMBO_MENU_STICK_H

#include <cstdint>

// Menu-navigation stick scaling for issue #238. Called from the recompiled
// func_800427D4 (whole-ROM funcs_4.c) at the four sites that read the parsed pad
// stick bytes, so the scale reaches pre-race menu navigation only -- never the
// race steering path. Returns `value` unchanged unless menu_stick_sensitivity()
// is greater than 1.0, so a default build is a no-op.
extern "C" int32_t lambo_menu_stick_scale(int32_t value) noexcept;

#endif // LAMBO_MENU_STICK_H
