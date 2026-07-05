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

#include "recomp.h"
#include "rt64_extended_gbi.h"

#define LAMBO_DL_CURSOR 0x800A39CCu
#define SCREEN_WIDTH_QP (320 * 4) /* gEXSetRectAlign offsets are quarter-pixels */

// From src/lambo_config.cpp: true when the shipped widescreen defaults are active
// (ar Expand + hr Clamp16x9). Gates only the needle matrix nudge; the rect-origin
// pins degenerate to no-ops at 4:3 by construction.
int lambo_ws_hud_widescreen_active(void);

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

// The speedo NEEDLE is triangle geometry: func_80056318 (the dial drawer, inside the
// RIGHT rect bracket) builds one G_MTX LOAD|MODELVIEW + rotation-MUL chain for the
// needle quads. Rect-align moves only texrects, and RT64 renders this geometry on the
// stretched-wide path (screen x = game x * wideWidth/320), so the needle is moved in
// GAME space instead: each bracket records the DL cursor and the reset walks the
// commands emitted in between, adding a translation to any LOAD matrix found (only the
// dial bracket contains one). The minimap overlay func_80054FFC also builds matrices
// but is deliberately not bracketed — its arrow/dots must stay with the centered map.
//
// Shift in the needle's model units, calibrated live against the RIGHT-pinned dial at
// 16:9 (paused-race measurement 2026-07-05: pivot trans.x=1000 units; +300 units moved
// the needle +98px at 1600-wide output; 530 centers it on the dial hub). Only valid
// for the shipped Expand + Clamp16x9 defaults — gated above for anything else.
#define LAMBO_WS_NEEDLE_DX 530.0f

static void lambo_ws_patch_needle_mtx(uint8_t* rdram, gpr start, gpr end) {
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
            fixed += (int32_t)(LAMBO_WS_NEEDLE_DX * 65536.0f);
            MEM_H(24, mtx) = (int16_t)(fixed >> 16);
            MEM_HU(24 + 0x20, mtx) = (uint16_t)(fixed & 0xFFFF);
        }
    }
}

void lambo_ws_pin_reset(uint8_t* rdram) {
    lambo_ws_patch_needle_mtx(rdram, lambo_ws_bracket_start,
                              MEM_W(0, (gpr)(int32_t)LAMBO_DL_CURSOR));
    emit_cmd(rdram,
             PARAM(RT64_EXTENDED_OPCODE, 8, 24) | PARAM(G_EX_POPSCISSOR_V1, 24, 0),
             0);
    emit_rect_align(rdram, G_EX_ORIGIN_NONE, 0);
}
