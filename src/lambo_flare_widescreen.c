// Widescreen lens flare (issue #40, RT64 extended GBI).
//
// The sun lens flare (emitter func_80036854) is a chain of 10 translucent "ghost"
// texrects traced from the sun's projected screen position. Under ar_option Expand
// RT64 treats each small untagged texrect as 4:3 content: rt64_framebuffer_renderer.cpp
// gives a Rectangle-projection draw invRatioScale = 1/aspectRatioScale (squished into
// the central 4:3 band) unless the rect covers the whole scissor width or carries
// G_EX_ASPECT_STRETCH. The same invRatioScale also converts the texrect's own scissor
// (line 1679), so the game's (0,0,320,240) scissor collapses to the 4:3 band too — that
// is the crop reported in the issue, and why a bare full-width DL scissor did nothing
// (it never touched invRatioScale).
//
// gEXSetRectAspect(STRETCH) forces invRatioScale = 1.0 for the bracketed texrects. With
// the game's default (NONE) rect origins, convertViewportRect then pivots the horizontal
// mapping about the native centre (160): a ghost's offset-from-centre in game pixels is
// preserved into widened pixels, which is exactly where the FOV-widened 3D sun lands
// (skybox/frustum already widen per issue #3). The scissor widens to the full frame by
// the same math. So the flare tracks the sun across the whole widescreen frame, uncropped,
// with no coordinate arithmetic here. At 4:3 output invRatioScale is already 1.0, so the
// bracket degenerates to a no-op (same reasoning as the HUD rect pins).
//
// Bracketed around the ghost loop of func_80036854 via [[patches.hook]] (before_vram
// 0x800361BC / 0x80036A60): the setup commands before the loop and teardown after it stay
// on the default aspect. The flare draws before the HUD each frame and RDP::clearExtended
// resets the aspect per frame, so this bracket enables the extended GBI itself.

#include "recomp.h"
#include "rt64_extended_gbi.h"

#define LAMBO_DL_CURSOR 0x800A39CCu

static void emit_cmd(uint8_t* rdram, uint32_t w0, uint32_t w1) {
    gpr curp = (gpr)(int32_t)LAMBO_DL_CURSOR;
    gpr cur = MEM_W(0, curp);
    MEM_W(0, cur) = (int32_t)w0;
    MEM_W(4, cur) = (int32_t)w1;
    MEM_W(0, curp) = (int32_t)(cur + 8);
}

static void emit_rect_aspect(uint8_t* rdram, uint32_t aspect) {
    // Enable the extended GBI (idempotent); nothing else has enabled it this early in the
    // frame since the flare draws before the HUD.
    emit_cmd(rdram,
             PARAM(RT64_HOOK_OPCODE, 8, 24) | PARAM(RT64_HOOK_MAGIC_NUMBER, 24, 0),
             PARAM(RT64_HOOK_OP_ENABLE, 4, 28) | PARAM(RT64_EXTENDED_OPCODE, 8, 0));
    emit_cmd(rdram,
             PARAM(RT64_EXTENDED_OPCODE, 8, 24) | PARAM(G_EX_SETRECTASPECT_V1, 24, 0),
             PARAM(aspect, 2, 0));
}

void lambo_flare_ws_aspect_stretch(uint8_t* rdram) {
    emit_rect_aspect(rdram, G_EX_ASPECT_STRETCH);
}

void lambo_flare_ws_aspect_reset(uint8_t* rdram) {
    emit_rect_aspect(rdram, G_EX_ASPECT_AUTO);
}
