#include "lambo_rt_shadows.h"
#include "shared/rt64_sun_shadow.h"

#include <cmath>
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
