// SPDX-License-Identifier: GPL-3.0-or-later

#include <ultramodern/ultramodern.hpp>

#include "recomp.h"

// func_80079720 (runtime 0x80078B20) is the sound-player status getter. Its
// native override yields at the ROM's busy-spin boundary so the cooperative
// runtime can dispatch the stop handler, then performs the original load.
extern "C" void func_80079720(uint8_t* rdram, recomp_context* ctx) {
    ultramodern::deliver_external_and_yield(rdram);
    ctx->r2 = MEM_W(ctx->r4, 0X2C);
}
