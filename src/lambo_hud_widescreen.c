// Per-element widescreen HUD pinning (issue #2, RT64 extended GBI).
//
// Under `ar_option: Expand` RT64 renders untagged 2D centered in the 4:3 region, so the
// race HUD's edge-anchored elements (LAP block left; RANK block + speedometer right)
// float inboard of the widescreen edges. Peer ports (MM64 ui_patches.c, SK2
// race_hud_widescreen.c) bracket each element's draw with gEXSetRectAlign; this repo has
// no MIPS patch pipeline, so the brackets are injected as N64Recomp [[patches.hook]]
// text (lamborghini.us.toml) around the three 1P-race draw calls in the 2D dispatcher
// func_80050860, calling the natives below.
//
// The game's DL write cursor: 0x800A39CC (every 2D helper stores a command and advances
// it by 8; verified in the recompiled emitters). Origins pin against RT64's widened
// color image and travel is clamped by hr_option (default Clamp16x9); with a 4:3 output
// the math degenerates to the original coordinates, so no config gating is needed.

#include <stdlib.h>
#include <string.h>

#include "recomp.h"
#include "rt64_extended_gbi.h"

#define LAMBO_DL_CURSOR 0x800A39CCu
#define SCREEN_WIDTH_QP (320 * 4) /* gEXSetRectAlign offsets are quarter-pixels */

// From src/lambo_config.cpp: true when the rect pins are actively travelling
// (ar Expand + live output aspect > 4/3). Gates the game-space geometry shifts
// (needle, minimap arrow, outline) so they only fire when the rect pins they
// track are also firing. The rect-origin pins themselves degenerate to no-ops
// at 4:3 by construction, so no gate is needed for them.
int lambo_ws_hud_widescreen_active(void);

// From src/rt64_renderer.cpp: live output aspect as float bits, 4/3 floor for
// non-Expand / non-wide outputs. Used to scale the game-space shifts with the
// actual output ratio (issue #67), so the composite HUD stays glued together
// at any Expand-on aspect, not just the shipped 16:9 Clamp16x9 defaults.
extern uint32_t lambo_ws_get_output_aspect_bits(void);

static gpr lambo_ws_bracket_start;

static void emit_cmd(uint8_t* rdram, uint32_t w0, uint32_t w1) {
    gpr curp = (gpr)(int32_t)LAMBO_DL_CURSOR;
    gpr cur = MEM_W(0, curp);
    MEM_W(0, cur) = (int32_t)w0;
    MEM_W(4, cur) = (int32_t)w1;
    MEM_W(0, curp) = (int32_t)(cur + 8);
}

static void emit_rect_align(uint8_t* rdram, uint32_t origin, int32_t xoff) {
    // RT64 only honours extended opcodes after the enable hook; emit it every bracket
    // (idempotent) since nothing else in this port enables the extended GBI.
    emit_cmd(rdram,
             PARAM(RT64_HOOK_OPCODE, 8, 24) | PARAM(RT64_HOOK_MAGIC_NUMBER, 24, 0),
             PARAM(RT64_HOOK_OP_ENABLE, 4, 28) | PARAM(RT64_EXTENDED_OPCODE, 8, 0));
    emit_cmd(rdram,
             PARAM(RT64_EXTENDED_OPCODE, 8, 24) | PARAM(G_EX_SETRECTALIGN_V1, 24, 0),
             PARAM(origin, 12, 0) | PARAM(origin, 12, 12));
    emit_cmd(rdram,
             PARAM(xoff, 16, 16) | PARAM(0, 16, 0),
             PARAM(xoff, 16, 16) | PARAM(0, 16, 0));
}

static void pin(uint8_t* rdram, uint32_t origin, int32_t xoff) {
    emit_rect_align(rdram, origin, xoff);
    // The game's own scissor is untagged, so RT64 centers it on the 4:3 region and
    // clips the moved element at the 4:3 edge. Bracket with a full-width scissor
    // (ulx pinned LEFT at 0, lrx pinned RIGHT at 0 == the widened image's right edge),
    // restored on reset (verified live: without this, LAP/RANK/dial render cropped).
    emit_cmd(rdram,
             PARAM(RT64_EXTENDED_OPCODE, 8, 24) | PARAM(G_EX_PUSHSCISSOR_V1, 24, 0),
             0);
    emit_cmd(rdram,
             PARAM(RT64_EXTENDED_OPCODE, 8, 24) | PARAM(G_EX_SETSCISSOR_V1, 24, 0),
             PARAM(0, 2, 0) | PARAM(G_EX_ORIGIN_LEFT, 12, 2) | PARAM(G_EX_ORIGIN_RIGHT, 12, 14));
    emit_cmd(rdram,
             PARAM(0, 16, 16) | PARAM(0, 16, 0),
             PARAM(0, 16, 16) | PARAM(240 * 4, 16, 0));
    lambo_ws_bracket_start = MEM_W(0, (gpr)(int32_t)LAMBO_DL_CURSOR);
}

void lambo_ws_pin_left(uint8_t* rdram) {
    pin(rdram, G_EX_ORIGIN_LEFT, 0);
}

void lambo_ws_pin_right(uint8_t* rdram) {
    // Rebase x to the right edge: movedFromOrigin adds the full image width, so the
    // original 320-wide coordinate space is subtracted back out (peer-standard offset).
    pin(rdram, G_EX_ORIGIN_RIGHT, -SCREEN_WIDTH_QP);
}

// HUD geometry (needle, minimap arrow) renders through the game's 2D ortho section,
// which RT64 places on the 4:3-centered path (screenScale.x = 320/wideWidth when the
// draw's scissor∩viewport does not cover the widened framebuffer, see
// rt64_framebuffer_renderer.cpp:1493) — rect-align moves only texrects, so geometry
// that must track a pinned rect element is moved in GAME space instead: each bracket
// records the DL cursor and the reset walks the commands emitted in between, adding a
// translation to any LOAD matrix found. Both the needle and the minimap arrow build
// their matrices at 10 units per game pixel (the ortho convention of func_80075278),
// and a pinned rect travels (wideWidth - height*4/3)/2 screen px = 160/3 game px at
// 16:9, so the matrix-unit magnitude at 16:9 is 160/3 * 10 = 533.33 (live-calibrated
// 530 matches to measurement precision, PR #39). Scales linearly with the live output
// aspect (issue #67), so the composite stays glued at any Expand-on aspect.
static float live_aspect(void) {
    uint32_t bits = lambo_ws_get_output_aspect_bits();
    float a;
    memcpy(&a, &bits, sizeof(a));
    return a;
}

static float hud_shift_x_matrix_units(void) {
    if (!lambo_ws_hud_widescreen_active()) return 0.0f;
    float a = live_aspect();
    if (a <= 4.0f / 3.0f) return 0.0f;
    return 1200.0f * (a - 4.0f / 3.0f); /* 160/3 * 10 * (aspect-4/3) / (16/9-4/3) */
}

static void patch_load_mtx_dx(uint8_t* rdram, gpr start, gpr end, float dx_units) {
    if (!lambo_ws_hud_widescreen_active()) {
        return;
    }
    for (gpr p = start; p + 8 <= end; p += 8) {
        uint32_t w0 = (uint32_t)MEM_W(0, p);
        if (w0 == 0x01020040u) { /* G_MTX LOAD|MODELVIEW, len 0x40 */
            uint32_t seg = (uint32_t)MEM_W(4, p);
            gpr mtx = (gpr)(int32_t)(0x80000000u | seg);
            int32_t ip = (int16_t)MEM_H(24, mtx); /* translate.x = element 12 */
            uint32_t fp = (uint16_t)MEM_HU(24 + 0x20, mtx);
            int32_t fixed = (int32_t)(((uint32_t)ip << 16) | fp);
            fixed += (int32_t)(dx_units * 65536.0f);
            MEM_H(24, mtx) = (int16_t)(fixed >> 16);
            MEM_HU(24 + 0x20, mtx) = (uint16_t)(fixed & 0xFFFF);
        }
    }
}

void lambo_ws_pin_reset(uint8_t* rdram) {
    patch_load_mtx_dx(rdram, lambo_ws_bracket_start,
                      MEM_W(0, (gpr)(int32_t)LAMBO_DL_CURSOR), hud_shift_x_matrix_units());
    emit_cmd(rdram,
             PARAM(RT64_EXTENDED_OPCODE, 8, 24) | PARAM(G_EX_POPSCISSOR_V1, 24, 0),
             0);
    emit_rect_align(rdram, G_EX_ORIGIN_NONE, 0);
}

// Minimap composite (issue #41): the 1P dispatcher's jal to the overlay func_80054FFC
// (car dots + P1 label texrects, player arrow = two quads via pool LOAD matrices) is
// bracketed LEFT; the reset shifts the arrow matrices in game space to match. The
// track OUTLINE is separate: 3D geometry drawn by the per-frame builder func_8004384C
// through the race PERSPECTIVE projection (center-anchored under RT64 Expand's FOV
// widening), placed by a camera-space translate(-2.05, -2.4, 0) built at 0x80043C88 —
// a hook there rewrites the x argument in flight. Camera units -> screen px depends
// on that projection; -1.09 verified live 2026-07-05 (1600x900 Expand+Clamp16x9,
// arcade race: dots and P1 sit on the outline). Scales linearly with the live output
// aspect (issue #67). Env LAMBO_WS_MINIMAP_OUTLINE_DX (float, camera units, negative =
// further left) overrides for recalibration. The 2P outline lives on a separate
// per-viewport emitter (see docs/HUD.md "2P minimap"); this 1P hook is intentionally
// dormant in 2P.
#define LAMBO_WS_MINIMAP_OUTLINE_DX -1.09f /* live-calibrated 2026-07-05, PR #43 */

// The frame builder runs in modes whose dots are not pinned yet (#42: 2P split;
// demo race has no HUD). Key the 1P outline shift off the 1P bracket actually
// running: the builder draws before the dispatcher each frame, so it sees the
// previous frame's flag (one unshifted frame on race entry, decays within 3 frames
// after exit).
static int lambo_ws_minimap_1p_frames;

static float minimap_outline_dx_camera(void) {
    if (!lambo_ws_hud_widescreen_active()) return 0.0f;
    if (lambo_ws_minimap_1p_frames <= 0) return 0.0f;
    lambo_ws_minimap_1p_frames--;
    float a = live_aspect();
    if (a <= 4.0f / 3.0f) return 0.0f;
    /* base at 16/9; scales linearly to 0 at 4/3 and proportionally wider */
    return LAMBO_WS_MINIMAP_OUTLINE_DX * (9.0f / 4.0f) * (a - 4.0f / 3.0f);
}

void lambo_ws_minimap_pin(uint8_t* rdram) {
    lambo_ws_minimap_1p_frames = 3;
    lambo_ws_pin_left(rdram);
}

void lambo_ws_minimap_reset(uint8_t* rdram) {
    patch_load_mtx_dx(rdram, lambo_ws_bracket_start,
                      MEM_W(0, (gpr)(int32_t)LAMBO_DL_CURSOR),
                      -hud_shift_x_matrix_units()); /* arrow tracks dots, opposite sign */
    emit_cmd(rdram,
             PARAM(RT64_EXTENDED_OPCODE, 8, 24) | PARAM(G_EX_POPSCISSOR_V1, 24, 0),
             0);
    emit_rect_align(rdram, G_EX_ORIGIN_NONE, 0);
}

uint32_t lambo_ws_minimap_outline_x(uint32_t x_bits) {
    float dx = minimap_outline_dx_camera();
    if (dx == 0.0f) return x_bits; /* gate / counter / mode all said no */
    const char* env = getenv("LAMBO_WS_MINIMAP_OUTLINE_DX");
    if (env != NULL) {
        dx = (float)atof(env);
    }
    union { uint32_t u; float f; } x;
    x.u = x_bits;
    x.f += dx;
    return x.u;
}

// 2P minimap outline (issue #42 + #67). The 1P hook at `func_8004384C @ 0x80043C88`
// doesn't fire in 2P (verified by env-var iteration on the previous DX constant):
// the per-frame 3D builder takes a different code path in 2P. `func_800448DC` (the
// sibling function at runtime 0x80043CDC, after `func_8004384C` in ROM) is the
// 2P per-viewport builder -- it has the same shape (one `func_80075278` matrix-
// translate build followed by the `func_80045BDC` polyline emit) but its camera-space
// translate is built from $a1=0 (live-calibrated vs. 1P's hardcoded -2.05f). The
// constant is much larger in 2P because the per-viewport projection differs: each
// 2P viewport is half-height (top/bottom split), so the camera-space shift that
// projects to a given screen distance is halved -- but the offset from 4:3-center
// to 16:9-wide edge in 2P is the SAME screen distance as 1P, so the absolute
// shift in camera units is comparable. -50.0f is a first guess (live-calibrate via
// env var below). Live aspect scales proportionally; widescreen must be active.
#define LAMBO_WS_MINIMAP_OUTLINE_DX_2P -50.0f /* first guess; live-calibrate via env */

static float minimap_outline_dx_camera_2p(void) {
    if (!lambo_ws_hud_widescreen_active()) return 0.0f;
    float a = live_aspect();
    if (a <= 4.0f / 3.0f) return 0.0f;
    /* base at 16/9; scales linearly to 0 at 4/3 and proportionally wider */
    return LAMBO_WS_MINIMAP_OUTLINE_DX_2P * (9.0f / 4.0f) * (a - 4.0f / 3.0f);
}

uint32_t lambo_ws_minimap_outline_x_2p(uint32_t x_bits) {
    float dx = minimap_outline_dx_camera_2p();
    if (dx == 0.0f) return x_bits;
    const char* env = getenv("LAMBO_WS_MINIMAP_OUTLINE_DX_2P");
    if (env != NULL) {
        dx = (float)atof(env);
    }
    union { uint32_t u; float f; } x;
    x.u = x_bits;
    x.f += dx;
    return x.u;
}
