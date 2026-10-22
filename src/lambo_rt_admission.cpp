#include "lambo_rt_admission.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <string>

namespace lambo::rt {
namespace {

// Diagnostic codes for draws the policy cannot trace; none of them gate a frame.
enum UnclassifiedShadowDraw : uint32_t {
    UnclassifiedProjection = 1,
    UnclassifiedFaceRange = 2,
    UnclassifiedFaceCountOverflow = 5,
};

// USA native car-shadow child: func_80013328 builds flags 0x42 with this list
// under a physical car. Provenance: docs/rt-provenance.md.
constexpr uint32_t native_overlay_list = 0x8013D3C8u;

bool native_overlay(const PresentedDraw& draw, uint32_t count,
        const std::vector<TaskSunProbe::ObjectIdentity>& objects, uint32_t object_id) {
    const auto& m = draw.material;
    const bool material = count == 48 &&
        native_overlay_material(m.otherL, m.combineW0, m.combineW1, draw.projection.geometry);
    const auto& object = objects[object_id];
    if (!material || object.flags != 0x42u || object.list != native_overlay_list) return false;
    return object.parent >= 0 && size_t(object.parent) < objects.size() &&
        (objects[size_t(object.parent)].flags & 8u) != 0;
}

void tally_rejection(std::array<size_t, rejection_bit_count>& tally, uint32_t rejection) {
    for (uint32_t bit = 0; bit < rejection_bit_count; ++bit) {
        if (rejection & (1u << bit)) ++tally[bit];
    }
}

} // namespace

std::string rejection_summary(const AdmissionStats& stats) {
    std::string text = " cutouts=" + std::to_string(stats.world_cutouts) +
        " glass=" + std::to_string(stats.glass_casters);
    const auto bits = [&text](const char* name, const auto& tally) {
        text += std::string(" ") + name + "=[";
        for (size_t bit = 0; bit < tally.size(); ++bit) {
            text += (bit ? "," : "") + std::to_string(tally[bit]);
        }
        text += "]";
    };
    bits("caster_bits", stats.caster_rejection_reasons);
    bits("receiver_bits", stats.receiver_rejection_reasons);
    return text;
}

std::shared_ptr<RT64::SunShadowWorkload> admit_sun_shadow(const ShadowTask& task,
        const std::vector<PresentedDraw>& draws, const std::vector<PresentedLight>& lights,
        const ShadowSettings& settings, AdmissionStats& stats) {
    stats = {};
    // Fail closed: one-player races in the validated race modes and cars only.
    // Anything else keeps the native shadow until its capture run is recorded.
    const int model = task.model_cursors[0];
    const bool mode_admitted = task.race_mode >= 0 && task.race_mode <= 3 &&
        (settings.validation_sweep || validated_race_mode(task.race_mode, task.circuit));
    const bool model_admitted = settings.validation_sweep
        ? model >= 0 && model <= last_player_model
        : validated_player_model(model, task.race_mode, task.circuit);
    if (task.circuit < 0 || task.circuit >= 6 || task.phase != 8 || task.players != 1 ||
        !mode_admitted || !model_admitted || !task.scene_consistent) {
        stats.gate = ShadowGate::UnsupportedScene;
        return {};
    }
    if (task.objects.empty() || task.objects.size() > 128) {
        stats.gate = ShadowGate::InvalidObjectIdentity;
        return {};
    }

    // The world key measured on all six circuits, fixed under camera and car
    // motion (docs/rt-provenance.md). It stays the sun when the view shows no
    // lit car (in-car camera).
    static constexpr float key[3] = {-11, 55, -101};
    const float length = std::sqrt(key[0] * key[0] + key[1] * key[1] + key[2] * key[2]);
    bool lamp_seen = false;
    PresentedLight brightest_lamp{};
    for (const auto& light : lights) {
        if (!light.directional || !physical_car_object(task.objects, light.object_id)) continue;
        if (!stats.car_light_seen) {
            stats.car_light_seen = true;
            stats.car_light = light;
        }
        if (std::abs(light.x - key[0]) < 1e-4f && std::abs(light.y - key[1]) < 1e-4f &&
            std::abs(light.z - key[2]) < 1e-4f) {
            stats.car_key_seen = true;
            stats.car_key = light;
            break;
        }
        // Without the key, a frame can carry several car lights; the lamp that
        // lights the car is the brightest one, not whichever came first.
        if (!lamp_seen || light_luminance(light) > light_luminance(brightest_lamp)) {
            brightest_lamp = light;
            lamp_seen = true;
        }
    }
    if (!stats.car_key_seen && lamp_seen) stats.car_light = brightest_lamp;

    auto result = std::make_shared<RT64::SunShadowWorkload>();
    result->sceneEpoch = task.epoch;
    result->taskSequence = task.sequence;
    result->circuit = uint32_t(task.circuit);
    result->phase = task.phase;
    result->playerCount = uint32_t(task.players);
    result->ambient = settings.ambient;
    result->params.strength = settings.strength;
    // Where the game lights the car with another directional light (tunnel
    // lamps; Circuit 1 lap tasks 460-490 pulse it lamp by lamp), the car's
    // shadow follows that light instead. Its brightness against the open-road
    // key scales the sun's strength. Scenery already carries the lamps' light
    // in its art, so only cars block it, and the sky test does not apply.
    const PresentedLight& lamp = stats.car_light;
    const float lamp_length = std::sqrt(lamp.x * lamp.x + lamp.y * lamp.y + lamp.z * lamp.z);
    stats.lamp_light = !stats.car_key_seen && stats.car_light_seen && std::isfinite(lamp_length) && lamp_length > 0;
    if (stats.lamp_light) {
        result->params.direction[0] = lamp.x / lamp_length;
        result->params.direction[1] = lamp.y / lamp_length;
        result->params.direction[2] = lamp.z / lamp_length;
        const float scale = settings.key_luminance > 0 ? light_luminance(lamp) / settings.key_luminance : 0;
        result->params.strength = settings.strength * std::clamp(std::isfinite(scale) ? scale : 0.0f, min_lamp_strength_scale, 1.0f);
        result->ambient.occluderMask = RT64::SunShadowMovingMask;
        result->ambient.skyDistance = 0;
    }
    else {
        for (int axis = 0; axis < 3; ++axis) result->params.direction[axis] = key[axis] / length;
    }
    result->params.angularRadius = settings.angular_radius;
    // Renderer world units: the car measures about 4.35 long, 1.67 wide and
    // 1.13 tall (Circuit 1 start-line render capture, docs/rt-shadows.md).
    result->params.rayMin = 0.01f;
    result->params.rayMax = 10000.0f;
    result->params.originBias = 0.005f;
    result->params.sampleCount = settings.samples;
    result->params.valid = 1;
    if (!RT64::validSunShadowParams(result->params) || !RT64::validSunShadowAmbient(result->ambient)) {
        stats.gate = ShadowGate::InvalidParameters;
        return {};
    }
    result->authenticated = true;
    stats.gate = ShadowGate::Admitted;

    const auto skip = [&](std::vector<RT64::SunShadowRange>& list, const PresentedDraw& draw, uint32_t first,
            uint32_t count, uint32_t reason) {
        list.push_back({first, count, draw.call, false, reason, draw.projection_type});
    };
    for (const auto& draw : draws) {
        // Only perspective draws are world geometry. Screen rectangles, the
        // HUD and raw triangles carry no face range; indexed orthographic
        // draws keep theirs in the record but never trace.
        if (!draw.indexed || draw.projection_type != 1) {
            const bool ranged = draw.indexed && draw.wide_count <= std::numeric_limits<uint32_t>::max();
            const uint32_t first = ranged ? draw.first : 0;
            const uint32_t count = ranged ? uint32_t(draw.wide_count) : 0;
            const uint32_t exclusion = non_caster_reason(draw.projection, draw.material);
            if (exclusion != 0) skip(result->nonCasters, draw, first, count, exclusion);
            else {
                skip(result->unclassified, draw, first, count, UnclassifiedProjection);
                ++stats.unclassified;
            }
            continue;
        }
        if (draw.wide_count > std::numeric_limits<uint32_t>::max()) {
            skip(result->unclassified, draw, draw.first, 0, UnclassifiedFaceCountOverflow);
            ++stats.unclassified;
            continue;
        }
        const uint32_t count = uint32_t(draw.wide_count);
        if (count == 0) continue;
        if (!draw.face_range_valid) {
            skip(result->unclassified, draw, draw.first, count, UnclassifiedFaceRange);
            ++stats.unclassified;
            continue;
        }
        const uint32_t exclusion = non_caster_reason(draw.projection, draw.material);
        if (exclusion != 0) {
            skip(result->nonCasters, draw, draw.first, count, exclusion);
            continue;
        }
        if (trail_decal_exclusion(draw.material, draw.projection.geometry) != 0) {
            skip(result->nonCasters, draw, draw.first, count, NonCasterTrailDecal);
            continue;
        }
        // Object identity only refines the policy: it names the native overlay
        // and tells moving cars from scenery. Untagged draws are scenery.
        const uint32_t object_id = presented_object_index(draw.matrix_id);
        const bool identified = draw.uniform_group && object_id < task.objects.size();
        const bool physical_car = identified && physical_car_object(task.objects, object_id);
        if (identified && native_overlay(draw, count, task.objects, object_id)) {
            result->overlays.push_back({draw.first, count, draw.call, false, 0, draw.projection_type});
            ++stats.overlays;
            continue;
        }
        RT64::SunShadowRange caster{draw.first, count, draw.call, true, 0, draw.projection_type};
        caster.dynamicCaster = physical_car;
        const uint32_t rejection = caster_material_rejection(draw.material);
        if (rejection != 0) {
            if (!physical_car && world_cutout_exclusion(draw.material) != 0) {
                skip(result->nonCasters, draw, draw.first, count, NonCasterWorldCutout);
                ++stats.world_cutouts;
                continue;
            }
            if (physical_car && artistic_opaque_car_glass(draw.material)) {
                result->geometry.push_back(caster);
                skip(result->receiverRejected, draw, draw.first, count, rejection);
                ++stats.glass_casters;
                stats.admitted_faces += count / 3;
                continue;
            }
            // Translucent, cutout or unusual depth: no proven solid coverage.
            skip(result->rejected, draw, draw.first, count, rejection);
            ++stats.rejected;
            tally_rejection(stats.caster_rejection_reasons, rejection);
            continue;
        }
        result->geometry.push_back(caster);
        stats.admitted_faces += count / 3;
        uint32_t receiver_rejection = receiver_material_rejection(draw.material);
        if (draw.projection.geometry & f3dex_geometry_lighting) receiver_rejection |= RejectVertexLighting;
        if (receiver_rejection == 0) {
            RT64::SunShadowRange receiver = caster;
            receiver.standardFog = draw.material.standardFog;
            result->receivers.push_back(receiver);
        }
        else {
            skip(result->receiverRejected, draw, draw.first, count, receiver_rejection);
            tally_rejection(stats.receiver_rejection_reasons, receiver_rejection);
        }
    }
    result->complete = stats.admitted_faces != 0 && !result->receivers.empty();
    return result;
}

} // namespace lambo::rt
