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
    // Receiver only: phase 1 attenuates native output and never relights, so a
    // vertex-lit surface would lose fill/ambient light too and double-darken its
    // light-averted faces. Lit surfaces still cast.
    RejectVertexLighting = 1u << 10,
};
inline constexpr uint32_t rejection_bit_count = 11;
static_assert((RejectVertexLighting >> (rejection_bit_count - 1)) == 1u, "rejection tally misses a bit");

// USA native car-shadow overlay material (render mode, combiner, geometry
// mode). The object identity check lives with the callers. Provenance:
// docs/rt-provenance.md.
inline bool native_overlay_material(uint32_t other_l, uint32_t combine_w0, uint32_t combine_w1,
        uint32_t geometry) {
    return other_l == 0xC8104A50u && (combine_w0 & 0xFFFFFFu) == 0x11FFFFu && combine_w1 == 0xFFFFF238u &&
        (geometry & ~0x800000u) == 0x12005u;
}

// F3DEX geometry-mode bit for per-vertex lighting.
inline constexpr uint32_t f3dex_geometry_lighting = 0x00020000u;

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
    // RenderFlags::usesHDR (bit 29) selects the 16-bit UNORM high-precision
    // target, which the receiver and composite follow; MSAA sample counts
    // (bits 30-31) use the receiver's averaged-depth variant.
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
    NonCasterWorldCutout = 4,
    NonCasterTrailDecal = 5,
};

// Measured world-builder cutouts (coverage-times-alpha, opaque depth) discard
// about a third of their interior samples, so they cannot be solid casters and
// no ray-hit coverage is proven. Excluding a caster only removes occlusion; the
// native game draws no scenery shadows. The caller restricts this to the world
// roles. Measurements: docs/rt-material-evidence.md#production-caster-policy.
inline uint32_t world_cutout_exclusion(const ShadowMaterial& material) {
    const bool measured = material.otherL == 0xCB023038u && material.shaderMatches() &&
        !material.extended && material.coverageAlpha && material.alphaCompare == 0 &&
        !material.alphaBlend && !material.forceBlend && material.zCompare && material.zUpdate &&
        material.zMode == 0 && material.zSource == 0;
    return measured ? NonCasterWorldCutout : UnknownCasterRole;
}

// Measured tyre-trail decals: untextured translucent quads the trail emitter
// lays on the road, plus its brief four-triangle decal before each pair. Each
// can span road segments, so it has no single transform group. Translucent
// draws never cast, so the exact material alone classifies them. Measurement:
// docs/rt-material-evidence.md#production-caster-policy.
inline uint32_t trail_decal_exclusion(const ShadowMaterial& material, uint32_t geometry) {
    struct Signature { uint32_t otherL, combineW1, geometry; };
    static constexpr Signature measured[] = {
        {0xC8104A50u, 0xFFFE7638u, 0x10205u}, // Laid trail quads.
        {0xC8104B50u, 0xFFFCF238u, 0x10005u}, // Brief decal before each pair.
    };
    const bool translucent = material.combineW0 == 0xFCFFFFFFu && material.shaderMatches() && !material.extended &&
        !material.coverageAlpha && material.alphaCompare == 0 && material.alphaBlend && material.forceBlend &&
        material.zCompare && !material.zUpdate && material.zMode == 0x800u && material.zSource == 0;
    for (const auto& signature : measured) {
        if (translucent && material.otherL == signature.otherL && material.combineW1 == signature.combineW1 &&
            (geometry & ~0x800000u) == signature.geometry) return NonCasterTrailDecal;
    }
    return UnknownCasterRole;
}

// Maintainer policy: the one measured blended car-glass layer casts as opaque
// tinted glass, so the car silhouette has no window-shaped gaps. This is an
// artistic simplification, not measured transmission; the glass never receives.
// The caller restricts this to physical-car descendants.
inline bool artistic_opaque_car_glass(const ShadowMaterial& material) {
    return material.otherL == 0x00504A50u && material.combineW0 == 0xFC121824u &&
        material.combineW1 == 0xFF33FFFFu && material.shaderMatches() && !material.extended &&
        material.alphaCompare == 0 && material.alphaBlend && material.zCompare && !material.zUpdate &&
        material.zMode == 0x800u && material.zSource == 0;
}

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
