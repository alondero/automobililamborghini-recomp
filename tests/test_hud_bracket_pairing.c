// Regression spec for widescreen HUD bracket pairing (pit-stop flicker / savestate crash).
//
// The reset hooks in func_80050860 sit on recompiled branch labels, so game paths that
// branch over a bracketed jal (the pit-stop screen hides the dial/RANK/LAP draws this
// way) run a reset WITHOUT its pin. Unguarded, that walked a stale
// [bracket_start, cursor) range and shifted arbitrary scene G_MTX matrices (+530 units
// => per-frame flicker), and on a cold start (bracket_start still 0) walked straight out
// of RDRAM (savestate-load crash). This locks the guard: unpaired resets are no-ops,
// paired brackets keep shifting exactly as calibrated.
//
// Standalone, no ROM build needed (fake 8 MiB RDRAM):
//   gcc -Ilib/N64ModernRuntime/N64Recomp/include -Ilib/rt64/include \
//       tests/test_hud_bracket_pairing.c -lm -o test_hud_bracket_pairing && ./test_hud_bracket_pairing

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint8_t* rdram;

void lambo_ws_quad_text_begin(uint8_t* rdram);
void lambo_ws_quad_text_end(uint8_t* rdram);

// Pull in the real module; the test provides the renderer extern it links against.
#include "../src/lambo_hud_widescreen.c"

// 16:9 => shift scale is exactly the calibrated baseline (see test_hud_shift_scale.c).
uint32_t lambo_ws_get_hud_rect_aspect_bits(void) {
    union { float f; uint32_t u; } a;
    a.f = 16.0f / 9.0f;
    return a.u;
}

#define DL_BASE   0x800BF400u
/* MTX_A: a legitimate in-bracket matrix the walker must shift; if it does, we know the
 * bracket was correctly armed. MTX_SCENE: a matrix the walker must NOT shift; only ever
 * reachable when a reset runs without its pin (i.e. our regression case). */
#define MTX_A     0x800A6738u
#define MTX_SCENE 0x800A7000u

static void wr32(uint32_t addr, uint32_t v) { MEM_W(0, (gpr)(int32_t)addr) = (int32_t)v; }
static uint32_t rd32(uint32_t addr) { return (uint32_t)MEM_W(0, (gpr)(int32_t)addr); }

static void emit_gmtx(uint32_t mtx_vaddr) {
    uint32_t cur = rd32(LAMBO_DL_CURSOR);
    wr32(cur, 0x01020040u);
    wr32(cur + 4, mtx_vaddr & 0x00FFFFFFu);
    wr32(LAMBO_DL_CURSOR, cur + 8);
}

/* Mirrors the layout patch_load_mtx_dx walks: translate.x is element 12 of the 4x4
 * matrix, stored split as int16 integer-part and uint16 fraction-part at +24/+24+0x20. */
static void set_mtx_x(uint32_t mtx, int16_t ip) {
    MEM_H(24, (gpr)(int32_t)mtx) = ip;
    MEM_HU(24 + 0x20, (gpr)(int32_t)mtx) = 0;
}

static int16_t get_mtx_x(uint32_t mtx) { return (int16_t)MEM_H(24, (gpr)(int32_t)mtx); }

int main(void) {
    rdram = (uint8_t*)calloc(1, 0x800000);
    assert(rdram != NULL);
    wr32(LAMBO_DL_CURSOR, DL_BASE);

    // --- cold start: reset with no pin ever run (savestate cold-load shape) ---
    // bracket_start is still 0; unguarded this walked [0, cursor) out of RDRAM and
    // crashed. Must be a complete no-op: nothing emitted, cursor untouched.
    lambo_ws_pin_reset(rdram);
    assert(rd32(LAMBO_DL_CURSOR) == DL_BASE);
    lambo_ws_minimap_reset(rdram);
    assert(rd32(LAMBO_DL_CURSOR) == DL_BASE);

    // --- paired bracket still shifts the needle exactly as calibrated ---
    set_mtx_x(MTX_A, 100);
    lambo_ws_pin_right(rdram);
    emit_gmtx(MTX_A); /* the dial helper's needle matrix */
    uint32_t cur_before_reset = rd32(LAMBO_DL_CURSOR);
    lambo_ws_pin_reset(rdram);
    int32_t shift = (int32_t)(LAMBO_WS_NEEDLE_DX *
                              lambo_ws_hud_shift_scale_for_aspect(16.0f / 9.0f) * 65536.0f);
    assert(get_mtx_x(MTX_A) == (int16_t)(((100 << 16) + shift) >> 16));
    assert(rd32(LAMBO_DL_CURSOR) > cur_before_reset); /* pop + align reset were emitted */

    // --- the pit-stop path: scene draws, then a label-landing reset with NO pin ---
    // The scene matrix sits in the stale [bracket_start, cursor) span; unguarded the
    // walker shifted it (+530 => flicker). Must be untouched and nothing emitted.
    set_mtx_x(MTX_SCENE, 50);
    emit_gmtx(MTX_SCENE);
    uint32_t cur = rd32(LAMBO_DL_CURSOR);
    lambo_ws_pin_reset(rdram);
    assert(get_mtx_x(MTX_SCENE) == 50);
    assert(rd32(LAMBO_DL_CURSOR) == cur);
    lambo_ws_minimap_reset(rdram);   /* later label-landing resets in the same frame */
    lambo_ws_quad_panel_reset(rdram);
    assert(get_mtx_x(MTX_SCENE) == 50);
    assert(rd32(LAMBO_DL_CURSOR) == cur);

    // --- next legit bracket re-arms cleanly after the no-op resets ---
    set_mtx_x(MTX_A, 200);
    lambo_ws_pin_right(rdram);
    emit_gmtx(MTX_A);
    lambo_ws_pin_reset(rdram);
    assert(get_mtx_x(MTX_A) == (int16_t)(((200 << 16) + shift) >> 16));

    // --- quad-text section: pin_left arms, end disarms (no per-element pin_reset) ---
    lambo_ws_quad_text_begin(rdram);
    /* pin_left armed the bracket flag */
    lambo_ws_quad_text_end(rdram);  /* label-landing end disarms even if other paths branch to L_80052C00 */
    /* a subsequent unpaired reset must be a no-op (flag was cleared by end) */
    uint32_t cur_after_end = rd32(LAMBO_DL_CURSOR);
    lambo_ws_pin_reset(rdram);
    assert(rd32(LAMBO_DL_CURSOR) == cur_after_end);

    // --- legit quad_panel bracket arms, shifts, disarms (verifies the only path the
    //     guard permits on a real 3P frame; pre-fix this would shift the scene matrix) ---
    set_mtx_x(MTX_SCENE, 999);
    lambo_ws_quad_panel_pin(rdram);
    emit_gmtx(MTX_SCENE);
    lambo_ws_quad_panel_reset(rdram);
    int32_t quad_shift = (int32_t)(LAMBO_WS_QUAD_PANEL_DX *
                                   lambo_ws_hud_shift_scale_for_aspect(16.0f / 9.0f) * 65536.0f);
    assert(get_mtx_x(MTX_SCENE) == (int16_t)(((999 << 16) + quad_shift) >> 16));

    // --- 2P minimap: pin_2p arms via pin_left; the shared minimap_reset disarms ---
    set_mtx_x(MTX_A, 400);
    lambo_ws_minimap_pin_2p(rdram);
    emit_gmtx(MTX_A);
    lambo_ws_minimap_reset(rdram);
    int32_t arrow_shift = (int32_t)(LAMBO_WS_MINIMAP_ARROW_DX *
                                    lambo_ws_hud_shift_scale_for_aspect(16.0f / 9.0f) * 65536.0f);
    assert(get_mtx_x(MTX_A) == (int16_t)(((400 << 16) + arrow_shift) >> 16));

    printf("all HUD bracket-pairing assertions passed\n");
    return 0;
}
