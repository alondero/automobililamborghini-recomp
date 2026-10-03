#include "lambo_rt_shadows.h"
#include "shared/rt64_sun_shadow.h"

#include <cmath>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <vector>

namespace {
void require(bool value, const char* why) {
    if (!value) throw std::runtime_error(why);
}
template<class T> void write(std::vector<uint8_t>& ram, uint32_t address, T value) {
    std::memcpy(ram.data() + ((address & 0x7FFFFFu) ^ (sizeof(T) == 2 ? 2u : 0u)),
        &value, sizeof(value));
}
}

int main() {
    try {
        std::vector<lambo::rt::TaskSunProbe::ObjectIdentity> car_objects{{9, 0x80001000u, -1},
            {0x26, 0x80002000u, 0}, {0x26, 0x80003000u, 1}, {0x26, 0x80004000u, 9}};
        require(lambo::rt::physical_car_object(car_objects, 0), "physical car root not identified");
        require(lambo::rt::physical_car_object(car_objects, 1), "physical car child not identified through parent");
        require(lambo::rt::physical_car_object(car_objects, 2), "nested physical car child not identified through parent chain");
        require(!lambo::rt::physical_car_object(car_objects, 3), "out-of-range parent identity admitted");
        require(!lambo::rt::physical_car_object(car_objects, 99), "out-of-range object identity admitted");
        uint32_t presented_object = 0;
        for (uint32_t viewport = 0; viewport < 4; ++viewport) {
            const uint32_t matrix_id = 0x10000000u | ((viewport + 1) << 16) | 0x2Au;
            require(lambo::rt::presented_object_id(matrix_id, presented_object) && presented_object == 0x2Au,
                "presented object identity rejected a supported viewport slot");
        }
        for (uint32_t matrix_id : {0x0001002Au, 0x2001002Au, 0x1011002Au}) {
            require(!lambo::rt::presented_object_id(matrix_id, presented_object),
                "non-presented transform group admitted as a scene object");
        }
        std::vector<lambo::rt::TaskSunProbe::ObjectIdentity> cyclic_objects{{0, 0, 1}, {0, 0, 0}};
        require(!lambo::rt::physical_car_object(cyclic_objects, 0), "parent cycle admitted as physical car");
        std::vector<lambo::rt::TaskSunProbe::ObjectIdentity> negative_root{{0, 0, -144}};
        require(!lambo::rt::physical_car_object(negative_root, 0), "negative root sentinel admitted as a car");
        const lambo::rt::TaskSunProbe::ObjectIdentity procedural{0xC01, 0, -1, 13};
        require(lambo::rt::procedural_world_object(procedural), "native procedural world role not identified");
        for (unsigned field = 0; field < 4; ++field) {
            auto unknown = procedural;
            if (field == 0) unknown.flags = 0x801;
            if (field == 1) unknown.list = 0x80100000u;
            if (field == 2) unknown.parent = 0;
            if (field == 3) unknown.kind = 12;
            require(!lambo::rt::procedural_world_object(unknown), "unknown procedural identity admitted");
        }
        std::vector<uint8_t> ram(0x800000);
        lambo::rt::TaskSunProbes probes;
        auto begin = [&](uint32_t task, int16_t phase = 8, int16_t circuit = 0) {
            write(ram, 0x800A2BFCu, task);
            write(ram, 0x800CE6ACu, phase);
            write(ram, 0x800CE794u, circuit);
            write(ram, 0x800CE6A4u, int16_t(1));
            return probes.begin(ram.data(), ram.size());
        };
        auto capture = [&](int16_t camera_slot, int16_t heading, float height) {
            write(ram, 0x800CE6AAu, camera_slot);
            write(ram, 0x800A2FB8u, int16_t(72));
            write(ram, 0x800A2F10u + uint32_t(camera_slot) * 2, heading);
            write(ram, 0x800A2F90u, height);
            return probes.capture(ram.data(), ram.size());
        };
        require(!probes.begin(nullptr, ram.size()), "null RAM admitted");
        require(!probes.begin(ram.data(), 16), "short RAM admitted");
        require(begin(0x800BF240), "first task rejected");
        require(capture(0, 50, 25), "first view rejected");
        require(begin(0x800C6C90), "second task rejected");
        require(capture(0, 190, -12), "second view rejected");
        const auto first = probes.take(0x000BF400);
        require(first && first->cameras[0] && first->cameras[0]->camera_heading == 50,
            "consumer used latest camera instead of matching task");
        const auto second = probes.take(0x800C6E50);
        require(second && second->sequence > first->sequence &&
            second->cameras[0]->bearing == first->cameras[0]->bearing &&
            second->cameras[0]->camera_height_term == -12, "task values changed");
        require(!probes.take(0x800C6E50), "task consumed twice");
        for (uint32_t arena : {0x800BF240u, 0x800C6C90u}) {
            const uint32_t physical_root = (arena & 0x1FFFFFFFu) + 0x1C0u;
            for (uint32_t segment : {0u, 0x80000000u, 0xA0000000u}) {
                require(begin(arena) && capture(1, 123, 42), "alias task capture rejected");
                const auto aliased = probes.take(physical_root | segment);
                require(aliased && aliased->task_address == arena && aliased->cameras[1] &&
                    aliased->cameras[1]->camera_heading == 123 &&
                    aliased->cameras[1]->camera_height_term == 42, "root alias lost the matching task");
                for (uint32_t other : {0u, 0x80000000u, 0xA0000000u})
                    require(!probes.take(physical_root | other), "task consumed twice through another alias");
            }
        }
        require(begin(0x800BF240) && capture(0, 50, 25), "alias rejection setup failed");
        for (uint32_t address : {0x200BF400u, 0x400BF400u, 0x600BF400u,
            0xC00BF400u, 0xE00BF400u, 0xA00BF408u, 0xA0100000u})
            require(!probes.take(address), "unsupported segment or non-root address admitted");
        const auto retained = probes.take(0xA00BF400);
        require(retained && retained->cameras[0] && retained->cameras[0]->camera_heading == 50,
            "invalid alias consumed the pending task");
        require(begin(0x800BF240), "reuse rejected");
        require(!capture(0, 0, std::numeric_limits<float>::quiet_NaN()), "NaN admitted");
        require(!capture(4, 0, 0), "unknown view admitted");
        require(!probes.take(0x800BF408), "interior list matched a task");
        const auto empty = probes.take(0x800BF400);
        require(empty && !empty->cameras[0], "reused arena retained previous sun");
        require(begin(0x800BF240) && capture(1, 90, 0), "second camera slot rejected");
        require(begin(0x800C6C90, 7, 4), "new scene rejected");
        require(!probes.take(0x800BF400), "scene transition retained old task");
        const auto city = probes.take(0x800C6E50);
        require(city && city->epoch > first->epoch && !city->cameras[0], "scene epoch not advanced");
        require(begin(0x800BF240), "restart rejected");
        probes.invalidate();
        require(!probes.take(0x800BF400), "save-state invalidation retained a task");
        require(!begin(0x80100000), "unknown task arena admitted");
        // Emitter observations belong to the exact task, including empty
        // spans and incomplete captures. Material words never admit a draw.
        auto cursor = [&](uint32_t address) { write(ram, 0x800A39CCu, address); };
        require(begin(0x800BF240), "emitter setup failed");
        write(ram, 0x800CE6AAu, int16_t(1));
        cursor(0x800BF500);
        require(probes.emitter_begin(ram.data(), ram.size(), 0x800159FC), "emitter begin rejected");
        require(begin(0x800C6C90), "alternate emitter setup failed");
        cursor(0x800C7000);
        require(probes.emitter_begin(ram.data(), ram.size(), 0x8000F6D8), "panorama begin rejected");
        cursor(0x800C7040);
        require(probes.emitter_end(ram.data(), ram.size(), 0x8000F6D8), "panorama end rejected");
        write(ram, 0x800A2BFCu, 0x800BF240u);
        cursor(0x800BF510);
        require(probes.emitter_end(ram.data(), ram.size(), 0x800159FC), "first emitter end rejected");
        const auto spans = probes.take(0x800BF400);
        require(spans && spans->emitters_complete && spans->emitters.size() == 1 &&
            spans->emitters[0].begin == 0x800BF500 && spans->emitters[0].end == 0x800BF510 &&
            spans->emitters[0].camera_slot == 1, "emitter spans mixed between tasks");
        const auto panorama = probes.take(0xA00C6E50);
        require(panorama && panorama->emitters.size() == 1 && panorama->emitters[0].emitter == 0x8000F6D8,
            "panorama lost its task identity");
        require(begin(0x800BF240), "incomplete emitter setup failed");
        cursor(0x800BF500);
        require(probes.emitter_begin(ram.data(), ram.size(), 0x800159FC), "pending begin rejected");
        const auto incomplete = probes.take(0x800BF400);
        require(incomplete && !incomplete->emitters_complete, "open emitter reported complete");
        require(begin(0x800BF240), "invalid cursor setup failed");
        cursor(0x800BF501);
        require(!probes.emitter_begin(ram.data(), ram.size(), 0x800159FC), "unaligned cursor accepted");
        const auto invalid = probes.take(0x800BF400);
        require(invalid && !invalid->emitters_complete, "invalid cursor reported complete");
        for (uint32_t outside : {0x800BF3F8u, 0x800C6C08u, 0x800C6C98u, 0x80100000u}) {
            require(begin(0x800BF240), "arena bounds setup failed");
            cursor(outside);
            require(!probes.emitter_begin(ram.data(), ram.size(), 0x800159FC), "foreign cursor admitted");
            require(!probes.take(0x800BF400)->emitters_complete, "foreign cursor reported complete");
        }
        require(begin(0x800BF240), "reversed span setup failed");
        cursor(0x800BF510);
        require(probes.emitter_begin(ram.data(), ram.size(), 0x800159FC), "reversed span begin failed");
        cursor(0x800BF500);
        require(!probes.emitter_end(ram.data(), ram.size(), 0x800159FC), "reversed span admitted");
        require(!probes.take(0x800BF400)->emitters_complete, "reversed span reported complete");
        require(begin(0x800BF240), "pending invalidation setup failed");
        require(probes.emitter_begin(ram.data(), ram.size(), 0x800159FC), "invalidation begin failed");
        probes.invalidate();
        require(begin(0x800BF240), "post-invalidation begin failed");
        require(!probes.emitter_end(ram.data(), ram.size(), 0x800159FC), "stale emitter survived invalidation");
        require(begin(0x800BF240), "bounded emitter setup failed");
        cursor(0x800BF500);
        for (unsigned i = 0; i < 64; ++i) {
            require(probes.emitter_begin(ram.data(), ram.size(), 0x800159FC) &&
                probes.emitter_end(ram.data(), ram.size(), 0x800159FC), "bounded empty span rejected");
        }
        require(probes.emitter_begin(ram.data(), ram.size(), 0x800159FC), "overflow setup failed");
        require(!probes.emitter_end(ram.data(), ram.size(), 0x800159FC), "span budget exceeded");
        const auto overflow = probes.take(0x800BF400);
        require(overflow && !overflow->emitters_complete && overflow->emitters.size() == 64,
            "span overflow retained an unbounded/complete capture");
        lambo::rt::TaskSunProbes snapshots;
        require(!snapshots.snapshot(nullptr, ram.size()) && !snapshots.snapshot(ram.data(), 16),
            "snapshot admitted absent/short RAM");
#if defined(_WIN32)
        _putenv_s("LAMBO_RT_CAPTURE_DIR", "rt-sun-test-capture");
#else
        setenv("LAMBO_RT_CAPTURE_DIR", "rt-sun-test-capture", 1);
#endif
        write(ram, 0x800A2BFCu, 0x800BF240u);
        for (unsigned i = 1; i < 60; ++i) {
            require(snapshots.begin(ram.data(), ram.size()), "snapshot sequence begin failed");
            require(!snapshots.snapshot(ram.data(), ram.size()), "unsampled task allocated RAM");
        }
        require(snapshots.begin(ram.data(), ram.size()), "sampled snapshot begin failed");
        ram[0xB69A8] = 0x11;
        require(snapshots.snapshot(ram.data(), ram.size()), "producer snapshot rejected");
        ram[0xB69A8] = 0x22;
        require(!snapshots.snapshot(ram.data(), ram.size()), "snapshot overwritten after publication");
        write(ram, 0x800A2BFCu, 0x800C6C90u);
        require(snapshots.begin(ram.data(), ram.size()), "next producer task rejected");
        const auto owned = snapshots.take(0x800BF400);
        require(owned && owned->sequence == 60 && owned->native_ram &&
            owned->native_ram->size() == ram.size() && (*owned->native_ram)[0xB69A8] == 0x11,
            "consumer read next task's mutable globals");
        snapshots.invalidate();
        require((*owned->native_ram)[0xB69A8] == 0x11 && !snapshots.take(0x800BF400),
            "immutable consumer snapshot or one-time ownership lost");
#if defined(_WIN32)
        _putenv_s("LAMBO_RT_CAPTURE_DIR", "");
#else
        unsetenv("LAMBO_RT_CAPTURE_DIR");
#endif
        lambo::rt::TaskSunProbes compactSnapshots;
        write(ram, 0x800A2BFCu, 0x800BF240u);
        write(ram, 0x800B69A8u, uint16_t(0x42));
        write(ram, 0x800B69B0u, uint32_t(0x8013D3C8u));
        write(ram, 0x800B6A00u, int16_t(7));
        write(ram, 0x800B69B6u, int16_t(13));
        for (unsigned i = 0; i < 59; ++i) {
            require(compactSnapshots.begin(ram.data(), ram.size()), "compact snapshot sampling setup failed");
        }
        require(compactSnapshots.begin(ram.data(), ram.size()), "compact snapshot begin rejected");
        require(compactSnapshots.snapshot(ram.data(), ram.size()), "compact producer metadata rejected");
        write(ram, 0x800B69A8u, uint16_t(0));
        write(ram, 0x800B69B6u, int16_t(-1));
        const auto compact = compactSnapshots.take(0x800BF400u);
        require(compact && compact->objects_complete && !compact->native_ram &&
            compact->objects[0].flags == 0x42 && compact->objects[0].list == 0x8013D3C8u &&
            compact->objects[0].parent == 7 && compact->objects[0].kind == 13,
            "object identity was not copied into the task value");
        lambo::rt::TaskSunProbes sceneSelections;
        write(ram, 0x800CE6B4u, int16_t(0));
        write(ram, 0x800CE7E8u, int16_t(3));
        require(sceneSelections.begin(ram.data(), ram.size()), "scene identity setup failed");
        const auto trial = sceneSelections.take(0x800BF400u);
        require(trial && trial->race_mode == 0 && trial->model_cursors[0] == 3,
            "scene selectors missing from owned task values");
        require(sceneSelections.begin(ram.data(), ram.size()), "pending old mode setup failed");
        write(ram, 0x800A2BFCu, 0x800C6C90u);
        write(ram, 0x800CE6B4u, int16_t(2));
        require(sceneSelections.begin(ram.data(), ram.size()), "race mode transition failed");
        require(!sceneSelections.take(0x800BF400u), "old mode task survived scene transition");
        const auto race = sceneSelections.take(0x800C6E50u);
        require(race && race->epoch > trial->epoch && race->race_mode == 2 && trial->race_mode == 0,
            "mode transition changed consumed identity or failed to advance epoch");
        write(ram, 0x800CE7E8u, int16_t(7));
        require(sceneSelections.begin(ram.data(), ram.size()), "model transition failed");
        const auto model = sceneSelections.take(0x800C6E50u);
        require(model && model->epoch > race->epoch && model->model_cursors[0] == 7,
            "model transition failed to invalidate the scene epoch");
        write(ram, 0x800CE6A4u, int16_t(2));
        require(sceneSelections.begin(ram.data(), ram.size()), "player count transition failed");
        const auto twoPlayers = sceneSelections.take(0x800C6E50u);
        require(twoPlayers && twoPlayers->epoch > model->epoch, "player count failed to advance scene epoch");
        require(sceneSelections.begin(ram.data(), ram.size()), "publication mismatch setup failed");
        write(ram, 0x800CE6B4u, int16_t(0));
        require(!sceneSelections.snapshot(ram.data(), ram.size()), "mixed scene snapshot admitted");
        const auto mixedScene = sceneSelections.take(0x800C6E50u);
        require(mixedScene && !mixedScene->emitters_complete && !mixedScene->objects_complete &&
            !mixedScene->native_ram, "mixed scene remained usable as proof");
        RT64::SunShadowParams params;
        require(!RT64::validSunShadowParams(params), "default parameters enabled shadows");
        params.valid = 1;
        params.rayMax = 20;
        require(RT64::validSunShadowParams(params), "bounded light rejected");
        for (float angle : {-0.1f, 0.1f, std::numeric_limits<float>::quiet_NaN()}) {
            params.angularRadius = angle;
            require(!RT64::validSunShadowParams(params), "invalid angular extent admitted");
        }
        params.angularRadius = 0.08f;
        params.sampleCount = 3;
        require(!RT64::validSunShadowParams(params), "unknown quality admitted");
        params.sampleCount = 16;
        params.direction[2] = 2;
        require(!RT64::validSunShadowParams(params), "nonunit direction admitted");
        params.direction[2] = 1;
        params.rayMin = params.rayMax;
        require(!RT64::validSunShadowParams(params), "empty ray interval admitted");
        params.rayMin = 0;
        params.originBias = params.rayMax;
        require(!RT64::validSunShadowParams(params), "unbounded bias admitted");
        params.originBias = 0.01f;
        params.strength = 1.1f;
        require(!RT64::validSunShadowParams(params), "strength outside [0,1] admitted");
        params.strength = 0.6f;
        require(RT64::validSunShadowParams(params), "supported settings rejected");
        std::puts("PASS: task isolation, swapped fields, invalidation and art-only provenance");
        return 0;
    } catch (const std::exception& error) {
        std::fprintf(stderr, "FAIL: %s\n", error.what());
        return 1;
    }
}
