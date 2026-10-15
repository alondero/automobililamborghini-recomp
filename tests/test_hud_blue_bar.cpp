#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <vector>

#include "recomp.h"

extern "C" void lambo_hud_remove_blue_bar(uint8_t*, recomp_context*);
namespace lambo::config {
static bool hide_bar = false;
bool remove_blue_hud_bar() { return hide_bar; }
}

static void require(bool condition, const char* message) {
    if (!condition) { std::cerr << message << '\n'; std::exit(1); }
}

int main() {
    std::vector<uint8_t> memory(8 * 1024 * 1024);
    uint8_t* rdram = memory.data();
    recomp_context context{};
    context.r11 = static_cast<int32_t>(0x800BF400u);
    const gpr command = context.r11;
    const gpr cursor = static_cast<int32_t>(0x800A39CCu);
    MEM_W(0, cursor) = static_cast<int32_t>(command + 8);
    MEM_W(-4, command) = 0x12345678;
    MEM_W(8, command) = 0x23456789;
    auto emit = [&] {
        MEM_W(0, command) = 0x06000000;
        MEM_W(4, command) = static_cast<int32_t>(0x80167170u);
    };
    emit();
    lambo_hud_remove_blue_bar(rdram, &context);
    require(MEM_W(0, command) == 0x06000000 &&
            static_cast<uint32_t>(MEM_W(4, command)) == 0x80167170u,
            "default must keep the original strip call");
    lambo::config::hide_bar = true;
    lambo_hud_remove_blue_bar(rdram, &context);
    require(MEM_W(0, command) == 0 && MEM_W(4, command) == 0,
            "enabled must replace the strip call with G_SPNOOP");
    require(MEM_W(0, cursor) == static_cast<int32_t>(command + 8) &&
            MEM_W(-4, command) == 0x12345678 && MEM_W(8, command) == 0x23456789,
            "cursor and adjacent HUD commands must stay intact");
    emit();
    MEM_W(4, command) = static_cast<int32_t>(0x80166FF0u);
    lambo_hud_remove_blue_bar(rdram, &context);
    require(MEM_W(0, command) == 0x06000000 &&
            static_cast<uint32_t>(MEM_W(4, command)) == 0x80166FF0u,
            "other display lists must stay intact");
    emit();
    MEM_W(0, command) = 0x01030040;
    lambo_hud_remove_blue_bar(rdram, &context);
    require(MEM_W(0, command) == 0x01030040, "other opcodes must stay intact");
    lambo::config::hide_bar = false;
    emit();
    lambo_hud_remove_blue_bar(rdram, &context);
    require(MEM_W(0, command) == 0x06000000, "disabling must restore the next frame's strip");
}
