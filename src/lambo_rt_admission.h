#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

#include "shared/rt64_sun_shadow.h"

#include "lambo_rt_material.h"
#include "lambo_rt_shadows.h"

namespace lambo::rt {

// Task-owned scene identity copied on the game producer (docs/rt-shadows.md).
// Workers receive these values; nothing here can read guest RAM.
struct ShadowTask {
    uint64_t epoch = 0;
    uint64_t sequence = 0;
    int circuit = -1;
    int phase = -1;
    int players = 0;
    int race_mode = -1;
    std::array<int16_t, 4> model_cursors{};
    bool emitters_complete = false;
    std::vector<TaskSunProbe::ObjectIdentity> objects;
};

// One presented RT64 game call, in draw order, reduced to the values the
// policy inspects. The adapter in lambo_rt_presented.cpp builds it from the
// matching Workload; indexed ranges refer to that Workload's faceIndices.
struct PresentedDraw {
    uint32_t call = 0;
    uint32_t projection_type = 0; // RT64::Projection::Type value.
    bool indexed = false; // Perspective/orthographic: addresses faceIndices.
    uint32_t first = 0;
    uint64_t wide_count = 0; // triangleCount * 3, before narrowing.
    bool face_range_valid = false;
    bool uniform_group = false; // Every index maps to one presented object group.
    uint32_t matrix_id = 0;
    ShadowProjection projection;
    ShadowMaterial material;
};

// A light record referenced by one presented vertex of an object group.
struct PresentedLight {
    uint32_t object_id = 0;
    float x = 0, y = 0, z = 0;
    bool directional = false; // kc, kl and kq are all zero.
};

struct ShadowSettings {
    float strength = 0; // 0 keeps a capture-only Workload from attenuating.
    float angular_radius = 0; // Radians, 0 through 5 degrees.
    uint32_t samples = 8;
    // Developer validation sweeps only: admit any in-range player model so
    // its scene can be measured before it joins validated_player_models.
    bool any_player_model = false;
};

// Player-one model selectors (0-23) whose scenes passed hard-shadow parity.
// Evidence: docs/rt-shadows.md#validation. Other models keep native shadows.
inline constexpr std::array<int16_t, 1> validated_player_models{{0}};

// Strength is framebuffer-space attenuation; the VI's gamma runs afterwards.
// The displayed native overlay core transmits about 0.51 on every captured
// circuit/mode, which a hard ray-traced run maps back to 0.243 before the VI.
// A fully occluded hard shadow therefore matches native darkness on screen.
// Measurement: docs/rt-material-evidence.md#native-overlay-contrast.
inline constexpr float native_overlay_strength = 0.757f;

enum class ShadowGate : uint32_t {
    Admitted = 0,
    UnsupportedScene,
    InvalidObjectIdentity,
    MissingLitCarKey,
    InvalidParameters,
};

struct AdmissionStats {
    ShadowGate gate = ShadowGate::UnsupportedScene;
    size_t admitted_faces = 0, rejected = 0, unclassified = 0, overlays = 0;
    size_t world_cutouts = 0, glass_casters = 0;
    std::array<size_t, 10> rejection_reasons{};
};

// Returns null when the scene/light gate fails. A non-null result can still
// be incomplete; only complete results may ever replace a native overlay.
std::shared_ptr<RT64::SunShadowWorkload> admit_sun_shadow(const ShadowTask& task,
    const std::vector<PresentedDraw>& draws, const std::vector<PresentedLight>& lights,
    const ShadowSettings& settings, AdmissionStats& stats);

} // namespace lambo::rt
