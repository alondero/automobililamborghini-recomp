#include "lambo_rt_admission.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace lambo::rt {
namespace {

enum UnclassifiedShadowDraw : uint32_t {
    UnclassifiedProjection = 1,
    UnclassifiedFaceRange = 2,
    UnclassifiedTransformGroup = 3,
    UnclassifiedObjectIdentity = 4,
    UnclassifiedFaceCountOverflow = 5,
    UnclassifiedObjectRole = 6,
};

// USA native car-shadow child: func_80013328 builds flags 0x42 with this list
// under a physical car. Provenance: docs/rt-provenance.md.
constexpr uint32_t native_overlay_list = 0x8013D3C8u;

bool native_overlay(const PresentedDraw& draw, uint32_t count,
        const std::vector<TaskSunProbe::ObjectIdentity>& objects, uint32_t object_id) {
    const auto& m = draw.material;
    const bool material = count == 48 && m.otherL == 0xC8104A50u && (m.combineW0 & 0xFFFFFFu) == 0x11FFFFu &&
        m.combineW1 == 0xFFFFF238u && (draw.projection.geometry & ~0x800000u) == 0x12005u;
    const auto& object = objects[object_id];
    if (!material || object.flags != 0x42u || object.list != native_overlay_list) return false;
    return object.parent >= 0 && size_t(object.parent) < objects.size() &&
        (objects[size_t(object.parent)].flags & 8u) != 0;
}

} // namespace

std::shared_ptr<RT64::SunShadowWorkload> admit_sun_shadow(const ShadowTask& task,
        const std::vector<PresentedDraw>& draws, const std::vector<PresentedLight>& lights,
        const ShadowSettings& settings, AdmissionStats& stats) {
    stats = {};
    // Player modes 0-3 share the car/light/view path; mode 4 is attract
    // (docs/camera-sequences.md). One player only; models and modes must be
    // validated unless a developer sweep is measuring them.
    const int16_t model = task.model_cursors[0];
    const bool validated_model = std::find(validated_player_models.begin(), validated_player_models.end(), model) !=
        validated_player_models.end();
    const bool model_admitted = model >= 0 && model <= 23 && (validated_model || settings.validation_sweep);
    const bool mode_admitted = task.race_mode >= 0 && task.race_mode <= 3 &&
        (validated_race_mode(task.race_mode, task.circuit) || settings.validation_sweep);
    if (task.circuit < 0 || task.circuit >= 6 || task.phase != 8 || task.players != 1 ||
        !mode_admitted || !model_admitted || !task.emitters_complete) {
        stats.gate = ShadowGate::UnsupportedScene;
        return {};
    }
    if (task.objects.empty() || task.objects.size() > 128) {
        stats.gate = ShadowGate::InvalidObjectIdentity;
        return {};
    }

    // Each circuit keeps its own measured key even where the raw values match;
    // future measurements may differ. No camera or car matrix contributes.
    struct CircuitPolicy { int x, y, z; };
    static constexpr std::array<CircuitPolicy, 6> policies{{
        {-11, 55, -101}, {-11, 55, -101}, {-11, 55, -101},
        {-11, 55, -101}, {-11, 55, -101}, {-11, 55, -101}
    }};
    const CircuitPolicy policy = policies[size_t(task.circuit)];
    const float length = std::sqrt(float(policy.x * policy.x + policy.y * policy.y + policy.z * policy.z));
    bool key_used = false;
    for (const auto& light : lights) {
        if (!light.directional || !physical_car_object(task.objects, light.object_id)) continue;
        if (std::abs(light.x - policy.x) < 1e-4f && std::abs(light.y - policy.y) < 1e-4f &&
            std::abs(light.z - policy.z) < 1e-4f) {
            key_used = true;
            break;
        }
    }
    if (!key_used) {
        stats.gate = ShadowGate::MissingLitCarKey;
        return {};
    }

    auto result = std::make_shared<RT64::SunShadowWorkload>();
    result->sceneEpoch = task.epoch;
    result->taskSequence = task.sequence;
    result->circuit = uint32_t(task.circuit);
    result->phase = task.phase;
    result->playerCount = uint32_t(task.players);
    result->params.direction[0] = float(policy.x) / length;
    result->params.direction[1] = float(policy.y) / length;
    result->params.direction[2] = float(policy.z) / length;
    result->params.strength = settings.strength;
    result->params.angularRadius = settings.angular_radius;
    // Renderer world units; the car is about 4.4 units long (rt-provenance.md).
    result->params.rayMin = 0.01f;
    result->params.rayMax = 10000.0f;
    result->params.originBias = 0.005f;
    result->params.sampleCount = settings.samples;
    result->params.valid = 1;
    if (!RT64::validSunShadowParams(result->params)) {
        stats.gate = ShadowGate::InvalidParameters;
        return {};
    }
    result->authenticated = true;
    stats.gate = ShadowGate::Admitted;

    bool admission_complete = true;
    const auto mark_unclassified = [&](uint32_t first, uint32_t count, uint32_t draw, uint32_t reason,
            uint32_t projection_type) {
        result->unclassified.push_back({first, count, draw, false, reason, projection_type});
        admission_complete = false;
        ++stats.unclassified;
    };
    for (const auto& draw : draws) {
        if (!draw.indexed) {
            // Non-indexed projections do not address faceIndices. Keep draw
            // identity without inventing a face range.
            const uint32_t exclusion = non_caster_reason(draw.projection, draw.material);
            if (exclusion != 0) {
                result->nonCasters.push_back({0, 0, draw.call, false, exclusion, draw.projection_type});
            }
            else mark_unclassified(0, 0, draw.call, UnclassifiedProjection, draw.projection_type);
            continue;
        }
        if (draw.wide_count > std::numeric_limits<uint32_t>::max()) {
            mark_unclassified(draw.first, 0, draw.call, UnclassifiedFaceCountOverflow, draw.projection_type);
            continue;
        }
        const uint32_t count = uint32_t(draw.wide_count);
        if (count == 0) continue;
        if (!draw.face_range_valid) {
            mark_unclassified(draw.first, count, draw.call, UnclassifiedFaceRange, draw.projection_type);
            continue;
        }
        const uint32_t exclusion = non_caster_reason(draw.projection, draw.material);
        if (exclusion != 0) {
            result->nonCasters.push_back({draw.first, count, draw.call, false, exclusion, draw.projection_type});
            continue;
        }
        if (trail_decal_exclusion(draw.material, draw.projection.geometry) != 0) {
            result->nonCasters.push_back({draw.first, count, draw.call, false, NonCasterTrailDecal,
                draw.projection_type});
            continue;
        }
        if (!draw.uniform_group) {
            mark_unclassified(draw.first, count, draw.call, UnclassifiedTransformGroup, draw.projection_type);
            continue;
        }
        const uint32_t object_id = draw.matrix_id & 0xFFFFu;
        if (object_id >= task.objects.size()) {
            mark_unclassified(draw.first, count, draw.call, UnclassifiedObjectIdentity, draw.projection_type);
            continue;
        }
        const auto& object = task.objects[object_id];
        const bool world_builder = object_id == 0 && object.flags == 0x601u && object.list != 0;
        const bool physical_car = physical_car_object(task.objects, object_id);
        const bool procedural_world = procedural_world_object(object);
        if (native_overlay(draw, count, task.objects, object_id)) {
            result->overlays.push_back({draw.first, count, draw.call, false, 0, draw.projection_type});
            ++stats.overlays;
            continue;
        }
        if (!world_builder && !physical_car && !procedural_world) {
            mark_unclassified(draw.first, count, draw.call, UnclassifiedObjectRole, draw.projection_type);
            continue;
        }
        const uint32_t rejection = caster_material_rejection(draw.material);
        if (rejection != 0) {
            if ((world_builder || procedural_world) && world_cutout_exclusion(draw.material) != 0) {
                result->nonCasters.push_back({draw.first, count, draw.call, false,
                    NonCasterWorldCutout, draw.projection_type});
                ++stats.world_cutouts;
                continue;
            }
            if (physical_car && artistic_opaque_car_glass(draw.material)) {
                result->geometry.push_back({draw.first, count, draw.call, true, 0, draw.projection_type});
                result->receiverRejected.push_back({draw.first, count, draw.call, false, rejection,
                    draw.projection_type});
                ++stats.glass_casters;
                stats.admitted_faces += count / 3;
                continue;
            }
            result->rejected.push_back({draw.first, count, draw.call, false, rejection, draw.projection_type});
            ++stats.rejected;
            for (uint32_t bit = 0; bit < stats.rejection_reasons.size(); ++bit) {
                if (rejection & (1u << bit)) ++stats.rejection_reasons[bit];
            }
            continue;
        }
        result->geometry.push_back({draw.first, count, draw.call, true, 0, draw.projection_type});
        uint32_t receiver_rejection = receiver_material_rejection(draw.material);
        if (draw.projection.geometry & f3dex_geometry_lighting) receiver_rejection |= RejectVertexLighting;
        if (receiver_rejection == 0) {
            result->receivers.push_back({draw.first, count, draw.call, true, 0, draw.projection_type});
        }
        else {
            result->receiverRejected.push_back({draw.first, count, draw.call, false, receiver_rejection,
                draw.projection_type});
        }
        stats.admitted_faces += count / 3;
    }
    result->complete = admission_complete && stats.admitted_faces != 0 && !result->receivers.empty() &&
        stats.overlays != 0 && stats.rejected == 0;
    return result;
}

} // namespace lambo::rt
