#ifndef LAMBO_HUD_WIDESCREEN_H
#define LAMBO_HUD_WIDESCREEN_H

// Widescreen HUD-geometry shift scale (issue #67).
//
// The rect-based HUD elements pin to the widened output edges with gEXSetRectAlign, so
// they travel proportionally to the LIVE output aspect. The geometry-based elements
// (rev needle, minimap player arrow, minimap track outline) are moved in game/camera
// space instead, and their magnitudes were measured for the 16:9 case. To keep the
// composite HUD together at any Expand-on aspect (ultrawide, or 16:9 with hr_option
// Full), each measured constant is multiplied by this dimensionless scale, which tracks
// the rect-pin travel: 0 at 4:3, exactly 1 at 16:9, larger for wider outputs.
//
// Header-inlined so the host unit test (tests/test_hud_shift_scale.c) and the game build
// share one definition. `aspect` is the EFFECTIVE rect-pin aspect (see below), always
// >= 4/3 by construction; the clamp is only a guard.
static inline float lambo_ws_hud_shift_scale_for_aspect(float aspect) {
    // The rect pin's edge travel is 160*(aspect*3/4 - 1) game px (docs/HUD.md); dividing
    // by its 16:9 value (160/3) normalizes to 1 at 16:9 and collapses to aspect*2.25 - 3.
    float scale = aspect * 2.25f - 3.0f;
    return scale > 0.0f ? scale : 0.0f;
}

// The rect pins do NOT always travel to the real output edges: RT64's extended GBI moves
// them by `extAspectPercentage`, which depends on hr_option (rt64_workload_queue.cpp
// :159-183). So the geometry must scale off the aspect the rects EFFECTIVELY pin to, not
// the raw output aspect (that is the skybox's concern, issue #3). These two helpers mirror
// RT64's math so the game-space geometry tracks the rects in every hr_option, at any
// output aspect. `source` is the game-native aspect (4/3).

// Fraction of full-width travel the rects use in RT64 Manual mode (hr_option Clamp16x9,
// ext_target = 16/9): 1.0 once the output reaches the clamp target, tapering to 0 as the
// output narrows toward source. Mirrors rt64_workload_queue.cpp:165-168.
static inline float lambo_ws_hud_clamp_ext_percentage(float display, float source,
                                                      float ext_target) {
    float reduced_ext = ext_target - source;
    float reduced_display = display - source;
    if (reduced_ext <= 0.0f || reduced_display <= 0.0f) {
        return 0.0f;
    }
    float p = reduced_ext / reduced_display;
    return p < 1.0f ? p : 1.0f;
}

// The aspect the rects effectively pin to given how far they actually travel
// (ext_percentage): `source` at zero travel (rects centred), `display` at full travel.
// Feeding this into lambo_ws_hud_shift_scale_for_aspect makes the geometry match the
// rects exactly -- e.g. 21:9 output under Clamp16x9 (ext_percentage ~= 0.444) yields 16:9,
// so the geometry travels the 16:9 amount, same as the 16:9-clamped rects.
static inline float lambo_ws_hud_effective_rect_aspect(float display, float source,
                                                       float ext_percentage) {
    return source + (display - source) * ext_percentage;
}

#endif // LAMBO_HUD_WIDESCREEN_H
