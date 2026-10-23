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
    bool emitters_complete = false; // Diagnostic emitter spans only.
    // Phase, circuit, players, mode and models matched at queue publication.
    bool scene_consistent = false;
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
    float r = 0, g = 0, b = 0; // RSP light colour, 0 through 1.
};

// Rec. 709 luminance of a light's colour.
inline float light_luminance(const PresentedLight& light) {
    return 0.2126f * light.r + 0.7152f * light.g + 0.0722f * light.b;
}

struct ShadowSettings {
    float strength = 0; // 0 keeps a capture-only Workload from attenuating.
    float angular_radius = 0; // Radians, 0 through 5 degrees.
    uint32_t samples = 8;
    RT64::SunShadowAmbient ambient; // Defaults keep sun-only shadows.
    // Luminance of the car's open-road key, the scale for a lamp light's
    // strength. Production keeps the last one seen; this default is the
    // Circuit 1 start-line key (155, 163, 98)/255.
    float key_luminance = 0.6143f;
};

// All native player-one model/colour selectors are eligible. Caster and
// receiver classification uses each draw's material and object ancestry.
inline constexpr int16_t last_player_model = 23;

// Strength is framebuffer-space attenuation; the VI's gamma runs afterwards.
// The displayed native overlay core transmits about 0.51 on every captured
// circuit/mode, which a hard ray-traced run maps back to 0.243 before the VI.
// A fully occluded hard shadow therefore matches native darkness on screen.
// Measurement: docs/rt-material-evidence.md#native-overlay-contrast.
inline constexpr float native_overlay_strength = 0.757f;

// Zero strength means "not requested", so a lamp pulsed fully dark keeps this
// fraction of the sun's strength and the traced shadow stays in control.
inline constexpr float min_lamp_strength_scale = 0.001f;

enum class ShadowGate : uint32_t {
    Admitted = 0,
    UnsupportedScene,
    InvalidObjectIdentity,
    InvalidParameters,
};

struct AdmissionStats {
    ShadowGate gate = ShadowGate::UnsupportedScene;
    size_t admitted_faces = 0, rejected = 0, unclassified = 0, overlays = 0;
    size_t world_cutouts = 0, glass_casters = 0;
    // The car's lit vertices carried the measured world key this frame.
    bool car_key_seen = false;
    // The physical car's first directional light, for diagnostics only.
    bool car_light_seen = false;
    PresentedLight car_light;
    PresentedLight car_key; // Valid when car_key_seen.
    // The shadow follows the car's non-sun key (tunnel lamps).
    bool lamp_light = false;
    // Draws counted per set rejection bit: caster failures and admitted
    // casters that may not receive.
    std::array<size_t, rejection_bit_count> caster_rejection_reasons{}, receiver_rejection_reasons{};
};

// " cutouts=N glass=N caster_bits=[...] receiver_bits=[...]" for logs.
std::string rejection_summary(const AdmissionStats& stats);

// Returns null outside a one-player race or for invalid settings. Every other
// task gets a result: each draw is classified on its own, and a draw the policy
// cannot use is left out of the shadow scene rather than reverting the frame.
// `complete` means there is something to trace and something to receive.
std::shared_ptr<RT64::SunShadowWorkload> admit_sun_shadow(const ShadowTask& task,
    const std::vector<PresentedDraw>& draws, const std::vector<PresentedLight>& lights,
    const ShadowSettings& settings, AdmissionStats& stats);

} // namespace lambo::rt
