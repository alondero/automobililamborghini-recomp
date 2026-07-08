// Standalone spec for the widescreen HUD-geometry shift scale (issue #67).
//
// The scale is pure float math (no game/RDRAM/RT64 state), so it is unit-testable in
// isolation — compile and run directly with the host compiler, no ROM build needed:
//   gcc -I. tests/test_hud_shift_scale.c -lm -o test_hud_shift_scale && ./test_hud_shift_scale
//
// It locks the contract that makes the composite HUD stay together at any Expand-on
// output aspect: 0 shift at 4:3, exactly the measured 16:9 constants at 16:9, and
// proportional travel for wider outputs (matching the gEXSetRectAlign rect pins).

#include <assert.h>
#include <math.h>
#include <stdio.h>

#include "../src/lambo_hud_widescreen.h"

static int approx(float a, float b) { return fabsf(a - b) < 1e-4f; }

#define SRC (4.0f / 3.0f) /* game-native aspect */

// End-to-end: the scale the geometry actually uses, given a display aspect + hr_option's
// ext_percentage, is scale_for_aspect(effective_rect_aspect(...)).
static float end_to_end(float display, float ext_percentage) {
    return lambo_ws_hud_shift_scale_for_aspect(
        lambo_ws_hud_effective_rect_aspect(display, SRC, ext_percentage));
}

int main(void) {
    // --- pure scale-for-aspect contract ---
    // 4:3: rect pins don't travel, so geometry must not move either.
    assert(approx(lambo_ws_hud_shift_scale_for_aspect(SRC), 0.0f));
    // 16:9: scale is exactly 1 so the live-calibrated 530 / -533.33 / -1.09 constants
    // reproduce the #39/#43-shipped placement to measurement precision.
    assert(approx(lambo_ws_hud_shift_scale_for_aspect(16.0f / 9.0f), 1.0f));
    // 21:9-class: geometry tracks proportionally (2.25x the 16:9 shift).
    assert(approx(lambo_ws_hud_shift_scale_for_aspect(21.0f / 9.0f), 2.25f));
    // Defensive: a sub-4:3 aspect clamps to zero, never shifts the wrong way.
    assert(lambo_ws_hud_shift_scale_for_aspect(1.0f) == 0.0f);

    // --- Clamp16x9 ext-percentage (RT64 Manual mode), ext_target = 16/9 ---
    // At/below the clamp target the rects use full travel; wider outputs taper.
    assert(approx(lambo_ws_hud_clamp_ext_percentage(16.0f / 9.0f, SRC, 16.0f / 9.0f), 1.0f));
    assert(approx(lambo_ws_hud_clamp_ext_percentage(21.0f / 9.0f, SRC, 16.0f / 9.0f), 4.0f / 9.0f));
    // Narrower than source: no travel.
    assert(lambo_ws_hud_clamp_ext_percentage(SRC, SRC, 16.0f / 9.0f) == 0.0f);

    // --- the #67 regression case, end to end ---
    // 21:9 under Clamp16x9 (the SHIPPED DEFAULT on an ultrawide): the rects clamp to 16:9,
    // so the geometry must too -> effective aspect 16:9 -> scale exactly 1, NOT 2.25.
    assert(approx(end_to_end(21.0f / 9.0f, 4.0f / 9.0f), 1.0f));
    // 21:9 under Full: rects reach the real edges -> scale 2.25.
    assert(approx(end_to_end(21.0f / 9.0f, 1.0f), 2.25f));
    // Any output under Original: rects don't move -> geometry doesn't either.
    assert(approx(end_to_end(21.0f / 9.0f, 0.0f), 0.0f));
    // 16:9 under Clamp16x9 (shipped default at 16:9): unchanged, exactly 1.
    assert(approx(end_to_end(16.0f / 9.0f, 1.0f), 1.0f));

    printf("all HUD shift-scale assertions passed\n");
    return 0;
}
