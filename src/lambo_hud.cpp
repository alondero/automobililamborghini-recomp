#include "lambo_config.h"
#include "recomp.h"

// USA ROM HUD dispatcher func_80050860, immediately after each blue-strip
// G_DL emission (runtime 0x8004FE74 / 0x80051140). On the game thread, t3
// holds the just-written 8-byte command in word-swapped guest RAM. Replace
// only the call to the decorative strip at 0x80167170 with G_SPNOOP; retain
// the cursor and all neighbouring HUD commands. A mismatched command is
// left alone. Each frame emits a fresh call, so disabling restores it.
extern "C" void lambo_hud_remove_blue_bar(uint8_t* rdram, recomp_context* ctx) {
    if (!lambo::config::remove_blue_hud_bar()) return;
    const gpr command = ctx->r11;
    if (static_cast<uint32_t>(MEM_W(0, command)) != 0x06000000u ||
        static_cast<uint32_t>(MEM_W(4, command)) != 0x80167170u) return;
    MEM_W(0, command) = 0;
    MEM_W(4, command) = 0;
}
