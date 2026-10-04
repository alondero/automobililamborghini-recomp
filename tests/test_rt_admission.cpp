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
    s.task.emitters_complete = true;
    s.task.objects = {
        {0x601u, 0x80288160u, -1, 0}, // World builder.
        {0x9u, 0x80200000u, -1, 0}, // Physical car.
        {0x26u, 0x80200100u, 1, 0}, // Car child carrying glass.
        {0x42u, 0x8013D3C8u, 1, 0}, // Native car-shadow overlay child.
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
        bool lit_rejected = false;
        for (const auto& range : result->receiverRejected)
            lit_rejected |= range.draw == 3 && (range.rejection & RejectVertexLighting);
        require(lit_rejected, "vertex-lit car surface was not rejected as a receiver");
        bool body_casts = false;
        for (const auto& range : result->geometry) body_casts |= range.draw == 3 && range.opaqueSolid;
        require(body_casts, "lit car body stopped casting");
        bool glass_cast = false, cutout_cast = false;
        for (const auto& range : result->geometry) {
            glass_cast |= range.draw == 5 && range.opaqueSolid;
            cutout_cast |= range.draw == 2;
        }
        require(glass_cast && !cutout_cast, "glass must cast and cutouts must not");
        bool cutout_listed = false;
        for (const auto& range : result->nonCasters) cutout_listed |= range.draw == 2 && range.rejection == NonCasterWorldCutout;
        require(cutout_listed, "excluded cutout lost its diagnostic identity");
        require(std::abs(result->params.direction[1] - 0.476070f) < 1e-5f, "native key not normalized");
        require(result->params.strength == native_overlay_strength, "settings strength not carried");
        // Framebuffer-space contrast: the VI gamma maps 0.243 to the measured
        // displayed native core of about 0.51 (docs/rt-material-evidence.md).
        require(std::abs(native_overlay_strength - 0.757f) < 1e-6f, "native contrast calibration changed");

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
            bool listed = false;
            for (const auto& range : admitted->nonCasters) listed |= range.draw == 7 && range.rejection == NonCasterTrailDecal;
            require(listed, "tyre trail lost its non-caster identity");
            trails.draws.back().material.combineW1 = 0xFFFFF238u;
            admitted = admit(trails, stats);
            require(admitted && !admitted->complete, "unmeasured mixed-group draw was classified");
        }

        // Rectangle non-casters keep zero face extent.
        for (const auto& range : result->nonCasters) {
            if (range.draw == 6) require(range.faceStart == 0 && range.indexCount == 0, "rectangle gained a face range");
        }

        // An unmeasured blended car child is not glass and keeps the scene native.
        Scene other = measured_scene();
        other.draws[4].material.combineW1 = 0xFF33FFFEu;
        result = admit(other, stats);
        require(result && !result->complete && stats.rejected == 1, "unmeasured blended car child admitted");

        // The cutout exclusion is restricted to world roles.
        other = measured_scene();
        other.draws[1].matrix_id = 0x10010001u;
        result = admit(other, stats);
        require(result && !result->complete, "car-owned cutout silently excluded");

        // Without a visible receiver or an overlay the scene cannot replace anything.
        other = measured_scene();
        other.draws.erase(other.draws.begin() + 3);
        result = admit(other, stats);
        require(result && !result->complete, "scene without native overlay marked complete");

        // Scene gates.
        other = measured_scene();
        other.lights[0].y = 54;
        require(!admit(other, stats) && stats.gate == ShadowGate::MissingLitCarKey, "foreign light admitted");
        other = measured_scene();
        other.lights[0].object_id = 0;
        require(!admit(other, stats), "world-object light authenticated the car key");
        other = measured_scene();
        other.lights[0].object_id = 2;
        require(admit(other, stats) != nullptr, "lit car child failed ancestry authentication");
        const auto gated = [&](auto mutate, const char* message) {
            Scene g = measured_scene();
            mutate(g.task);
            require(!admit(g, stats) && stats.gate == ShadowGate::UnsupportedScene, message);
        };
        gated([](ShadowTask& t) { t.players = 2; }, "two players admitted");
        // Every model selector 0-23 passed hard parity on Circuit 1 time trial.
        gated([](ShadowTask& t) { t.model_cursors[0] = 24; }, "out-of-range player model admitted");
        gated([](ShadowTask& t) { t.model_cursors[0] = -1; }, "negative player model admitted");
        for (int16_t model = 0; model <= 23; ++model) {
            Scene validated = measured_scene();
            validated.task.model_cursors[0] = model;
            require(admit(validated, stats) != nullptr, "validated player model gated");
        }
        {
            // Developer validation sweeps may exercise any player race mode.
            Scene sweep = measured_scene();
            sweep.task.circuit = 3;
            sweep.task.race_mode = 3;
            ShadowSettings any = {native_overlay_strength, 0, 8};
            any.validation_sweep = true;
            require(!admit(sweep, stats), "mode 3 admitted on an unmeasured circuit");
            require(admit(sweep, stats, any) != nullptr, "validation sweep could not reach mode 3 on circuit 4");
            sweep.task.model_cursors[0] = 24;
            require(!admit(sweep, stats, any), "validation sweep admitted an out-of-range model");
            sweep.task.model_cursors[0] = 0;
            sweep.task.race_mode = 4;
            require(!admit(sweep, stats, any), "validation sweep admitted attract mode");
        }
        // Modes 0 and 2 passed on all six circuits; modes 1 and 3 only on
        // Circuit 1, because warping into them always loads Circuit 1.
        for (int circuit = 0; circuit < 6; ++circuit) {
            for (int16_t mode = 0; mode < 4; ++mode) {
                Scene candidate = measured_scene();
                candidate.task.circuit = circuit;
                candidate.task.race_mode = mode;
                const bool expected = mode == 0 || mode == 2 || circuit == 0;
                require(validated_race_mode(mode, circuit) == expected, "validated race mode table changed");
                require((admit(candidate, stats) != nullptr) == expected, "race mode admission differs from table");
            }
        }
        gated([](ShadowTask& t) { t.race_mode = 1; t.circuit = 1; }, "mode 1 admitted on unvalidated circuit");
        gated([](ShadowTask& t) { t.race_mode = 3; t.circuit = 5; }, "mode 3 admitted on unvalidated circuit");
        gated([](ShadowTask& t) { t.race_mode = 4; }, "attract mode admitted");
        gated([](ShadowTask& t) { t.phase = 6; }, "menu phase admitted");
        gated([](ShadowTask& t) { t.circuit = 6; }, "unknown circuit admitted");
        gated([](ShadowTask& t) { t.emitters_complete = false; }, "incomplete task admitted");
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
