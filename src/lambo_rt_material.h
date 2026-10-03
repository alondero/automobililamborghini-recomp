#pragma once

#include <array>
#include <cmath>
#include <cstdint>

namespace lambo::rt {

enum PhysicalShadowRejection : uint32_t {
    RejectExtendedDraw = 1u << 0,
    RejectCombiner = 1u << 1,
    RejectOtherMode = 1u << 2,
    RejectDepthCompareMode = 1u << 3,
    RejectShaderModeMismatch = 1u << 4,
    RejectFogCycle = 1u << 5,
    RejectCoverageAlphaOrBlending = 1u << 6,
    RejectDepthBehavior = 1u << 7,
    RejectShaderFlags = 1u << 8,
    RejectFogColor = 1u << 9,
};

// Values come from a single admitted native GameCall and its selected shader.
// No texture, guest-memory or renderer-resource lifetime is hidden here.
struct ShadowMaterial {
    uint32_t otherL = 0, otherH = 0, shaderOtherL = 0, shaderOtherH = 0;
    uint32_t combineW0 = 0, combineW1 = 0, shaderFlags = 0;
    uint32_t alphaCompare = 0, zMode = 0, zSource = 0;
    bool extended = false, coverageAlpha = false, alphaBlend = false, forceBlend = false;
    bool zCompare = false, zUpdate = false, standardFog = false;
    std::array<float, 4> fog{};

    bool shaderMatches() const {
        // RSP initializes the unused low six OtherModeH bits to ones.
        return otherL == shaderOtherL && (otherH & ~63u) == (shaderOtherH & ~63u);
    }
};

inline uint32_t caster_material_rejection(const ShadowMaterial& material) {
    uint32_t rejection = 0;
    if (material.extended) rejection |= RejectExtendedDraw;
    if (!material.shaderMatches()) rejection |= RejectShaderModeMismatch;
    if (material.alphaCompare != 0 || material.coverageAlpha || material.alphaBlend || material.forceBlend)
        rejection |= RejectCoverageAlphaOrBlending;
    if (!material.zCompare || !material.zUpdate || material.zMode != 0 || material.zSource != 0)
        rejection |= RejectDepthBehavior;
    if ((material.shaderFlags & 1u) != 0) rejection |= RejectShaderFlags;
    // No native discard/coverage/alpha blend can punch a hole in these paths.
    // RGB combiners and fog are separate receiver-composition constraints.
    return rejection;
}

inline uint32_t receiver_material_rejection(const ShadowMaterial& material) {
    uint32_t rejection = caster_material_rejection(material);
    const bool measuredCombine =
        (material.combineW0 == 0xFC127FFFu && material.combineW1 == 0xFFFFF238u) ||
        (material.combineW0 == 0xFC26A004u && material.combineW1 == 0x1FFC93F8u) ||
        (material.combineW0 == 0xFC327FFFu && material.combineW1 == 0xFFFFF838u) ||
        (material.combineW0 == 0xFCFFFFFFu && material.combineW1 == 0xFFFE7838u);
    if (!measuredCombine) rejection |= RejectCombiner;
    if (material.otherL != 0xC8112078u && material.otherL != 0xC8112230u) rejection |= RejectOtherMode;
    if (((material.otherH >> 20) & 3u) != 1u) rejection |= RejectDepthCompareMode;
    if (!material.standardFog) rejection |= RejectFogCycle;
    if ((material.shaderFlags & ((1u << 29) | (3u << 30))) != 0) rejection |= RejectShaderFlags;
    for (float component : material.fog) {
        if (!std::isfinite(component) || component < 0 || component > 1) rejection |= RejectFogColor;
    }
    return rejection;
}

enum NonCasterShadowDraw : uint32_t {
    UnknownCasterRole = 0,
    NonCasterScreenRectangle = 1,
    NonCasterBackdrop = 2,
    NonCasterNativeHud = 3,
};

// Numeric projection types follow RT64::Projection::Type, checked at the seam.
// Addresses are HLE-copied identities, never pointers for a worker to follow.
struct ShadowProjection {
    uint32_t type = 0, physicalAddress = 0, aspectMode = 0, geometry = 0;
    bool rectangleShader = false;
};

// USA static orthographic HUD matrix, loaded by the native needle/arrow path.
// Layout, source/load and measurements: docs/rt-material-evidence.md.
inline constexpr uint32_t native_hud_projection_address = 0x000A2C40u;

inline uint32_t non_caster_reason(const ShadowProjection& projection, const ShadowMaterial& material) {
    if (material.extended || !material.shaderMatches() || material.zCompare || material.zUpdate)
        return UnknownCasterRole;
    if (projection.type == 3 && projection.rectangleShader) return NonCasterScreenRectangle;
    // Aspect BACKDROP is an explicit producer tag, not a transform heuristic.
    if (projection.type == 1 && projection.aspectMode == 3) return NonCasterBackdrop;
    if (projection.type == 2 && projection.physicalAddress == native_hud_projection_address &&
        (projection.geometry & ~0x800000u) == 0) return NonCasterNativeHud;
    return UnknownCasterRole;
}

} // namespace lambo::rt
