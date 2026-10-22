#include "lambo_rt_admission.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <stdexcept>

namespace {
using namespace lambo::rt;

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

ShadowMaterial opaque_road() {
    ShadowMaterial m;
    m.otherL = m.shaderOtherL = 0xC8112230u;
    m.otherH = m.shaderOtherH = 0x18ACFFu;
    m.combineW0 = 0xFC26A004u;
    m.combineW1 = 0x1FFC93F8u;
    m.zCompare = m.zUpdate = m.standardFog = true;
    m.fog = {0.2f, 0.2f, 0.2f, 1.0f};
    return m;
}

PresentedDraw indexed(uint32_t call, uint32_t first, uint32_t count, uint32_t object, ShadowMaterial material,
        uint32_t geometry = 0x12205u) {
    PresentedDraw d;
    d.call = call;
    d.projection_type = 1;
    d.indexed = true;
    d.first = first;
    d.wide_count = count;
    d.face_range_valid = true;
    d.uniform_group = true;
    d.matrix_id = 0x10010000u | object;
    d.projection = {1, 0, 0, geometry, false};
    d.material = material;
    return d;
}

struct Scene {
    ShadowTask task;
    std::vector<PresentedDraw> draws;
    std::vector<PresentedLight> lights;
};

Scene measured_scene() {
    Scene s;
    s.task.epoch = 3;
    s.task.sequence = 60;
    s.task.circuit = 0;
    s.task.phase = 8;
    s.task.players = 1;
    s.task.race_mode = 2;
    s.task.scene_consistent = true;
    s.task.objects = {
        {0x601u, 0x80288160u, -1, 0}, // World builder.
        {0x9u, 0x80200000u, -1, 0}, // Physical car.
        {0x26u, 0x80200100u, 1, 0}, // Car child carrying glass.
        {0x42u, 0x8013D3C8u, 1, 0}, // Native car-shadow overlay child.
        {0x123u, 0x80300000u, -1, 0}, // A role no measurement has named.
    };
    auto cutout = opaque_road();
    cutout.otherL = cutout.shaderOtherL = 0xCB023038u;
    cutout.coverageAlpha = true;
    ShadowMaterial glass;
    glass.otherL = glass.shaderOtherL = 0x00504A50u;
    glass.otherH = glass.shaderOtherH = 0x8ACFFu;
    glass.combineW0 = 0xFC121824u;
    glass.combineW1 = 0xFF33FFFFu;
    glass.alphaBlend = glass.forceBlend = glass.zCompare = true;
    glass.zMode = 0x800u;
    ShadowMaterial overlay;
    overlay.otherL = overlay.shaderOtherL = 0xC8104A50u;
    overlay.combineW0 = 0xFC11FFFFu;
    overlay.combineW1 = 0xFFFFF238u;
    overlay.alphaBlend = overlay.zCompare = true;
    s.draws.push_back(indexed(1, 0, 30, 0, opaque_road()));
    s.draws.push_back(indexed(2, 30, 12, 0, cutout));
    // A measured receiver material, but vertex-lit (G_LIGHTING): casts only.
    s.draws.push_back(indexed(3, 42, 60, 1, opaque_road(), 0x832205u));
    s.draws.push_back(indexed(4, 102, 48, 3, overlay, 0x12005u));
    s.draws.push_back(indexed(5, 150, 60, 2, glass, 0x822205u));
    PresentedDraw hud;
    hud.call = 6;
    hud.projection_type = 3;
    hud.projection = {3, 0, 0, 0, true};
    s.draws.push_back(hud);
    s.lights.push_back({1, -11, 55, -101, true});
    return s;
}

std::shared_ptr<RT64::SunShadowWorkload> admit(const Scene& s, AdmissionStats& stats,
        ShadowSettings settings = {native_overlay_strength, 0, 8}) {
    return admit_sun_shadow(s.task, s.draws, s.lights, settings, stats);
}

const RT64::SunShadowRange* find(const std::vector<RT64::SunShadowRange>& ranges, uint32_t draw) {
    for (const auto& range : ranges) {
        if (range.draw == draw) return &range;
    }
    return nullptr;
}
}

int main() {
    try {
        AdmissionStats stats;
        Scene scene = measured_scene();
        auto result = admit(scene, stats);
        require(result && result->complete, "measured scene with cutout and glass did not admit");
        require(stats.world_cutouts == 1 && stats.glass_casters == 1 && stats.overlays == 1, "policy counts wrong");
        require(result->overlays.size() == 1 && result->overlays[0].draw == 4, "overlay identity lost");
        require(result->receivers.size() == 1 && result->receivers[0].draw == 1, "only the prelit road may receive");
        require(result->receivers[0].standardFog, "standard-fog road lost its fog composition");
        const auto* lit = find(result->receiverRejected, 3);
        require(lit && (lit->rejection & RejectVertexLighting), "vertex-lit car surface was not rejected as a receiver");
        static_assert(RejectVertexLighting == (1u << (rejection_bit_count - 1)), "rejection tally too small");
        require(stats.receiver_rejection_reasons[rejection_bit_count - 1] == 1,
            "vertex-lit receiver rejection was not tallied");
        require(rejection_summary(stats).find(" cutouts=1 glass=1 ") == 0, "log summary lost policy counts");
        const auto* body = find(result->geometry, 3);
        require(body && body->opaqueSolid, "lit car body stopped casting");
        require(body->dynamicCaster, "car body was not tagged as a moving caster");
        const auto* road = find(result->geometry, 1);
        require(road && !road->dynamicCaster, "world road was tagged as a moving caster");
        const auto* glass = find(result->geometry, 5);
        require(glass && glass->opaqueSolid && glass->dynamicCaster, "car glass must cast as part of the car");
        require(!find(result->geometry, 2), "cutouts must not cast");
        const auto* cutout = find(result->nonCasters, 2);
        require(cutout && cutout->rejection == NonCasterWorldCutout, "excluded cutout lost its diagnostic identity");
        require(std::abs(result->params.direction[1] - 0.476070f) < 1e-5f, "native key not normalized");
        require(result->params.strength == native_overlay_strength, "settings strength not carried");
        // Framebuffer-space contrast: the VI gamma maps 0.243 to the measured
        // displayed native core of about 0.51 (docs/rt-material-evidence.md).
        require(std::abs(native_overlay_strength - 0.757f) < 1e-6f, "native contrast calibration changed");
        require(stats.car_key_seen, "the car's native key was present but not reported");

        {
            // Tyre-trail quads span road segments, so their vertices are not one
            // transform group; the measured decal identity still classifies them.
            Scene trails = measured_scene();
            ShadowMaterial trail;
            trail.otherL = trail.shaderOtherL = 0xC8104A50u;
            trail.otherH = trail.shaderOtherH = 0x18ACFFu;
            trail.combineW0 = 0xFCFFFFFFu;
            trail.combineW1 = 0xFFFE7638u;
            trail.alphaBlend = trail.forceBlend = trail.zCompare = trail.standardFog = true;
            trail.zMode = 0x800u;
            PresentedDraw mark = indexed(7, 210, 6, 0, trail, 0x810205u);
            mark.uniform_group = false;
            trails.draws.push_back(mark);
            auto admitted = admit(trails, stats);
            require(admitted && admitted->complete, "tyre trail kept the scene incomplete");
            const auto* listed = find(admitted->nonCasters, 7);
            require(listed && listed->rejection == NonCasterTrailDecal, "tyre trail lost its non-caster identity");
        }

        // Rectangle non-casters keep zero face extent.
        for (const auto& range : result->nonCasters) {
            if (range.draw == 6) require(range.faceStart == 0 && range.indexCount == 0, "rectangle gained a face range");
        }

        // Robust per-draw policy: one unfamiliar draw (a tunnel lamp, a sign,
        // an unmeasured blend) is left out of the shadow scene. It never sends
        // the whole frame back to the native blob, which is what made the
        // shadow switch with track and viewpoint.
        {
            Scene other = measured_scene();
            other.draws[4].material.combineW1 = 0xFF33FFFEu; // Unmeasured blended car child.
            result = admit(other, stats);
            require(result && result->complete, "an unmeasured blended car child reverted the frame");
            require(!find(result->geometry, 5), "an unmeasured blended draw cast a shadow");
            require(find(result->rejected, 5) && stats.rejected == 1, "excluded blend lost its diagnostic");

            other = measured_scene();
            other.draws[1].matrix_id = 0x10010001u; // Car-owned cutout.
            result = admit(other, stats);
            require(result && result->complete, "a car-owned cutout reverted the frame");
            require(!find(result->geometry, 2), "a cutout cast as a solid");

            // An opaque perspective draw whose role nobody measured still casts:
            // shadows come from what is drawn, not from a list of known owners.
            other = measured_scene();
            other.draws.push_back(indexed(8, 210, 30, 4, opaque_road()));
            result = admit(other, stats);
            require(result && result->complete, "an unknown object role reverted the frame");
            require(find(result->geometry, 8) && find(result->receivers, 8), "unknown opaque scenery was excluded");

            // Untagged geometry (no object identity, or spanning groups) is
            // still ordinary opaque scenery.
            other = measured_scene();
            PresentedDraw untagged = indexed(9, 210, 30, 0, opaque_road());
            untagged.uniform_group = false;
            untagged.matrix_id = 0xFFFFFFFFu;
            other.draws.push_back(untagged);
            result = admit(other, stats);
            require(result && result->complete, "untagged geometry reverted the frame");
            const auto* range = find(result->geometry, 9);
            require(range && !range->dynamicCaster, "untagged opaque geometry did not cast as world");

            // A non-indexed draw that is not a known rectangle carries no
            // geometry to trace, so it is simply ignored.
            other = measured_scene();
            PresentedDraw raw;
            raw.call = 10;
            raw.projection_type = 4;
            raw.projection = {4, 0, 0, 0, false};
            raw.material = opaque_road();
            other.draws.push_back(raw);
            result = admit(other, stats);
            require(result && result->complete, "a raw-triangle draw reverted the frame");

            // Orthographic draws are screen-space art and never trace.
            other = measured_scene();
            PresentedDraw ortho = indexed(11, 210, 30, 0, opaque_road());
            ortho.projection_type = 2;
            ortho.projection.type = 2;
            other.draws.push_back(ortho);
            result = admit(other, stats);
            require(result && result->complete && !find(result->geometry, 11) && !find(result->receivers, 11),
                "an orthographic draw entered the world shadow scene");
            // Its diagnostic record keeps the presented face range, which the
            // render-capture checker compares with the draw.
            const auto* untraced = find(result->unclassified, 11);
            require(untraced && untraced->faceStart == 210 && untraced->indexCount == 30,
                "an indexed orthographic draw lost its face range");

            // A range RT64 cannot address is skipped, not fatal.
            other = measured_scene();
            PresentedDraw broken = indexed(12, 210, 30, 0, opaque_road());
            broken.face_range_valid = false;
            other.draws.push_back(broken);
            result = admit(other, stats);
            require(result && result->complete && !find(result->geometry, 12), "an invalid range reverted the frame");
        }

        // Receivers are chosen by what the composite needs, not by a short
        // list of measured road combiners: an opaque surface whose blender is
        // standard fog or no fog at all can be attenuated exactly.
        {
            Scene other = measured_scene();
            ShadowMaterial tunnel_road = opaque_road();
            tunnel_road.combineW0 = 0xFC121603u; // Not one of the four measured combiners.
            tunnel_road.combineW1 = 0xFF5BFFF8u;
            other.draws.push_back(indexed(13, 210, 30, 0, tunnel_road));
            result = admit(other, stats);
            const auto* fogged = find(result->receivers, 13);
            require(fogged && fogged->standardFog, "a standard-fog road with another combiner did not receive");

            other = measured_scene();
            ShadowMaterial unfogged = opaque_road();
            unfogged.otherL = unfogged.shaderOtherL = 0x00552078u; // 1-cycle opaque, no fog in the blender.
            unfogged.otherH = unfogged.shaderOtherH = 0x0008ACFFu;
            unfogged.standardFog = false;
            other.draws.push_back(indexed(14, 210, 30, 0, unfogged));
            result = admit(other, stats);
            const auto* plain = find(result->receivers, 14);
            require(plain && !plain->standardFog, "an unfogged opaque surface did not receive without fog");

            // A blender that reads fog some other way cannot be composed exactly.
            other = measured_scene();
            ShadowMaterial odd_fog = unfogged;
            odd_fog.otherL = odd_fog.shaderOtherL = 0x30552078u; // Fog colour in cycle 2 only.
            other.draws.push_back(indexed(15, 210, 30, 0, odd_fog));
            result = admit(other, stats);
            require(!find(result->receivers, 15) && find(result->geometry, 15),
                "a non-standard fog blender received a shadow");

            // Copy and fill cycles are never 3D surfaces.
            other = measured_scene();
            ShadowMaterial copy = unfogged;
            copy.otherH = copy.shaderOtherH = 0x0028ACFFu;
            other.draws.push_back(indexed(16, 210, 30, 0, copy));
            result = admit(other, stats);
            require(!find(result->receivers, 16), "a copy-cycle draw received a shadow");
        }

        // Without a native overlay in view (an in-car camera, the car off
        // screen) the scenery still has sunlight shadows; there is simply no
        // blob to replace.
        {
            Scene other = measured_scene();
            other.draws.erase(other.draws.begin() + 3);
            result = admit(other, stats);
            require(result && result->complete && result->overlays.empty(), "scenery shadows needed the car overlay");
            other.draws.erase(other.draws.begin());
            result = admit(other, stats);
            require(result && !result->complete, "a frame without any receiver was marked complete");
        }

        // The sun is the measured world key on every circuit. A view that does
        // not show the lit car (in-car camera, a tunnel's dimmed car lights)
        // keeps the same sun rather than losing its shadows.
        {
            Scene other = measured_scene();
            other.lights.clear();
            result = admit(other, stats);
            require(result && result->complete && !stats.car_key_seen, "missing car key changed the shadow scene");
            require(std::abs(result->params.direction[1] - 0.476070f) < 1e-5f, "sun moved without a car key");
            require(!stats.car_light_seen, "a car light was reported without one");
        }
        // The car's first directional light is kept for the change log, matched
        // or not: it shows how the game lights the car where the key is absent.
        {
            Scene other = measured_scene();
            other.lights = {{1, 20, 30, 40, true, 0.25f, 0.5f, 0.75f}, {1, -11, 55, -101, true, 1, 1, 1}};
            result = admit(other, stats);
            require(stats.car_light_seen && stats.car_light.x == 20 && stats.car_light.b == 0.75f,
                "the car's first directional light was not recorded");
            require(stats.car_key_seen, "a later matching key was missed");
        }
        // Tunnels: the game swaps the car's key for its lamp light and pulses
        // its colour as the car passes each lamp (Circuit 1 lap, tasks 460-490).
        // The shadow follows that light; only cars block it, because the walls'
        // lamp light is already in the art, and the sky test does not apply.
        {
            Scene other = measured_scene();
            other.lights = {{1, -100, 80, 0, true, 0.553f, 0.451f, 0.384f}};
            ShadowSettings settings{native_overlay_strength, 0, 8};
            settings.ambient.skyDistance = 30;
            settings.key_luminance = 0.6f;
            result = admit(other, stats, settings);
            require(result && result->complete, "a lamp-lit tunnel frame did not admit");
            const float length = std::sqrt(100.0f * 100.0f + 80.0f * 80.0f);
            require(std::abs(result->params.direction[0] + 100.0f / length) < 1e-5f &&
                std::abs(result->params.direction[1] - 80.0f / length) < 1e-5f &&
                result->params.direction[2] == 0, "shadow did not follow the car's lamp light");
            const float luminance = 0.2126f * 0.553f + 0.7152f * 0.451f + 0.0722f * 0.384f;
            require(std::abs(result->params.strength - native_overlay_strength * luminance / 0.6f) < 1e-5f,
                "lamp shadow strength does not follow the lamp's brightness");
            require(result->ambient.occluderMask == RT64::SunShadowMovingMask, "world geometry blocks the lamp light");
            require(result->ambient.skyDistance == 0, "the sky test still applies under the lamps");
            require(stats.lamp_light, "lamp light was not reported");

            // A lamp brighter than the open-road key cannot darken more than the sun.
            other.lights = {{1, -100, 80, 0, true, 1, 1, 1}};
            result = admit(other, stats, settings);
            require(result && result->params.strength == native_overlay_strength, "lamp strength exceeded the sun's");
            // A lamp pulsed fully dark must not reach strength 0: the renderer
            // reads that as "not requested" and draws the native blob.
            other.lights = {{1, -100, 80, 0, true, 0, 0, 0}};
            result = admit(other, stats, settings);
            require(result && result->params.strength > 0, "a dark lamp turned the traced shadow off");
            // In sunlight the world blocks too and the sky test stays on.
            result = admit(measured_scene(), stats, settings);
            require(result && result->ambient.occluderMask == RT64::SunShadowAllMask &&
                result->ambient.skyDistance == 30 && !stats.lamp_light, "sunlit frame lost world occluders");
        }

        // Scene gates: one-player races only, on any circuit and race mode.
        for (int circuit = 0; circuit < 6; ++circuit) {
            for (int16_t mode = 0; mode <= 4; ++mode) {
                Scene candidate = measured_scene();
                candidate.task.circuit = circuit;
                candidate.task.race_mode = mode;
                require(admit(candidate, stats) != nullptr, "a race circuit/mode was gated");
            }
        }
        for (int16_t model = 0; model <= 23; ++model) {
            Scene candidate = measured_scene();
            candidate.task.model_cursors[0] = model;
            require(admit(candidate, stats) != nullptr, "a player model was gated");
        }
        const auto gated = [&](auto mutate, const char* message) {
            Scene g = measured_scene();
            mutate(g.task);
            require(!admit(g, stats) && stats.gate == ShadowGate::UnsupportedScene, message);
        };
        gated([](ShadowTask& t) { t.players = 2; }, "two players admitted");
        gated([](ShadowTask& t) { t.race_mode = 5; }, "unknown race mode admitted");
        gated([](ShadowTask& t) { t.race_mode = -1; }, "missing race mode admitted");
        gated([](ShadowTask& t) { t.phase = 6; }, "menu phase admitted");
        gated([](ShadowTask& t) { t.circuit = 6; }, "unknown circuit admitted");
        gated([](ShadowTask& t) { t.scene_consistent = false; }, "a task whose scene changed mid-build admitted");
        {
            // Emitter spans are diagnostics; a busy frame must not gate shadows.
            Scene busy = measured_scene();
            busy.task.emitters_complete = false;
            require(admit(busy, stats) != nullptr, "incomplete diagnostic emitter spans gated the frame");
        }
        require(!admit(scene, stats, {0.49f, 0.2f, 8}) && stats.gate == ShadowGate::InvalidParameters,
            "angular radius above 5 degrees admitted");
        require(!admit(scene, stats, {0.49f, 0.0f, 5}) && stats.gate == ShadowGate::InvalidParameters,
            "unsupported sample count admitted");
        std::puts("PASS: production sun-shadow admission policy");
        return 0;
    }
    catch (const std::exception& error) {
        std::fprintf(stderr, "FAIL: %s\n", error.what());
        return 1;
    }
}
