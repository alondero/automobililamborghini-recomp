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

#include "recomp.h"
#include "rt64_extended_gbi.h"
#include "lambo_hud_widescreen.h"

#define LAMBO_DL_CURSOR 0x800A39CCu
#define SCREEN_WIDTH_QP (320 * 4) /* gEXSetRectAlign offsets are quarter-pixels */

// Effective aspect the HUD rect pins travel to (raw float bits, floored to >= 4/3) from
// rt64_renderer.cpp. Honours hr_option (Full = real edges, Clamp16x9 = stops at 16:9,
// Original = no travel) and returns 4/3 for any non-Expand config, so the shift scale
// below is 0 (a no-op) whenever the rects themselves don't move -- no separate config
// gate needed. NOT the raw output aspect (that is the skybox's, #3): keying off that would
// over-translate the geometry past the clamped rects at non-Full ultrawide outputs.
extern uint32_t lambo_ws_get_hud_rect_aspect_bits(void);

// How far the rect pins travel at the LIVE effective HUD aspect, normalized so 16:9 == 1
// (issue #67; see lambo_hud_widescreen.h). Each geometry element's measured-16:9 shift is
// multiplied by this, so needle/arrow/outline track the rect pins at ANY Expand aspect and
// hr_option. 0 when the rects don't travel (4:3 / non-Expand / Original) degenerates the
// shifts to no-ops.
static float lambo_ws_get_hud_shift_scale(void) {
    union { uint32_t u; float f; } a;
    a.u = lambo_ws_get_hud_rect_aspect_bits();
    return lambo_ws_hud_shift_scale_for_aspect(a.f);
}

static gpr lambo_ws_bracket_start;

// The reset hooks sit on recompiled branch labels (the instruction after each bracketed
// jal), and N64Recomp runs label hook text on EVERY incoming path — including game paths
// that branch over the jal (e.g. the pit-stop screen hides the dial/RANK/LAP draws by
// jumping straight to the post-jal label). An unpaired reset would pop a scissor that was
// never pushed and run the matrix walker over a stale [bracket_start, cursor) range,
// shifting arbitrary scene matrices (issue: pit-stop flicker; cold-start value 0 even
// walks out of RDRAM and crashes). Every pin arms this flag; every reset no-ops unless
// armed. Same trap the quad-text section documents below.
static int s_bracket_open;

static void emit_cmd(uint8_t* rdram, uint32_t w0, uint32_t w1) {
    gpr curp = (gpr)(int32_t)LAMBO_DL_CURSOR;
    gpr cur = MEM_W(0, curp);
    MEM_W(0, cur) = (int32_t)w0;
    MEM_W(4, cur) = (int32_t)w1;
    MEM_W(0, curp) = (int32_t)(cur + 8);
}

static void emit_rect_align2(uint8_t* rdram, uint32_t lorigin, int32_t lxoff,
                             uint32_t rorigin, int32_t rxoff) {
    // RT64 only honours extended opcodes after the enable hook; emit it every bracket
    // (idempotent) since nothing else in this port enables the extended GBI.
    emit_cmd(rdram,
             PARAM(RT64_HOOK_OPCODE, 8, 24) | PARAM(RT64_HOOK_MAGIC_NUMBER, 24, 0),
             PARAM(RT64_HOOK_OP_ENABLE, 4, 28) | PARAM(RT64_EXTENDED_OPCODE, 8, 0));
    emit_cmd(rdram,
             PARAM(RT64_EXTENDED_OPCODE, 8, 24) | PARAM(G_EX_SETRECTALIGN_V1, 24, 0),
             PARAM(lorigin, 12, 0) | PARAM(rorigin, 12, 12));
    emit_cmd(rdram,
             PARAM(lxoff, 16, 16) | PARAM(0, 16, 0),
             PARAM(rxoff, 16, 16) | PARAM(0, 16, 0));
}

static void emit_rect_align(uint8_t* rdram, uint32_t origin, int32_t xoff) {
    emit_rect_align2(rdram, origin, xoff, origin, xoff);
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
    s_bracket_open = 1;
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
// the shipped 16:9 Clamp16x9 defaults, so the magnitude is ~533 units either way.
//
// These are the 16:9 magnitudes; lambo_ws_get_hud_shift_scale() scales them for other
// output aspects (issue #67), so at 16:9 they reproduce the #39/#43-shipped placement
// exactly and at wider/narrower Expand aspects they track the rect pins.
// Needle: calibrated live against the RIGHT-pinned dial at 16:9 (2026-07-05: 530
// centers it on the dial hub — matching the analytic 533 to measurement precision).
#define LAMBO_WS_NEEDLE_DX 530.0f
#define LAMBO_WS_MINIMAP_ARROW_DX (-1600.0f / 3.0f) /* -53.33 game px * 10 units/px */

static void patch_load_mtx_dx(uint8_t* rdram, gpr start, gpr end, float dx_units) {
    float scale = lambo_ws_get_hud_shift_scale();
    if (scale <= 0.0f) {
        return; /* 4:3 / non-Expand output: the rect pins don't travel either */
    }
    dx_units *= scale;
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
    if (!s_bracket_open) {
        return; /* label-landing path that skipped the pin (e.g. pit stop) */
    }
    s_bracket_open = 0;
    patch_load_mtx_dx(rdram, lambo_ws_bracket_start,
                      MEM_W(0, (gpr)(int32_t)LAMBO_DL_CURSOR), LAMBO_WS_NEEDLE_DX);
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
// arcade race: dots and P1 sit on the outline) — the 16:9 magnitude, scaled by
// lambo_ws_get_hud_shift_scale() for other aspects (issue #67). Env
// LAMBO_WS_MINIMAP_OUTLINE_DX (float, camera units, negative = further left) sets the
// 16:9 base for recalibration; it is scaled the same way.
#define LAMBO_WS_MINIMAP_OUTLINE_DX -1.09f

// The frame builder also runs in modes whose dots are not pinned yet (#42: 2P split;
// demo race has no HUD). Key the outline shift off the 1P bracket actually running:
// the builder draws before the dispatcher each frame, so it sees the previous frame's
// flag (one unshifted frame on race entry, decays within 3 frames after exit).
static int lambo_ws_minimap_1p_frames;

void lambo_ws_minimap_pin(uint8_t* rdram) {
    lambo_ws_minimap_1p_frames = 3;
    lambo_ws_pin_left(rdram);
}

void lambo_ws_minimap_reset(uint8_t* rdram) {
    if (!s_bracket_open) {
        return; /* label-landing path that skipped the pin (e.g. pit stop) */
    }
    s_bracket_open = 0;
    patch_load_mtx_dx(rdram, lambo_ws_bracket_start,
                      MEM_W(0, (gpr)(int32_t)LAMBO_DL_CURSOR),
                      LAMBO_WS_MINIMAP_ARROW_DX);
    emit_cmd(rdram,
             PARAM(RT64_EXTENDED_OPCODE, 8, 24) | PARAM(G_EX_POPSCISSOR_V1, 24, 0),
             0);
    emit_rect_align(rdram, G_EX_ORIGIN_NONE, 0);
}

uint32_t lambo_ws_minimap_outline_x(uint32_t x_bits) {
    if (lambo_ws_minimap_1p_frames <= 0) {
        return x_bits;
    }
    float scale = lambo_ws_get_hud_shift_scale();
    if (scale <= 0.0f) {
        return x_bits; /* 4:3 / non-Expand output: leave the outline where the game drew it */
    }
    lambo_ws_minimap_1p_frames--;
    float dx = LAMBO_WS_MINIMAP_OUTLINE_DX;
    const char* env = getenv("LAMBO_WS_MINIMAP_OUTLINE_DX");
    if (env != NULL) {
        dx = (float)atof(env);
    }
    dx *= scale;
    union { uint32_t u; float f; } x;
    x.u = x_bits;
    x.f += dx;
    return x.u;
}

// 2P split minimap (issue #56): the whole composite (dots + arrow + track outline) is
// 2D here, so one LEFT bracket per half pins it -- no 3D outline shift like 1P (see
// docs/HUD.md). Distinct from lambo_ws_minimap_pin only to skip the 1P outline frame-gate.
void lambo_ws_minimap_pin_2p(uint8_t* rdram) {
    lambo_ws_pin_left(rdram);
}

// Quad-split HUD text pinning (issue #78). func_80050860's quad section (L_800517A8)
// draws each player's RANK/speed/lap-notify text and tag texrects at fixed 4:3-space
// columns (left ~0x14-0x46, right ~0xEB-0x11D) plus per-quadrant-centred message glyphs
// (x=0x50/0xF0) and, in 3P, the map panel centred at (240,180). With the quadrant 3D
// views widened (G_EX_ORIGIN_WIDE below), the text must follow its quarter: left column
// pins LEFT, right column RIGHT, quadrant-centred elements pin to fractional origins
// (RT64's computeOrigin is generic: origin/0x400 of the output width) with the matching
// quarter-pixel rebase, mirroring how pin_right rebases by -SCREEN_WIDTH_QP.
//
// The section's control flow merges at branch labels, and hook text at a label runs on
// EVERY incoming path (N64Recomp emits the label before the hook text), so per-element
// pin/reset scissor pairs cannot stay balanced across the 4P-only branches. Instead ONE
// full-width scissor spans the whole section — pushed at entry, popped at the common
// exit label L_80052C00 — and the hooks in between only switch the sticky rect-align
// state, which is idempotent and safe at merge labels. The pop is flag-guarded because
// L_80052C00 is shared with the 1P/2P dispatcher paths.

#define G_EX_ORIGIN_QUARTER_LEFT (G_EX_ORIGIN_CENTER / 2)
#define G_EX_ORIGIN_QUARTER_RIGHT ((G_EX_ORIGIN_CENTER + G_EX_ORIGIN_RIGHT) / 2)

static int s_quad_text_scissor_open;

void lambo_ws_quad_text_begin(uint8_t* rdram) {
    lambo_ws_pin_left(rdram); /* wide scissor push + LEFT for the first RANK draw */
    s_quad_text_scissor_open = 1;
}

void lambo_ws_quad_left(uint8_t* rdram) {
    emit_rect_align(rdram, G_EX_ORIGIN_LEFT, 0);
}

void lambo_ws_quad_right(uint8_t* rdram) {
    emit_rect_align(rdram, G_EX_ORIGIN_RIGHT, -SCREEN_WIDTH_QP);
}

void lambo_ws_quad_quarter_left(uint8_t* rdram) {
    emit_rect_align(rdram, G_EX_ORIGIN_QUARTER_LEFT, -SCREEN_WIDTH_QP / 4);
}

void lambo_ws_quad_quarter_right(uint8_t* rdram) {
    emit_rect_align(rdram, G_EX_ORIGIN_QUARTER_RIGHT, -SCREEN_WIDTH_QP * 3 / 4);
}

void lambo_ws_quad_none(uint8_t* rdram) {
    emit_rect_align(rdram, G_EX_ORIGIN_NONE, 0);
}

void lambo_ws_quad_text_end(uint8_t* rdram) {
    if (!s_quad_text_scissor_open) {
        return; /* reached via the 1P/2P paths that share L_80052C00 */
    }
    s_quad_text_scissor_open = 0;
    s_bracket_open = 0; /* quad_text_begin armed it via pin_left; no pin_reset runs here */
    emit_cmd(rdram,
             PARAM(RT64_EXTENDED_OPCODE, 8, 24) | PARAM(G_EX_POPSCISSOR_V1, 24, 0),
             0);
    emit_rect_align(rdram, G_EX_ORIGIN_NONE, 0);
}

// 3P map-panel BACKGROUND: func_800030F8's tail emits a static sub-DL (0x8011F1C0,
// black fillrect (160,120)-(319,239)) that blanks the unused 4th quadrant. Untagged it
// covers only the 4:3 band's right half, leaving a frame-clear-coloured gap out to the
// widened quarter's edge. A mixed-origin align STRETCHES it: left edge pinned to the
// output centre (the quadrant divide), right edge to the output right edge, each with
// the rebase for its game-space coordinate (160 -> CENTER, 320 -> RIGHT). Rects need
// the wide scissor or RT64 crops the moved edge at the 4:3 band; the reset is
// flag-guarded because the hook after the emission sits on a merge label.
static int s_quad_panel_bg_open;

void lambo_ws_quad_panel_bg_stretch(uint8_t* rdram) {
    emit_rect_align2(rdram, G_EX_ORIGIN_CENTER, -SCREEN_WIDTH_QP / 2,
                     G_EX_ORIGIN_RIGHT, -SCREEN_WIDTH_QP);
    emit_cmd(rdram,
             PARAM(RT64_EXTENDED_OPCODE, 8, 24) | PARAM(G_EX_PUSHSCISSOR_V1, 24, 0),
             0);
    emit_cmd(rdram,
             PARAM(RT64_EXTENDED_OPCODE, 8, 24) | PARAM(G_EX_SETSCISSOR_V1, 24, 0),
             PARAM(0, 2, 0) | PARAM(G_EX_ORIGIN_LEFT, 12, 2) | PARAM(G_EX_ORIGIN_RIGHT, 12, 14));
    emit_cmd(rdram,
             PARAM(0, 16, 16) | PARAM(0, 16, 0),
             PARAM(0, 16, 16) | PARAM(240 * 4, 16, 0));
    s_quad_panel_bg_open = 1;
}

void lambo_ws_quad_panel_bg_reset(uint8_t* rdram) {
    if (!s_quad_panel_bg_open) {
        return; /* merge-label hook: paths that skipped the stretch */
    }
    s_quad_panel_bg_open = 0;
    emit_cmd(rdram,
             PARAM(RT64_EXTENDED_OPCODE, 8, 24) | PARAM(G_EX_POPSCISSOR_V1, 24, 0),
             0);
    emit_rect_align(rdram, G_EX_ORIGIN_NONE, 0);
}

// 3P map panel: the minimap overlay func_80054FFC composite is centred at (240,180) =
// the 4th quadrant's 4:3 centre, so its rects pin to the 75% origin; the player arrow
// is geometry (pool LOAD matrices, as in 1P/2P), shifted in game space by the walker.
// A 75%-origin rect travels half as far as an edge pin: 160/3 / 2 game px * 10 units/px
// at 16:9, rightward, scaled for other aspects like every other geometry shift.
#define LAMBO_WS_QUAD_PANEL_DX (800.0f / 3.0f)

void lambo_ws_quad_panel_pin(uint8_t* rdram) {
    emit_rect_align(rdram, G_EX_ORIGIN_QUARTER_RIGHT, -SCREEN_WIDTH_QP * 3 / 4);
    lambo_ws_bracket_start = MEM_W(0, (gpr)(int32_t)LAMBO_DL_CURSOR);
    s_bracket_open = 1;
}

void lambo_ws_quad_panel_reset(uint8_t* rdram) {
    if (!s_bracket_open) {
        return; /* label-landing path that skipped the pin */
    }
    s_bracket_open = 0;
    patch_load_mtx_dx(rdram, lambo_ws_bracket_start,
                      MEM_W(0, (gpr)(int32_t)LAMBO_DL_CURSOR),
                      LAMBO_WS_QUAD_PANEL_DX);
    emit_rect_align(rdram, G_EX_ORIGIN_NONE, 0);
}

// 3P/4P split-screen widescreen (issue #42 follow-up). Unlike the rect-align pins above
// (which move 2D texrects), this tags the 3D quadrant VIEWPORTS: each player's view is a
// half-width quadrant that RT64 would otherwise squeeze to the 4:3 centre (pillarbox).
// G_EX_ORIGIN_WIDE (patches/0008) makes RT64 render a tagged viewport wide -- filling its
// output quarter with a Hor+-widened FOV -- so the bracket wraps the race renderer's
// quadrant-scene DL call. Self-gated on the player count so 1P (full screen) and 2P
// (top/bottom, already full width) emit nothing.
#define LAMBO_PLAYERS_ADDR 0x800CE6A4u

static int lambo_ws_is_quad_split(uint8_t* rdram) {
    return (int16_t)MEM_H(0, (gpr)(int32_t)LAMBO_PLAYERS_ADDR) >= 3;
}

static void emit_viewport_align(uint8_t* rdram, uint32_t origin) {
    emit_cmd(rdram,
             PARAM(RT64_HOOK_OPCODE, 8, 24) | PARAM(RT64_HOOK_MAGIC_NUMBER, 24, 0),
             PARAM(RT64_HOOK_OP_ENABLE, 4, 28) | PARAM(RT64_EXTENDED_OPCODE, 8, 0));
    emit_cmd(rdram,
             PARAM(RT64_EXTENDED_OPCODE, 8, 24) | PARAM(G_EX_SETVIEWPORTALIGN_V1, 24, 0),
             PARAM(origin, 12, 0));
    emit_cmd(rdram,
             PARAM(0, 16, 16) | PARAM(0, 16, 0),
             0);
}

void lambo_ws_split_wide_begin(uint8_t* rdram) {
    if (lambo_ws_is_quad_split(rdram)) {
        emit_viewport_align(rdram, G_EX_ORIGIN_WIDE);
    }
}

void lambo_ws_split_wide_end(uint8_t* rdram) {
    if (lambo_ws_is_quad_split(rdram)) {
        emit_viewport_align(rdram, G_EX_ORIGIN_NONE);
    }
}
