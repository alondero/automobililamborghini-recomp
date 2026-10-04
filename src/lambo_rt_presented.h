#pragma once

#include <vector>

#include "lambo_rt_admission.h"

namespace RT64 {
struct Workload;
struct GameCall;
struct Projection;
}

namespace lambo::rt {

// Reduces the matching Workload's presented draws and lit-vertex light records
// to the typed admission inputs. Reads only Workload-owned CPU data on the
// queue thread that owns it; never guest RAM.
struct PresentedScene {
    std::vector<PresentedDraw> draws;
    std::vector<PresentedLight> lights;
};
PresentedScene presented_scene(const RT64::Workload& workload);

ShadowMaterial shadow_material(const RT64::GameCall& game_call);
ShadowProjection shadow_projection(const RT64::Workload& workload, const RT64::Projection& projection,
    const RT64::GameCall& game_call);
bool presented_group(const RT64::Workload& workload, uint32_t vertex, uint32_t& matrix_id);

// Converts a producer record. Returns false when it lacks complete identity.
bool shadow_task(const TaskSunProbe& probe, ShadowTask& task);

} // namespace lambo::rt
