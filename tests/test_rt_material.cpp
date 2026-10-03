#include "lambo_rt_material.h"

#include <cstdio>
#include <limits>
#include <stdexcept>

namespace {
void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
}

int main() {
    using namespace lambo::rt;
    try {
        ShadowMaterial opaque;
        opaque.otherL = opaque.shaderOtherL = 0xC8112078u;
        opaque.otherH = opaque.shaderOtherH = 0x18ACFFu;
        opaque.combineW0 = 0xFC127FFFu;
        opaque.combineW1 = 0xFFFFF238u;
        opaque.zCompare = opaque.zUpdate = opaque.standardFog = true;
        opaque.fog = {0.2f, 0.3f, 0.4f, 1.0f};
        require(caster_material_rejection(opaque) == 0 && receiver_material_rejection(opaque) == 0,
            "measured opaque fog material rejected");
        auto nativeOnly = opaque;
        nativeOnly.standardFog = false;
        nativeOnly.combineW0 = 0;
        nativeOnly.fog[0] = std::numeric_limits<float>::quiet_NaN();
        require(caster_material_rejection(nativeOnly) == 0 && receiver_material_rejection(nativeOnly) != 0,
            "receiver fog/RGB constraints incorrectly removed an opaque caster");
        auto cutout = opaque;
        cutout.coverageAlpha = true;
        require(caster_material_rejection(cutout) & RejectCoverageAlphaOrBlending,
            "alpha-dependent caster treated as solid");
        auto translucent = opaque;
        translucent.alphaBlend = translucent.forceBlend = true;
        translucent.zUpdate = false;
        require(caster_material_rejection(translucent) != 0, "blended car component treated as solid");
        auto staleShader = opaque;
        staleShader.shaderOtherL = 0;
        require(caster_material_rejection(staleShader) & RejectShaderModeMismatch,
            "mismatched native/shader material admitted");
        opaque.shaderOtherH &= ~63u;
        require(caster_material_rejection(opaque) == 0, "unused native mode bits changed admission");

        ShadowMaterial screen;
        ShadowProjection rectangle{3, 0, 0, 0, true};
        require(non_caster_reason(rectangle, screen) == NonCasterScreenRectangle,
            "screen rectangle not identified");
        require(non_caster_reason({4, 0, 0, 0, false}, screen) == UnknownCasterRole,
            "raw triangle classified as a screen rectangle");
        ShadowProjection sky{1, 0, 3, 0, false};
        require(non_caster_reason(sky, screen) == NonCasterBackdrop, "explicit backdrop not identified");
        sky.aspectMode = 0;
        require(non_caster_reason(sky, screen) == UnknownCasterRole, "untagged camera-only draw called sky");
        ShadowProjection hud{2, native_hud_projection_address, 0, 0x800000u, false};
        require(non_caster_reason(hud, screen) == NonCasterNativeHud, "native HUD projection not identified");
        hud.physicalAddress += 64;
        require(non_caster_reason(hud, screen) == UnknownCasterRole, "foreign orthographic projection excluded");
        for (unsigned field = 0; field < 4; ++field) {
            auto unsafe = screen;
            if (field == 0) unsafe.zCompare = true;
            if (field == 1) unsafe.zUpdate = true;
            if (field == 2) unsafe.extended = true;
            if (field == 3) unsafe.shaderOtherL = 1;
            require(non_caster_reason(rectangle, unsafe) == UnknownCasterRole,
                "unsupported screen draw bypassed role admission");
        }
        std::puts("PASS: independent caster/receiver material and authenticated screen/backdrop policy");
        return 0;
    }
    catch (const std::exception& error) {
        std::fprintf(stderr, "FAIL: %s\n", error.what());
        return 1;
    }
}
