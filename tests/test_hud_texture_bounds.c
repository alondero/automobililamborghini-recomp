// Behavioral checks must also run in Release builds.
#ifdef NDEBUG
#undef NDEBUG
#endif
// Exercise the real bracket with the USA dial's commands, without a ROM.
#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include "../src/lambo_hud_widescreen.c"

uint32_t lambo_ws_get_hud_rect_aspect_bits(void) {
    union { float f; uint32_t u; } a = { .f = 4.0f / 3.0f };
    return a.u;
}

static void check(uint8_t* rdram, uint32_t source, uint32_t tile0,
                  uint32_t tile1, uint32_t bounds, int clamp) {
    MEM_W(0, (gpr)(int32_t)LAMBO_DL_CURSOR) = (int32_t)0x800BF400;
    lambo_ws_pin_right(rdram);
    emit_cmd(rdram, 0xFD180000, source);
    emit_cmd(rdram, 0xF5180000, 0x07000000); // load tile must stay unchanged
    gpr p = MEM_W(0, (gpr)(int32_t)LAMBO_DL_CURSOR);
    emit_cmd(rdram, tile0, tile1);
    emit_cmd(rdram, 0xF2000000, bounds);
    lambo_ws_pin_reset(rdram);
    assert((uint32_t)MEM_W(-4, p) == 0x07000000);
    assert((uint32_t)MEM_W(4, p) == (tile1 | (clamp ? 0x00080200u : 0)));
    assert((uint32_t)MEM_W(12, p) == bounds);
}

int main(void) {
    uint8_t* rdram = calloc(1, 0x800000);
    assert(rdram);
    check(rdram, 0x80160140, 0xF5181400, 0x0000C060, 0x0009C014, 1);
    check(rdram, 0x80160508, 0xF5180800, 0x00018040, 0x0003C0E0, 1);
    check(rdram, 0x00160140, 0xF5181400, 0x0000C060, 0x0009C014, 1);
    check(rdram, 0x00160508, 0xF5180800, 0x00018040, 0x0003C0E0, 1);
    // Same shape with another source, or changed geometry, must not match.
    check(rdram, 0x80160144, 0xF5181400, 0x0000C060, 0x0009C014, 0);
    check(rdram, 0x80160140, 0xF5181400, 0x0000C060, 0x0009C018, 0);
    check(rdram, 0x80160140, 0xF5181400, 0x0008C260, 0x0009C014, 1);
    free(rdram);
}
