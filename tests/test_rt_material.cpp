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
        // MSAA receivers use the pass's averaged-depth variant; HDR stays unvalidated.
        auto msaa = opaque;
        msaa.shaderFlags = 1u << 30; // RenderFlags::sampleCount = 1 (2x).
        require(receiver_material_rejection(msaa) == 0, "MSAA receiver rejected");
        auto hdr = opaque;
        hdr.shaderFlags = 1u << 29; // RenderFlags::usesHDR.
        require(receiver_material_rejection(hdr) & RejectShaderFlags, "HDR receiver admitted");
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
        // Measured world-builder cutout: coverage-times-alpha with opaque depth.
        ShadowMaterial worldCutout = opaque;
        worldCutout.otherL = worldCutout.shaderOtherL = 0xCB023038u;
        worldCutout.otherH = worldCutout.shaderOtherH = 0x19ACFFu;
        worldCutout.coverageAlpha = true;
        require(caster_material_rejection(worldCutout) == RejectCoverageAlphaOrBlending,
            "measured world cutout no longer isolated to its coverage rejection");
        require(world_cutout_exclusion(worldCutout) == NonCasterWorldCutout, "measured world cutout not excluded");
        for (unsigned field = 0; field < 7; ++field) {
            auto other = worldCutout;
            if (field == 0) other.otherL = other.shaderOtherL = 0xCB023039u;
            if (field == 1) other.alphaBlend = true;
            if (field == 2) other.forceBlend = true;
            if (field == 3) other.alphaCompare = 1;
            if (field == 4) other.zUpdate = false;
            if (field == 5) other.extended = true;
            if (field == 6) other.coverageAlpha = false;
            require(world_cutout_exclusion(other) == UnknownCasterRole, "unmeasured world material excluded silently");
        }

        // Measured car glass: blended, translucent depth mode, compare without update.
        ShadowMaterial glass;
        glass.otherL = glass.shaderOtherL = 0x00504A50u;
        glass.otherH = glass.shaderOtherH = 0x8ACFFu;
        glass.combineW0 = 0xFC121824u;
        glass.combineW1 = 0xFF33FFFFu;
        glass.alphaBlend = glass.forceBlend = glass.zCompare = true;
        glass.zMode = 0x800u;
        require(caster_material_rejection(glass) == (RejectCoverageAlphaOrBlending | RejectDepthBehavior),
            "measured glass rejection changed");
        require(artistic_opaque_car_glass(glass), "measured car glass not admitted as an opaque caster");
        require(receiver_material_rejection(glass) != 0, "glass became a receiver");
        for (unsigned field = 0; field < 7; ++field) {
            auto other = glass;
            if (field == 0) other.combineW1 = 0xFF33FFFEu;
            if (field == 1) other.otherL = other.shaderOtherL = 0x00504A51u;
            if (field == 2) other.zUpdate = true;
            if (field == 3) other.zMode = 0;
            if (field == 4) other.alphaCompare = 1;
            if (field == 5) other.shaderOtherL = 0;
            if (field == 6) other.extended = true;
            require(!artistic_opaque_car_glass(other), "unmeasured blended car material admitted as glass");
        }
        std::puts("PASS: independent caster/receiver material and authenticated screen/backdrop policy");
        return 0;
    }
    catch (const std::exception& error) {
        std::fprintf(stderr, "FAIL: %s\n", error.what());
        return 1;
    }
}
