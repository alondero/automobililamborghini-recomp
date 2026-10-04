#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
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
    // Developer validation sweeps only: admit any player race mode on any
    // circuit so the scene can be measured before it joins the validated
    // table below.
    bool validation_sweep = false;
};

// Every player-one model selector passed hard-shadow parity: 0 across the
// circuit/mode matrix, 1-23 on Circuit 1 time trial. docs/rt-shadows.md#validation.
inline constexpr int16_t last_player_model = 23;

// Race modes (0x800CE6B4) whose one-player scenes passed the same parity, as
// a bit mask of zero-based circuits: 0 = time trial and 2 = single race on all
// six; 1 = menu Arcade and 3 (unidentified) on Circuit 1 only, because warping
// into them always loads Circuit 1. Evidence: docs/rt-shadows.md#validation.
inline constexpr std::array<uint8_t, 4> validated_race_mode_circuits{{0x3F, 0x01, 0x3F, 0x01}};

inline bool validated_race_mode(int race_mode, int circuit) {
    return race_mode >= 0 && race_mode < int(validated_race_mode_circuits.size()) && circuit >= 0 && circuit < 6 &&
        (validated_race_mode_circuits[size_t(race_mode)] & (1u << circuit)) != 0;
}

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
    // Draws counted per set rejection bit: caster failures and admitted
    // casters that may not receive.
    std::array<size_t, rejection_bit_count> caster_rejection_reasons{}, receiver_rejection_reasons{};
};

// " cutouts=N glass=N caster_bits=[...] receiver_bits=[...]" for logs.
std::string rejection_summary(const AdmissionStats& stats);

// Returns null when the scene/light gate fails. A non-null result can still
// be incomplete; only complete results may ever replace a native overlay.
std::shared_ptr<RT64::SunShadowWorkload> admit_sun_shadow(const ShadowTask& task,
    const std::vector<PresentedDraw>& draws, const std::vector<PresentedLight>& lights,
    const ShadowSettings& settings, AdmissionStats& stats);

} // namespace lambo::rt
