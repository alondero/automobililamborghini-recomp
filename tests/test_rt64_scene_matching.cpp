#include <cstdlib>
#include <iostream>
#include <memory>

#include "common/rt64_common.h"
#include "hle/rt64_workload_queue.h"

static void require(bool condition, const char* message) {
    if (!condition) {
        std::cerr << message << '\n';
        std::exit(1);
    }
}

int main() {
    auto queue = std::make_unique<RT64::WorkloadQueue>();
    const auto identity = hlslpp::float4x4::identity();
    for (unsigned frame = 0; frame < 2; ++frame) {
        auto& workload = queue->workloads[frame];
        workload.drawData = {};
        workload.fbPairs.resize(1);
        auto& pair = workload.fbPairs[0];
        pair.projections.resize(1);
        auto& projection = pair.projections[0];
        projection.transformsIndex = 0;
        projection.gameCallCount = 2;
        projection.gameCalls.resize(2);
        auto& data = workload.drawData;
        data.viewTransforms.push_back(identity);
        data.projTransforms.push_back(identity);
        data.viewProjTransforms.push_back(identity);
        data.viewProjTransformGroups.push_back(0);
        data.transformGroups.resize(3);
        for (unsigned object = 0; object < 2; ++object) {
            auto transform = identity;
            transform[3][0] = object ? 0.2f : -0.2f;
            data.worldTransforms.push_back(transform);
            data.worldTransformGroups.push_back(object + 1);
            data.worldTransformVertexIndices.push_back(0);
            auto& group = data.transformGroups[object + 1];
            group.matrixId = 0x10000001u + (frame ? 1 - object : object);
            group.tileInterpolation = G_EX_COMPONENT_SKIP;
            auto& call = projection.gameCalls[object];
            call.callDesc.minWorldMatrix = object;
            call.callDesc.maxWorldMatrix = object;
            call.callDesc.triangleCount = 12;
        }
    }

    RT64::GameFrame previous, current;
    RT64::GameScene previous_scene, current_scene;
    previous_scene.projections.push_back({0, 0, 0});
    current_scene.projections.push_back({1, 0, 0});
    previous.workloads = {0};
    current.workloads = {1};
    previous.perspectiveScenes = {previous_scene};
    current.perspectiveScenes = {current_scene};
    auto match = [&] {
        current.frameMap.clear();
        current.frameMap.workloads.resize(queue->workloads.size());
        auto& old_workload = queue->workloads[0];
        previous.buildTransformIdMap(old_workload, old_workload.transformIdMap,
            old_workload.transformIgnoredIds);
        bool upload = false, tiles = false, look_at = false;
        current.match(nullptr, *queue, previous, nullptr, upload, tiles, look_at);
        require(!upload, "Rigid matching unexpectedly uploaded vertex morph data");
    };

    // Camera movement changes which screen position is nearest. Record IDs must
    // beat proximity, including when the scene traversal order reverses.
    match();
    const auto& transforms = current.frameMap.workloads[1].transforms;
    require(transforms[0].mapped && transforms[0].prevTransformIndex == 1 &&
        transforms[1].mapped && transforms[1].prevTransformIndex == 0,
        "Parts crossed scene-record identities during camera movement");

    queue->workloads[1].drawData.transformGroups[1].matrixId = 0x10000003;
    match();
    require(!current.frameMap.workloads[1].transforms[0].mapped,
        "A newly visible object inherited another object's transform");

    // A pair of wheels belongs to one car. Their identities must remain ordinal
    // even when the anonymous score would prefer a sibling with a similar phase.
    previous.matched = true;
    previous.frameMap.workloads.resize(queue->workloads.size());
    auto& history = previous.frameMap.workloads[0];
    history.mapped = true;
    history.transforms.resize(2);
    history.viewProjections.resize(1);
    for (auto& wheel : history.transforms) {
        wheel.rigidBody.angularVelocity = std::acos(-1.0f);
    }
    for (auto frame : {0u, 1u}) {
        for (auto group : {1u, 2u}) {
            auto& properties = queue->workloads[frame].drawData.transformGroups[group];
            properties.matrixId = 0x10000001;
            properties.ordering = G_EX_ORDER_LINEAR;
        }
        auto opposite = identity;
        opposite[0][0] = opposite[2][2] = -1.0f;
        opposite[3][0] = 1.5f;
        queue->workloads[frame].drawData.worldTransforms[0] = identity;
        queue->workloads[frame].drawData.worldTransforms[1] = opposite;
    }
    match();
    require(current.frameMap.workloads[1].transforms[0].mapped &&
        current.frameMap.workloads[1].transforms[1].mapped &&
        current.frameMap.workloads[1].transforms[0].prevTransformIndex == 0 &&
        current.frameMap.workloads[1].transforms[1].prevTransformIndex == 1,
        "Wheels swapped within the same car");

    auto& changed = queue->workloads[1];
    changed.drawData.worldTransforms.pop_back();
    changed.drawData.worldTransformGroups.pop_back();
    changed.drawData.worldTransformVertexIndices.pop_back();
    changed.fbPairs[0].projections[0].gameCallCount = 1;
    match();
    require(!current.frameMap.workloads[1].transforms[0].mapped,
        "A changed part count shifted the remaining wheel's ordinal identity");

    auto reversed = identity;
    reversed[0][0] = reversed[2][2] = -1.0f;
    queue->workloads[1].drawData.viewTransforms[0] = reversed;
    queue->workloads[1].drawData.viewProjTransforms[0] = reversed;
    match();
    const auto& camera = current.frameMap.workloads[1].viewProjections[0].rigidBody;
    require(!camera.lerpRotation && !camera.lerpTranslation,
        "Scene matching interpolated through a front-to-rear camera cut");
    require(std::abs(float(hlslpp::determinant(
        camera.lerp(0.5f, identity, reversed, true))) - 1.0f) < 1e-5f,
        "Scene matching collapsed the camera halfway through a cut");
    for (auto frame : {0u, 1u}) {
        auto& projection_group = queue->workloads[frame].drawData.transformGroups[0];
        projection_group.matrixId = 0x20000001;
        projection_group.rotationInterpolation = G_EX_COMPONENT_INTERPOLATE;
    }
    match();
    require(current.frameMap.workloads[1].viewProjections[0].rigidBody.lerpRotation,
        "Anonymous camera policy overrode an explicit projection group's policy");
    std::cout << "RT64 scene matching preserves object identity and camera cuts\n";
}
