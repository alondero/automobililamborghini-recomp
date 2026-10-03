#define HLSL_CPU
#include "lambo_rt_presented.h"

#include "hle/rt64_workload.h"
#include "shared/rt64_blender.h"

namespace lambo::rt {

ShadowMaterial shadow_material(const RT64::GameCall& game_call) {
    const auto& call = game_call.callDesc;
    const auto& shader = game_call.shaderDesc;
    const auto& mode = call.otherMode;
    const auto& fog = call.rdpParams.fogColor;
    return {mode.L, mode.H, shader.otherMode.L, shader.otherMode.H,
        call.colorCombiner.L, call.colorCombiner.H, shader.flags.value,
        mode.alphaCompare(), mode.zMode(), mode.zSource(),
        call.extendedType != RT64::DrawExtendedType::None,
        mode.cvgXAlpha() != 0, interop::Blender::usesAlphaBlend(mode), mode.forceBlend() != 0,
        mode.zCmp() != 0, mode.zUpd() != 0, interop::Blender::usesStandardFogCycle(mode),
        {fog.x, fog.y, fog.z, fog.w}};
}

ShadowProjection shadow_projection(const RT64::Workload& workload, const RT64::Projection& projection,
        const RT64::GameCall& game_call) {
    static_assert(uint32_t(RT64::Projection::Type::Perspective) == 1 &&
        uint32_t(RT64::Projection::Type::Orthographic) == 2 &&
        uint32_t(RT64::Projection::Type::Rectangle) == 3 && G_EX_ASPECT_BACKDROP == 3);
    ShadowProjection result;
    result.type = uint32_t(projection.type);
    result.geometry = game_call.callDesc.geometryMode;
    result.rectangleShader = game_call.shaderDesc.flags.rect;
    const auto& data = workload.drawData;
    if (projection.transformsIndex < data.viewProjTransformGroups.size()) {
        const uint32_t group = data.viewProjTransformGroups[projection.transformsIndex];
        if (group < data.transformGroups.size()) result.aspectMode = data.transformGroups[group].aspectMode;
    }
    if (projection.transformsIndex < data.viewProjTransformPhysicalAddresses.size())
        result.physicalAddress = data.viewProjTransformPhysicalAddresses[projection.transformsIndex];
    return result;
}

namespace {
bool uniform_presented_group(const RT64::Workload& workload, uint32_t first, uint32_t count,
        uint32_t& matrix_id) {
    const auto& indices = workload.drawData.faceIndices;
    if (count == 0 || first > indices.size() || count > indices.size() - first) return false;
    uint32_t initial = 0;
    if (!presented_group(workload, indices[first], initial)) return false;
    for (uint32_t i = first + 1; i < first + count; ++i) {
        uint32_t current = 0;
        if (!presented_group(workload, indices[i], current) || current != initial) return false;
    }
    matrix_id = initial;
    return true;
}
} // namespace

bool presented_group(const RT64::Workload& workload, uint32_t vertex, uint32_t& matrix_id) {
    const auto& draw = workload.drawData;
    if (vertex >= draw.worldIndices.size()) return false;
    const uint32_t world = draw.worldIndices[vertex];
    if (world >= draw.worldTransformGroups.size()) return false;
    const uint32_t group = draw.worldTransformGroups[world];
    if (group >= draw.transformGroups.size()) return false;
    matrix_id = draw.transformGroups[group].matrixId;
    uint32_t object_id = 0;
    return presented_object_id(matrix_id, object_id);
}

PresentedScene presented_scene(const RT64::Workload& workload) {
    PresentedScene scene;
    const auto& data = workload.drawData;
    for (uint32_t f = 0; f < workload.fbPairCount; ++f) {
        const auto& fb = workload.fbPairs[f];
        for (uint32_t p = 0; p < fb.projectionCount; ++p) {
            const auto& projection = fb.projections[p];
            const bool indexed = projection.type == RT64::Projection::Type::Perspective ||
                projection.type == RT64::Projection::Type::Orthographic;
            for (uint32_t c = 0; c < projection.gameCallCount; ++c) {
                const auto& game_call = projection.gameCalls[c];
                PresentedDraw draw;
                draw.call = game_call.callDesc.callIndex;
                draw.projection_type = uint32_t(projection.type);
                draw.indexed = indexed;
                draw.projection = shadow_projection(workload, projection, game_call);
                draw.material = shadow_material(game_call);
                if (indexed) {
                    draw.first = game_call.meshDesc.faceIndicesStart;
                    draw.wide_count = uint64_t(game_call.callDesc.triangleCount) * 3;
                    if (draw.wide_count <= data.faceIndices.size()) {
                        const uint32_t count = uint32_t(draw.wide_count);
                        draw.face_range_valid = draw.first <= data.faceIndices.size() &&
                            count <= data.faceIndices.size() - draw.first;
                        if (draw.face_range_valid)
                            draw.uniform_group = uniform_presented_group(workload, draw.first, count, draw.matrix_id);
                    }
                }
                scene.draws.push_back(draw);
            }
        }
    }
    if (data.lightIndices.size() != data.lightCounts.size()) return scene;
    for (size_t vertex = 0; vertex < data.lightIndices.size(); ++vertex) {
        uint32_t matrix = 0;
        if (!presented_group(workload, uint32_t(vertex), matrix)) continue;
        const uint32_t start = data.lightIndices[vertex];
        const uint32_t count = data.lightCounts[vertex];
        if (start > data.rspLights.size() || count > data.rspLights.size() - start) continue;
        for (uint32_t i = start; i < start + count; ++i) {
            const auto& light = data.rspLights[i];
            const PresentedLight value{matrix & 0xFFFFu, light.posDir.x, light.posDir.y, light.posDir.z,
                light.kc == 0 && light.kl == 0 && light.kq == 0};
            const bool repeated = !scene.lights.empty() && scene.lights.back().object_id == value.object_id &&
                scene.lights.back().x == value.x && scene.lights.back().y == value.y &&
                scene.lights.back().z == value.z && scene.lights.back().directional == value.directional;
            if (!repeated) scene.lights.push_back(value);
        }
    }
    return scene;
}

bool shadow_task(const TaskSunProbe& probe, ShadowTask& task) {
    if (!probe.objects_complete) return false;
    task.epoch = probe.epoch;
    task.sequence = probe.sequence;
    task.circuit = probe.circuit;
    task.phase = probe.phase;
    task.players = probe.players;
    task.race_mode = probe.race_mode;
    task.model_cursors = probe.model_cursors;
    task.emitters_complete = probe.emitters_complete;
    task.objects.assign(probe.objects.begin(), probe.objects.end());
    return true;
}

} // namespace lambo::rt
