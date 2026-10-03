#define HLSL_CPU
#include "lambo_rt_evidence.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <map>
#include <mutex>
#include <set>
#include <stdexcept>

#include "contrib/json/json.hpp"
#include "hle/rt64_workload.h"
#include "render/rt64_render_worker.h"
#include "shared/rt64_blender.h"
#include "lambo_log.h"

namespace lambo::rt {
namespace {
using Json = nlohmann::json;
constexpr size_t capture_budget = 128 * 1024 * 1024;

bool flag(const char* name) {
    const char* value = std::getenv(name);
    return value && std::strcmp(value, "1") == 0;
}

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

void write_bytes(const std::filesystem::path& path, const void* bytes, size_t size) {
    require(size <= capture_budget, "capture byte budget exceeded");
    std::ofstream file(path, std::ios::binary);
    require(bool(file), "cannot open capture");
    file.write(static_cast<const char*>(bytes), static_cast<std::streamsize>(size));
    file.flush();
    require(bool(file), "cannot write capture");
}

void write_json(const std::filesystem::path& path, const Json& value) {
    const std::string bytes = value.dump(2) + '\n';
    write_bytes(path, bytes.data(), bytes.size());
}

std::vector<uint8_t> read_buffer(RT64::RenderWorker* worker, const RenderBuffer* source, size_t size,
        RenderBarrierStages restored_stages) {
    require(source && size && size <= capture_budget, "invalid GPU buffer extent");
    auto target = worker->device->createBuffer(RenderBufferDesc::ReadbackBuffer(size));
    require(bool(target), "readback allocation failed");
    auto* list = worker->commandList.get();
    list->begin();
    list->barriers(RenderBarrierStage::COPY, RenderBufferBarrier(const_cast<RenderBuffer*>(source), RenderBufferAccess::READ));
    list->copyBufferRegion(target->at(0), source->at(0), size);
    list->barriers(restored_stages, RenderBufferBarrier(const_cast<RenderBuffer*>(source), RenderBufferAccess::READ));
    list->end();
    worker->execute();
    worker->wait();
    RenderRange range(0, size);
    const void* bytes = target->map(0, &range);
    require(bytes != nullptr, "readback map failed");
    std::vector<uint8_t> result(size);
    std::memcpy(result.data(), bytes, size);
    RenderRange no_write(0, 0);
    target->unmap(0, &no_write);
    return result;
}

std::vector<uint8_t> read_owner_texture(RT64::RenderWorker* worker, const plume::RenderTexture* source,
        uint32_t width, uint32_t height) {
    require(source && width && height && width <= 4096 && height <= 4096, "invalid owner texture extent");
    const uint32_t row_bytes = width * 8;
    const uint32_t row_pitch = (row_bytes + 255) & ~255u;
    const size_t padded_size = size_t(row_pitch) * height;
    require(padded_size <= capture_budget, "owner texture exceeds capture byte budget");
    auto target = worker->device->createBuffer(RenderBufferDesc::ReadbackBuffer(padded_size));
    require(bool(target), "owner readback allocation failed");
    auto* list = worker->commandList.get();
    list->begin();
    list->barriers(RenderBarrierStage::COPY,
        RenderTextureBarrier(const_cast<plume::RenderTexture*>(source), RenderTextureLayout::COPY_SOURCE));
    list->copyTextureRegion(RenderTextureCopyLocation::PlacedFootprint(target.get(), RenderFormat::R32G32_UINT,
        width, height, 1, row_pitch / 8), RenderTextureCopyLocation::Subresource(source));
    list->barriers(RenderBarrierStage::GRAPHICS,
        RenderTextureBarrier(const_cast<plume::RenderTexture*>(source), RenderTextureLayout::COLOR_WRITE));
    list->end();
    worker->execute();
    worker->wait();
    RenderRange range(0, padded_size);
    const void* mapped = target->map(0, &range);
    require(mapped != nullptr, "owner readback map failed");
    std::vector<uint8_t> result(size_t(row_bytes) * height);
    for (uint32_t y = 0; y < height; ++y) {
        std::memcpy(result.data() + size_t(y) * row_bytes,
            static_cast<const uint8_t*>(mapped) + size_t(y) * row_pitch, row_bytes);
    }
    RenderRange no_write(0, 0);
    target->unmap(0, &no_write);
    return result;
}

Json material(const RT64::DrawCall& call) {
    const auto& om = call.otherMode;
    const auto& cc = call.colorCombiner;
    Json alpha = Json::array();
    for (uint32_t c = 0; c < cc.cycleCount(om); ++c) {
        alpha.push_back(cc.cycleAlphaText(c));
    }
    // RT64's cc.L is native command word 0; cc.H is word 1.
    return {{"other_hi", om.H}, {"other_lo", om.L}, {"combine_w0", cc.L}, {"combine_w1", cc.H},
        {"geometry", call.geometryMode}, {"alpha_cycles", alpha},
        {"alpha_compare", om.alphaCompare()}, {"coverage_times_alpha", om.cvgXAlpha()},
        {"alpha_coverage_select", om.alphaCvgSel()}, {"force_blend", om.forceBlend()},
        {"alpha_blend", interop::Blender::usesAlphaBlend(om)},
        {"standard_fog", interop::Blender::usesStandardFogCycle(om)},
        {"z_compare", om.zCmp()}, {"z_update", om.zUpd()}, {"z_mode", om.zMode()},
        {"z_source", om.zSource()}, {"texture_on", call.textureOn},
        {"tile_start", call.tileIndex}, {"tile_count", call.tileCount},
        {"prim_alpha", call.rdpParams.primColor.w}, {"env_alpha", call.rdpParams.envColor.w},
        {"blend_alpha", call.rdpParams.blendColor.w},
        {"fog_rgba", {call.rdpParams.fogColor.x, call.rdpParams.fogColor.y,
            call.rdpParams.fogColor.z, call.rdpParams.fogColor.w}}};
}

enum PhysicalShadowRejection : uint32_t {
    RejectExtendedDraw = 1u << 0,
    RejectCombiner = 1u << 1,
    RejectOtherMode = 1u << 2,
    RejectDepthCompareMode = 1u << 3,
    RejectShaderModeMismatch = 1u << 4,
    RejectFogCycle = 1u << 5,
    RejectCoverageAlphaOrBlending = 1u << 6,
    RejectDepthBehavior = 1u << 7,
    RejectShaderFlags = 1u << 8,
    RejectFogColor = 1u << 9,
};

enum UnclassifiedShadowDraw : uint32_t {
    UnclassifiedProjection = 1,
    UnclassifiedFaceRange = 2,
    UnclassifiedTransformGroup = 3,
    UnclassifiedObjectIdentity = 4,
    UnclassifiedFaceCountOverflow = 5,
    UnclassifiedObjectRole = 6,
};

uint32_t physical_shadow_material_rejection(const RT64::GameCall& game_call) {
    const auto& call = game_call.callDesc;
    const auto& shader = game_call.shaderDesc;
    const auto& mode = call.otherMode;
    const auto& shader_mode = shader.otherMode;
    const auto& combine = call.colorCombiner;
    const bool measured_combine =
        (combine.L == 0xFC127FFFu && combine.H == 0xFFFFF238u) ||
        (combine.L == 0xFC26A004u && combine.H == 0x1FFC93F8u) ||
        (combine.L == 0xFC327FFFu && combine.H == 0xFFFFF838u) ||
        (combine.L == 0xFCFFFFFFu && combine.H == 0xFFFE7838u);
    const uint32_t unsupported_flags = (1u << 29) | (3u << 30) | 1u;
    const auto& fog = call.rdpParams.fogColor;
    uint32_t rejection = 0;
    if (game_call.callDesc.extendedType != RT64::DrawExtendedType::None) rejection |= RejectExtendedDraw;
    if (!measured_combine) rejection |= RejectCombiner;
    if (mode.L != 0xC8112078u && mode.L != 0xC8112230u) rejection |= RejectOtherMode;
    if (((mode.H >> 20) & 3u) != 1u) rejection |= RejectDepthCompareMode;
    if (shader_mode.L != mode.L || (shader_mode.H & ~63u) != (mode.H & ~63u)) rejection |= RejectShaderModeMismatch;
    if (!interop::Blender::usesStandardFogCycle(mode)) rejection |= RejectFogCycle;
    if (mode.alphaCompare() != 0 || mode.cvgXAlpha() || interop::Blender::usesAlphaBlend(mode) || mode.forceBlend())
        rejection |= RejectCoverageAlphaOrBlending;
    if (!mode.zCmp() || !mode.zUpd() || mode.zMode() != 0 || mode.zSource() != 0) rejection |= RejectDepthBehavior;
    if ((shader.flags.value & unsupported_flags) != 0) rejection |= RejectShaderFlags;
    if (!std::isfinite(fog.x) || !std::isfinite(fog.y) || !std::isfinite(fog.z) || !std::isfinite(fog.w) ||
        fog.x < 0 || fog.x > 1 || fog.y < 0 || fog.y > 1 || fog.z < 0 || fog.z > 1 || fog.w < 0 || fog.w > 1)
        rejection |= RejectFogColor;
    return rejection;
}

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

}

struct RenderEvidence::Impl {
    struct PendingOwner {
        uint32_t color_address = 0, width = 0, height = 0;
        const plume::RenderTexture* texture = nullptr;
        bool complete = false;
    };
    std::filesystem::path directory;
    bool drop_overlay = false;
    std::mutex mutex;
    // HLE installs only four producer-owned observations before fullSync publishes
    // their workload. Workers receive values; they never dereference guest RAM.
    std::map<uint64_t, Json> tasks;
    std::map<uint64_t, uint64_t> completed_tasks;
    std::set<uint64_t> rendered_tasks;
    const RT64::Workload* current = nullptr; // borrowed on the workload thread only
    Json report;
    bool capturing = false;
    std::vector<PendingOwner> ownerTextures;

    std::filesystem::path path(uint64_t sequence, const char* suffix) const {
        return directory / ("task-" + std::to_string(sequence) + suffix);
    }

    uint32_t group(uint32_t vertex) const {
        const auto& d = current->drawData;
        require(vertex < d.worldIndices.size(), "index outside vertices");
        const uint32_t world = d.worldIndices[vertex];
        require(world < d.worldTransformGroups.size(), "invalid world transform");
        const uint32_t g = d.worldTransformGroups[world];
        require(g < d.transformGroups.size(), "invalid transform group");
        return d.transformGroups[g].matrixId;
    }

    bool overlay(uint32_t first, uint32_t count, const RT64::DrawCall& call) const {
        // The USA car-child identity is measured from func_80013328. Each
        // circuit/mode still needs its own differential before any support
        // claim; this capture path never controls production suppression.
        if (report["native"]["players"] != 1 ||
            count != 48 || call.otherMode.L != 0xC8104A50u ||
            (call.colorCombiner.L & 0xFFFFFFu) != 0x11FFFFu || call.colorCombiner.H != 0xFFFFF238u ||
            (call.geometryMode & ~0x800000u) != 0x12005u) return false;
        const auto& indices = current->drawData.faceIndices;
        if (first > indices.size() || count > indices.size() - first) return false;
        uint32_t id = group(indices[first]);
        uint32_t object = 0;
        if (!presented_object_id(id, object)) return false;
        const auto& objects = report["native"]["objects"];
        if (object >= objects.size()) return false;
        const auto& obj = objects[object];
        const int parent = obj["parent"];
        if (obj["flags"] != 0x42 || obj["list"] != 0x8013D3C8u || parent < 0 ||
            static_cast<size_t>(parent) >= objects.size() || !(objects[parent]["flags"].get<uint32_t>() & 8)) return false;
        for (uint32_t i = first; i < first + count; ++i) {
            if (group(indices[i]) != id) return false;
        }
        return true;
    }
};

RenderEvidence::RenderEvidence() : impl_(std::make_unique<Impl>()) {
    const char* directory = std::getenv("LAMBO_RT_RENDER_CAPTURE_DIR");
    // Snapshots are required: no unowned/live-RAM identity path is allowed.
    if (directory && *directory && flag("LAMBO_RT_SUN_PROBE") && std::getenv("LAMBO_RT_CAPTURE_DIR")) {
        impl_->directory = directory;
        impl_->drop_overlay = flag("LAMBO_RT_EVIDENCE_DROP_OVERLAY");
        std::filesystem::create_directories(impl_->directory);
    }
}
RenderEvidence::~RenderEvidence() = default;
bool RenderEvidence::enabled() const { return !impl_->directory.empty(); }

std::shared_ptr<const RT64::SunShadowWorkload> RenderEvidence::sunShadow(const RT64::Workload& workload) noexcept {
    if (!enabled()) return {};
    try {
        Json task;
        {
            std::lock_guard<std::mutex> lock(impl_->mutex);
            const auto found = impl_->tasks.find(workload.workloadId);
            if (found == impl_->tasks.end()) {
                return {};
            }
            task = found->second;
        }
        const Json& native = task;
        const int circuit = native.at("circuit").get<int>();
        const int phase = native.at("phase").get<int>();
        const int players = native.at("players").get<int>();
        const int race_mode = native.at("race_mode").get<int>();
        const auto& models = native.at("model_cursors");
        // Player modes 0-3 share the car/light/view path. Mode 4 is attract;
        // docs/camera-sequences.md records the gate. Authenticate the task's
        // native key below; the harness's mode choices do not define lighting.
        if (circuit < 0 || circuit >= 6 || phase != 8 || players != 1 ||
            race_mode < 0 || race_mode > 3 || models.size() != 4 || models.at(0) != 0 ||
            !native.at("emitters_complete").get<bool>()) {
            LAMBO_LOG_INFO("rt-evidence", "sun shadow Workload gated: unsupported scene circuit=%d phase=%d players=%d mode=%d emitters=%d\n",
                circuit, phase, players, race_mode, native.at("emitters_complete").get<bool>());
            return {};
        }

        const Json& objects = native.at("objects");
        std::vector<TaskSunProbe::ObjectIdentity> object_identities;
        object_identities.reserve(objects.size());
        for (const auto& object : objects) {
            const uint32_t flags = object.at("flags").get<uint32_t>();
            const int parent = object.at("parent").get<int>();
            if (flags > std::numeric_limits<uint16_t>::max() ||
                parent < std::numeric_limits<int16_t>::min() ||
                parent > std::numeric_limits<int16_t>::max()) return {};
            object_identities.push_back({uint16_t(flags), object.at("list").get<uint32_t>(), int16_t(parent)});
        }

        // Each circuit keeps its own measured policy entry even where the
        // measured raw key matches. A Workload must contain the current task's
        // matching native direction; no camera or car matrix contributes here.
        // Keep one entry per circuit because future measurements may differ.
        struct CircuitPolicy { int x, y, z; };
        static constexpr std::array<CircuitPolicy, 6> policies{{
            {-11, 55, -101}, {-11, 55, -101}, {-11, 55, -101},
            {-11, 55, -101}, {-11, 55, -101}, {-11, 55, -101}
        }};
        const CircuitPolicy policy = policies[size_t(circuit)];
        const float length = std::sqrt(float(policy.x * policy.x + policy.y * policy.y + policy.z * policy.z));
        if (!std::isfinite(length) || length == 0) return {};
        bool native_key_used_by_lit_vertex = false;
        const auto& data = workload.drawData;
        if (data.lightIndices.size() != data.lightCounts.size()) return {};
        for (size_t vertex = 0; vertex < data.lightIndices.size(); ++vertex) {
            const uint32_t matrix = [&]() -> uint32_t {
                uint32_t value = 0;
                if (!presented_group(workload, uint32_t(vertex), value)) return 0;
                return value;
            }();
            if (matrix == 0) continue;
            const uint32_t object_id = matrix & 0xFFFFu;
            if (!physical_car_object(object_identities, object_id)) continue;
            const uint32_t start = data.lightIndices[vertex];
            const uint32_t count = data.lightCounts[vertex];
            if (start > data.rspLights.size() || count > data.rspLights.size() - start) continue;
            for (uint32_t i = start; i < start + count; ++i) {
                const auto& light = data.rspLights[i];
                if (std::abs(light.posDir.x - policy.x) < 1e-4f &&
                    std::abs(light.posDir.y - policy.y) < 1e-4f &&
                    std::abs(light.posDir.z - policy.z) < 1e-4f &&
                    (light.kc == 0 && light.kl == 0 && light.kq == 0)) {
                    native_key_used_by_lit_vertex = true;
                    break;
                }
            }
            if (native_key_used_by_lit_vertex) break;
        }
        if (!native_key_used_by_lit_vertex) {
            LAMBO_LOG_INFO("rt-evidence", "sun shadow Workload gated: circuit=%d key absent from lit physical-car vertices\n", circuit);
            return {};
        }

        auto result = std::make_shared<RT64::SunShadowWorkload>();
        result->sceneEpoch = native.at("epoch").get<uint64_t>();
        result->taskSequence = native.at("sequence").get<uint64_t>();
        result->circuit = uint32_t(circuit);
        result->phase = phase;
        result->playerCount = uint32_t(players);
        result->params.direction[0] = float(policy.x) / length;
        result->params.direction[1] = float(policy.y) / length;
        result->params.direction[2] = float(policy.z) / length;
        // Until native shadow contrast and emitter size are measured, this
        // capture-only payload cannot request visible attenuation.
        result->params.strength = 0.0f;
        result->params.angularRadius = 0.0f;
        result->params.rayMin = 0.01f;
        result->params.rayMax = 10000.0f;
        result->params.originBias = 0.005f;
        result->params.sampleCount = 8;
        result->params.valid = 1;
        result->authenticated = true;

        size_t admitted_faces = 0;
        size_t rejected_physical_calls = 0;
        size_t unclassified_draws = 0;
        size_t overlay_calls = 0;
        bool admission_complete = true;
        std::array<size_t, 10> rejection_reasons{};
        const auto mark_unclassified = [&](uint32_t first, uint32_t count, uint32_t draw, uint32_t reason,
                uint32_t projection_type) {
            result->unclassified.push_back({first, count, draw, false, reason, projection_type});
            admission_complete = false;
            ++unclassified_draws;
        };
        for (uint32_t f = 0; f < workload.fbPairCount; ++f) {
            const auto& fb = workload.fbPairs[f];
            for (uint32_t p = 0; p < fb.projectionCount; ++p) {
                const auto& projection = fb.projections[p];
                if (projection.type != RT64::Projection::Type::Perspective &&
                    projection.type != RT64::Projection::Type::Orthographic) {
                    for (uint32_t c = 0; c < projection.gameCallCount; ++c) {
                        const auto& game_call = projection.gameCalls[c];
                        // Non-indexed projections do not address faceIndices.
                        // Keep draw identity without inventing a face range from
                        // triangleCount or faceIndicesStart.
                        mark_unclassified(0, 0, game_call.callDesc.callIndex,
                            UnclassifiedProjection, uint32_t(projection.type));
                    }
                    continue;
                }
                for (uint32_t c = 0; c < projection.gameCallCount; ++c) {
                    const auto& game_call = projection.gameCalls[c];
                    const auto& call = game_call.callDesc;
                    const uint32_t first = game_call.meshDesc.faceIndicesStart;
                    const uint64_t wide_count = uint64_t(call.triangleCount) * 3;
                    if (wide_count > std::numeric_limits<uint32_t>::max()) {
                        mark_unclassified(first, 0, call.callIndex, UnclassifiedFaceCountOverflow,
                            uint32_t(projection.type));
                        continue;
                    }
                    const uint32_t count = uint32_t(wide_count);
                    if (count == 0) continue;
                    if (first > data.faceIndices.size() || count > data.faceIndices.size() - first) {
                        mark_unclassified(first, count, call.callIndex, UnclassifiedFaceRange,
                            uint32_t(projection.type));
                        continue;
                    }
                    uint32_t matrix = 0;
                    if (!uniform_presented_group(workload, first, count, matrix)) {
                        mark_unclassified(first, count, call.callIndex, UnclassifiedTransformGroup,
                            uint32_t(projection.type));
                        continue;
                    }
                    const uint32_t object_id = matrix & 0xFFFFu;
                    if (object_id >= objects.size()) {
                        mark_unclassified(first, count, call.callIndex, UnclassifiedObjectIdentity,
                            uint32_t(projection.type));
                        continue;
                    }
                    const Json& object = objects[object_id];
                    const uint32_t flags = object.at("flags").get<uint32_t>();
                    const bool world_builder = object_id == 0 && flags == 0x601u &&
                        object.at("list").get<uint32_t>() != 0;
                    const bool physical_car = physical_car_object(object_identities, object_id);
                    const bool overlay_material = count == 48 && call.otherMode.L == 0xC8104A50u &&
                        (call.colorCombiner.L & 0xFFFFFFu) == 0x11FFFFu &&
                        call.colorCombiner.H == 0xFFFFF238u && (call.geometryMode & ~0x800000u) == 0x12005u;
                    bool native_overlay = false;
                    if (overlay_material && flags == 0x42u && object.at("list").get<uint32_t>() == 0x8013D3C8u) {
                        const int parent = object.at("parent").get<int>();
                        native_overlay = parent >= 0 && size_t(parent) < objects.size() &&
                            (objects[size_t(parent)].at("flags").get<uint32_t>() & 8u) != 0;
                    }
                    if (native_overlay) {
                        result->overlays.push_back({first, count, call.callIndex, false, 0,
                            uint32_t(projection.type)});
                        ++overlay_calls;
                        continue;
                    }
                    if (!world_builder && !physical_car) {
                        mark_unclassified(first, count, call.callIndex, UnclassifiedObjectRole,
                            uint32_t(projection.type));
                        continue;
                    }
                    const uint32_t rejection = physical_shadow_material_rejection(game_call);
                    if (rejection != 0) {
                        result->rejected.push_back({first, count, call.callIndex, false, rejection,
                            uint32_t(projection.type)});
                        ++rejected_physical_calls;
                        for (uint32_t bit = 0; bit < rejection_reasons.size(); ++bit) {
                            if (rejection & (1u << bit)) ++rejection_reasons[bit];
                        }
                        continue;
                    }
                    result->geometry.push_back({first, count, call.callIndex, true, 0,
                        uint32_t(projection.type)});
                    admitted_faces += count / 3;
                }
            }
        }
        // This is only the metadata bridge. Until the raster receiver and the
        // per-view ready gate consume it, no caller may suppress native output.
        result->complete = admission_complete && admitted_faces != 0 && overlay_calls != 0 &&
            rejected_physical_calls == 0;
        if (!RT64::validSunShadowParams(result->params)) {
            LAMBO_LOG_INFO("rt-evidence", "sun shadow Workload rejected: invalid light parameters circuit=%d\n", circuit);
            return {};
        }
        if (!result->complete) {
            LAMBO_LOG_INFO("rt-evidence", "sun shadow Workload incomplete: circuit=%d faces=%zu overlays=%zu rejected=%zu unclassified=%zu reason_bits=[%zu,%zu,%zu,%zu,%zu,%zu,%zu,%zu,%zu,%zu]\n",
                circuit, admitted_faces, overlay_calls, rejected_physical_calls, unclassified_draws,
                rejection_reasons[0], rejection_reasons[1], rejection_reasons[2], rejection_reasons[3], rejection_reasons[4],
                rejection_reasons[5], rejection_reasons[6], rejection_reasons[7], rejection_reasons[8], rejection_reasons[9]);
        }
        return result;
    } catch (const std::exception& e) {
        LAMBO_LOG_INFO("rt-evidence", "shadow Workload rejected: %s\n", e.what());
        return {};
    }
}

bool RenderEvidence::ownerBufferEnabled() const noexcept {
    return enabled() && flag("LAMBO_RT_OWNER_BUFFER");
}

void RenderEvidence::ownerBufferIncomplete(uint32_t color_address, const char* reason,
        uint32_t identity) noexcept {
    if (!impl_->capturing || reason == nullptr) return;
    try {
        impl_->report["owner_incomplete_reasons"].push_back({{"color_address", color_address},
            {"reason", reason}, {"identity", identity}});
    } catch (const std::exception& e) {
        LAMBO_LOG_INFO("rt-evidence", "owner reason rejected: %s\n", e.what());
    }
}

void RenderEvidence::remember(uint64_t id, const std::optional<TaskSunProbe>& task) {
    if (!enabled() || !task || !task->native_ram || !task->objects_complete || !task->emitters_complete) return;
    try {
        Json objects = Json::array();
        for (const auto& object : task->objects) {
            objects.push_back({{"flags", object.flags}, {"list", object.list}, {"parent", object.parent}});
        }
        std::lock_guard lock(impl_->mutex);
        if (impl_->tasks.count(id)) throw std::runtime_error("duplicate workload mapping");
        while (impl_->tasks.size() >= 8) impl_->tasks.erase(impl_->tasks.begin());
        impl_->tasks.emplace(id, Json{{"epoch", task->epoch}, {"sequence", task->sequence},
            {"root", task->task_address + 0x1C0u}, {"phase", task->phase}, {"circuit", task->circuit},
            {"players", task->players}, {"emitters_complete", task->emitters_complete},
            {"race_mode", task->race_mode}, {"model_cursors", task->model_cursors},
            {"objects_complete", task->objects_complete}, {"objects", objects}});
    } catch (const std::exception& e) { LAMBO_LOG_INFO("rt-evidence", "task rejected: %s\n", e.what()); }
}

void RenderEvidence::begin(const RT64::Workload& w, float weight) noexcept {
    impl_->capturing = false;
    impl_->current = nullptr;
    impl_->ownerTextures.clear();
    try {
        Json native;
        {
            std::lock_guard lock(impl_->mutex);
            auto found = impl_->tasks.find(w.workloadId);
            if (found == impl_->tasks.end() || impl_->rendered_tasks.count(w.workloadId)) return;
            native = found->second;
        }
        const auto& d = w.drawData;
        require(d.vertexCount() <= 262144 && d.faceIndices.size() <= 262144 * 3, "geometry budget exceeded");
        require(weight == 1.0f, "diagnostic requires native presentation weight 1");
        Json calls = Json::array();
        for (uint32_t f = 0; f < w.fbPairCount; ++f) {
            const auto& fb = w.fbPairs[f];
            for (uint32_t p = 0; p < fb.projectionCount; ++p) {
                const auto& proj = fb.projections[p];
                for (uint32_t c = 0; c < proj.gameCallCount; ++c) {
                    const auto& call = proj.gameCalls[c];
                    const bool indexed = proj.type == RT64::Projection::Type::Perspective || proj.type == RT64::Projection::Type::Orthographic;
                    const uint64_t wide_index_count = indexed ? uint64_t(call.callDesc.triangleCount) * 3 : 0;
                    const uint64_t wide_raw_vertex_count = proj.type == RT64::Projection::Type::Triangle ?
                        uint64_t(call.callDesc.triangleCount) * 3 : 0;
                    require(wide_index_count <= std::numeric_limits<uint32_t>::max() &&
                        wide_raw_vertex_count <= std::numeric_limits<uint32_t>::max(), "call range count overflow");
                    calls.push_back({{"call", call.callDesc.callIndex}, {"first", indexed ? call.meshDesc.faceIndicesStart : 0},
                        {"count", uint32_t(wide_index_count)}, {"indexed", indexed},
                        {"triangle_count", call.callDesc.triangleCount},
                        {"raw_vertex_start", proj.type == RT64::Projection::Type::Triangle ?
                            Json(call.meshDesc.rawVertexStart) : Json(nullptr)},
                        {"raw_vertex_count", uint32_t(wide_raw_vertex_count)},
                        {"projection_type", int(proj.type)},
                        {"color_address", fb.colorImage.address}, {"shader_flags", call.shaderDesc.flags.value},
                        {"shader_other_lo", call.shaderDesc.otherMode.L}, {"shader_other_hi", call.shaderDesc.otherMode.H},
                        {"material", material(call.callDesc)}});
                }
            }
        }
        Json groups = Json::array();
        for (uint32_t g : d.worldTransformGroups) {
            require(g < d.transformGroups.size(), "invalid world transform group");
            groups.push_back(d.transformGroups[g].matrixId);
        }
        Json fog = Json::array();
        for (const auto& f : d.rspFog) fog.push_back({f.mul, f.offset});
        Json shadowRejected = Json::array();
        Json shadowUnclassified = Json::array();
        if (w.sunShadow) {
            for (const auto& range : w.sunShadow->rejected) {
                bool found = false;
                for (uint32_t f = 0; f < w.fbPairCount && !found; ++f) {
                    const auto& fb = w.fbPairs[f];
                    for (uint32_t p = 0; p < fb.projectionCount && !found; ++p) {
                        const auto& projection = fb.projections[p];
                        for (uint32_t c = 0; c < projection.gameCallCount; ++c) {
                            const auto& game_call = projection.gameCalls[c];
                            if (game_call.callDesc.callIndex != range.draw) continue;
                            shadowRejected.push_back({{"draw", range.draw}, {"first", range.faceStart},
                                {"count", range.indexCount}, {"reason", range.rejection},
                                {"projection_type", range.projectionType},
                                {"material", material(game_call.callDesc)},
                                {"shader_flags", game_call.shaderDesc.flags.value},
                                {"extended_type", int(game_call.callDesc.extendedType)}});
                            found = true;
                            break;
                        }
                    }
                }
                if (!found) shadowRejected.push_back({{"draw", range.draw}, {"first", range.faceStart},
                    {"count", range.indexCount}, {"reason", range.rejection},
                    {"projection_type", range.projectionType}, {"material", nullptr}});
            }
            for (const auto& range : w.sunShadow->unclassified) {
                bool found = false;
                for (uint32_t f = 0; f < w.fbPairCount && !found; ++f) {
                    const auto& fb = w.fbPairs[f];
                    for (uint32_t p = 0; p < fb.projectionCount && !found; ++p) {
                        const auto& projection = fb.projections[p];
                        for (uint32_t c = 0; c < projection.gameCallCount; ++c) {
                            const auto& game_call = projection.gameCalls[c];
                            if (game_call.callDesc.callIndex != range.draw) continue;
                            shadowUnclassified.push_back({{"draw", range.draw}, {"first", range.faceStart},
                                {"count", range.indexCount}, {"reason", range.rejection},
                                {"projection_type", range.projectionType},
                                {"material", material(game_call.callDesc)}});
                            found = true;
                            break;
                        }
                    }
                }
                if (!found) shadowUnclassified.push_back({{"draw", range.draw}, {"first", range.faceStart},
                    {"count", range.indexCount}, {"reason", range.rejection},
                    {"projection_type", range.projectionType}, {"material", nullptr}});
            }
        }
        impl_->report = {{"schema", 2}, {"native", native}, {"workload", w.workloadId}, {"weight", weight},
            {"drop_overlay", impl_->drop_overlay}, {"calls", calls}, {"raster", Json::array()},
            {"owner_buffers", Json::array()}, {"owner_incomplete_reasons", Json::array()},
            {"sun_shadow", w.sunShadow ? Json{{"authenticated", w.sunShadow->authenticated},
                {"complete", w.sunShadow->complete}, {"epoch", w.sunShadow->sceneEpoch},
                {"sequence", w.sunShadow->taskSequence}, {"circuit", w.sunShadow->circuit},
                {"phase", w.sunShadow->phase}, {"players", w.sunShadow->playerCount},
                {"params_abi_valid", RT64::validSunShadowParams(w.sunShadow->params)},
                {"geometry", w.sunShadow->geometry.size()}, {"overlays", w.sunShadow->overlays.size()},
                {"rejected", w.sunShadow->rejected.size()},
                {"unclassified", w.sunShadow->unclassified.size()}} : Json(nullptr)},
            {"sun_shadow_rejected_ranges", shadowRejected},
            {"sun_shadow_unclassified_ranges", shadowUnclassified},
            {"indices", d.faceIndices}, {"world_indices", d.worldIndices}, {"world_groups", groups},
            {"world_addresses", d.worldTransformPhysicalAddresses}, {"local_positions", d.posFloats},
            {"normal_color_bytes", d.normColBytes}, {"fog_indices", d.fogIndices}, {"fog_params", fog}, {"light_counts", d.lightCounts}};
        impl_->current = &w;
        impl_->capturing = true;
    } catch (const std::exception& e) { LAMBO_LOG_INFO("rt-evidence", "begin rejected: %s\n", e.what()); }
}

bool RenderEvidence::raster(const RT64::RasterEvidenceRange& range) noexcept {
    if (!impl_->capturing) return true;
    try {
        const auto [call, first, count, draw_index, indexed, test_z, scale, offset, scissor, resolution, viewport] = range;
        const RT64::DrawCall* desc = nullptr;
        for (uint32_t f = 0; f < impl_->current->fbPairCount && desc == nullptr; ++f) {
            const auto& fb = impl_->current->fbPairs[f];
            for (uint32_t p = 0; p < fb.projectionCount && desc == nullptr; ++p) {
                const auto& proj = fb.projections[p];
                for (uint32_t c = 0; c < proj.gameCallCount; ++c) {
                    if (proj.gameCalls[c].callDesc.callIndex == call) {
                        desc = &proj.gameCalls[c].callDesc;
                        break;
                    }
                }
            }
        }
        require(desc != nullptr, "raster call has no workload identity");
        const bool overlay = indexed && !test_z && impl_->overlay(first, count, *desc);
        const bool omit = impl_->drop_overlay && overlay;
        impl_->report["raster"].push_back({{"call", call}, {"first", first}, {"count", count}, {"draw_index", draw_index},
            {"indexed", indexed}, {"test_z", test_z}, {"overlay", overlay}, {"omitted", omit},
            {"screen_scale", {scale[0], scale[1]}}, {"screen_offset", {offset[0], offset[1]}},
            {"scissor", {scissor.left, scissor.top, scissor.right, scissor.bottom}},
            {"resolution", {resolution[0], resolution[1]}},
            {"viewport", {viewport.x, viewport.y, viewport.width, viewport.height}}});
        return !omit;
    } catch (const std::exception& e) {
        impl_->capturing = false;
        impl_->current = nullptr;
        impl_->ownerTextures.clear();
        LAMBO_LOG_INFO("rt-evidence", "raster rejected: %s\n", e.what());
        return true;
    }
}

void RenderEvidence::ownerBufferRendered(uint32_t color_address, uint32_t width, uint32_t height,
        const plume::RenderTexture* owner_texture, bool complete) noexcept {
    if (!impl_->capturing || owner_texture == nullptr) return;
    try {
        bool referenced = false;
        for (const auto& call : impl_->report["calls"]) {
            if (call["color_address"].get<uint32_t>() == color_address) {
                referenced = true;
                break;
            }
        }
        if (!referenced) return;
        if (!complete) {
            const bool has_reason = std::any_of(impl_->report["owner_incomplete_reasons"].begin(),
                impl_->report["owner_incomplete_reasons"].end(), [color_address](const Json& reason) {
                    return reason.value("color_address", 0u) == color_address;
                });
            if (!has_reason) ownerBufferIncomplete(color_address, "framebuffer_incomplete_unspecified", 0);
        }
        auto found = std::find_if(impl_->ownerTextures.begin(), impl_->ownerTextures.end(),
            [color_address](const Impl::PendingOwner& item) { return item.color_address == color_address; });
        if (found == impl_->ownerTextures.end()) {
            impl_->ownerTextures.push_back({color_address, width, height, owner_texture, complete});
        }
        else {
            if (found->texture != owner_texture || found->width != width || found->height != height) {
                found->complete = false;
                ownerBufferIncomplete(color_address, "owner_target_reused", 0);
            }
            found->color_address = color_address;
            found->width = width;
            found->height = height;
            found->texture = owner_texture;
            found->complete = found->complete && complete;
        }
    } catch (const std::exception& e) {
        LAMBO_LOG_INFO("rt-evidence", "owner target rejected: %s\n", e.what());
    }
}

void RenderEvidence::completed(const RT64::Workload& w, RT64::RenderWorker* worker) noexcept {
    if (!impl_->capturing || impl_->current != &w) return;
    impl_->capturing = false;
    impl_->current = nullptr;
    try {
        const auto& pos = w.outputBuffers.worldPosBuffer;
        const size_t bytes = size_t(w.drawData.vertexCount()) * 16;
        require(bytes <= pos.computedSize && bytes <= pos.allocatedSize, "presented world positions unavailable");
        const auto world = read_buffer(worker, pos.buffer.get(), bytes, RenderBarrierStage::GRAPHICS | RenderBarrierStage::COMPUTE);
        const auto& screen = w.outputBuffers.screenPosBuffer;
        require(bytes <= screen.computedSize && bytes <= screen.allocatedSize, "GPU screen positions unavailable");
        const auto screen_bytes = read_buffer(worker, screen.buffer.get(), bytes, RenderBarrierStage::GRAPHICS);
        const auto& shaded = w.outputBuffers.shadedColBuffer;
        require(bytes <= shaded.computedSize && bytes <= shaded.allocatedSize, "GPU shaded colors unavailable");
        const auto shade_bytes = read_buffer(worker, shaded.buffer.get(), bytes, RenderBarrierStage::GRAPHICS);
        const size_t index_bytes = w.drawData.faceIndices.size() * sizeof(uint32_t);
        require(index_bytes <= w.drawBuffers.faceIndicesBuffer.allocatedSize, "GPU index capacity mismatch");
        const auto faces = read_buffer(worker, w.drawBuffers.faceIndicesBuffer.get(), index_bytes, RenderBarrierStage::GRAPHICS);
        require(std::memcmp(faces.data(), w.drawData.faceIndices.data(), index_bytes) == 0, "GPU/CPU indices differ");
        const uint64_t sequence = impl_->report["native"]["sequence"];
        uint64_t owner_bytes_total = 0;
        for (uint32_t index = 0; index < impl_->ownerTextures.size(); ++index) {
            const auto& owner = impl_->ownerTextures[index];
            const auto owner_bytes = read_owner_texture(worker, owner.texture, owner.width, owner.height);
            owner_bytes_total += owner_bytes.size();
            require(owner_bytes_total <= capture_budget, "owner maps exceed capture byte budget");
            const std::string suffix = "-owner-" + std::to_string(index) + ".rg32ui";
            write_bytes(impl_->path(sequence, suffix.c_str()), owner_bytes.data(), owner_bytes.size());
            impl_->report["owner_buffers"].push_back({{"color_address", owner.color_address},
                {"width", owner.width}, {"height", owner.height}, {"row_bytes", owner.width * 8},
                {"file", "task-" + std::to_string(sequence) + suffix}, {"complete", owner.complete},
                {"layout", "draw_index_plus_one,primitive_id"}});
        }
        write_bytes(impl_->path(sequence, "-world.bin"), world.data(), world.size());
        write_bytes(impl_->path(sequence, "-screen.bin"), screen_bytes.data(), screen_bytes.size());
        write_bytes(impl_->path(sequence, "-shade.bin"), shade_bytes.data(), shade_bytes.size());
        impl_->report["world_bytes"] = bytes;
        impl_->report["gpu_indices_equal"] = true;
        write_json(impl_->path(sequence, "-render.json"), impl_->report);
        std::lock_guard lock(impl_->mutex);
        impl_->completed_tasks.emplace(w.workloadId, sequence);
        impl_->rendered_tasks.insert(w.workloadId);
    } catch (const std::exception& e) { LAMBO_LOG_INFO("rt-evidence", "completion rejected: %s\n", e.what()); }
    impl_->ownerTextures.clear();
}

void RenderEvidence::presented(const RT64::PresentationEvidence& presentation, RT64::RenderWorker* worker) noexcept {
    try {
        const auto workload = presentation.workloadId, present = presentation.presentId;
        const auto frame = presentation.frameIndex, frames = presentation.frameCount;
        const auto width = presentation.width, height = presentation.height;
        const auto* image = presentation.image;
        uint64_t sequence;
        {
            std::lock_guard lock(impl_->mutex);
            auto found = impl_->completed_tasks.find(workload);
            if (found == impl_->completed_tasks.end()) return;
            sequence = found->second;
            impl_->completed_tasks.erase(found);
        }
        require(frames == 1 && frame == 0, "swapchain evidence requires one native presentation");
        require(presentation.nativeColorImage, "presented image is not a native color target");
        require(width && height && width <= 4096 && height <= 4096, "swapchain extent exceeds budget");
        // RT64 Application fixes its swapchain format to B8G8R8A8_UNORM. This
        // diagnostic is registered only on the port's D3D12 backend. Copy after
        // its successful present/fence and before the present thread can reuse it.
        const uint32_t row = (width * 4 + 255) & ~255u;
        const size_t size = size_t(row) * height;
        auto target = worker->device->createBuffer(RenderBufferDesc::ReadbackBuffer(size));
        require(bool(target), "swapchain readback allocation failed");
        auto* list = worker->commandList.get();
        list->begin();
        list->barriers(RenderBarrierStage::COPY, RenderTextureBarrier(const_cast<RenderTexture*>(image), RenderTextureLayout::COPY_SOURCE));
        list->copyTextureRegion(RenderTextureCopyLocation::PlacedFootprint(target.get(), RenderFormat::B8G8R8A8_UNORM,
            width, height, 1, row / 4), RenderTextureCopyLocation::Subresource(image));
        list->barriers(RenderBarrierStage::NONE, RenderTextureBarrier(const_cast<RenderTexture*>(image), RenderTextureLayout::PRESENT));
        list->end();
        worker->execute();
        worker->wait();
        RenderRange range(0, size);
        const void* bytes = target->map(0, &range);
        require(bytes != nullptr, "swapchain map failed");
        write_bytes(impl_->path(sequence, "-swap.bgra"), bytes, size);
        RenderRange no_write(0, 0);
        target->unmap(0, &no_write);
        write_json(impl_->path(sequence, "-present.json"), {{"schema", 1}, {"workload", workload}, {"present", present},
            {"sequence", sequence}, {"frame", frame}, {"frames", frames}, {"width", width}, {"height", height},
            {"row_bytes", row}, {"format", "BGRA8"}, {"successful_present", true}, {"color_address", presentation.colorAddress},
            {"native_color_image", presentation.nativeColorImage}, {"filtering", presentation.filtering},
            {"video_resolution", {presentation.videoResolution[0], presentation.videoResolution[1]}},
            {"texture_extent", {presentation.textureWidth, presentation.textureHeight}},
            {"vi_viewport", {presentation.viViewport.x, presentation.viViewport.y, presentation.viViewport.width, presentation.viViewport.height}},
            {"vi_scissor", {presentation.viScissor.left, presentation.viScissor.top, presentation.viScissor.right, presentation.viScissor.bottom}}});
    } catch (const std::exception& e) { LAMBO_LOG_INFO("rt-evidence", "presentation rejected: %s\n", e.what()); }
}

void RenderEvidence::processed(uint64_t first, uint64_t last) {
    if (!enabled()) return;
    try {
        std::lock_guard lock(impl_->mutex);
        auto found = impl_->tasks.find(first);
        if (found == impl_->tasks.end()) return;
        write_json(impl_->path(found->second["sequence"], "-hle.json"), {{"first", first}, {"last", last},
            {"root", found->second["root"]}, {"epoch", found->second["epoch"]}, {"sequence", found->second["sequence"]}});
    } catch (const std::exception& e) { LAMBO_LOG_INFO("rt-evidence", "HLE mapping rejected: %s\n", e.what()); }
}
}
