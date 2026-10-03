// ROM-free GPU validation of the reusable AS builder and shared HLSL kernel.
// The raster receiver integration is deliberately gated; this is not a game capture.
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <limits>
#include <stdexcept>
#include <vector>

#include "plume_d3d12.h"
#include <directx/d3d12sdklayers.h>
#include "plume_render_interface_builders.h"
#include "render/rt64_render_worker.h"
#include "render/rt64_sun_shadow_scene.h"
#include "shared/rt64_sun_shadow.h"

using namespace plume;
using namespace RT64;
void nativeMaterialProbe(RenderDevice* device, RenderWorker& worker, const char* shaderPath,
    const char* rasterVsPath, const char* rasterPsPath);
namespace {
size_t checks = 0;
int debugErrorCount(ID3D12InfoQueue* messages) {
    int errors = 0;
    if (!messages) return errors;
    for (UINT64 i = 0; i < messages->GetNumStoredMessagesAllowedByRetrievalFilter(); ++i) {
        SIZE_T size = 0;
        messages->GetMessage(i, nullptr, &size);
        std::vector<char> bytes(size);
        auto* message = reinterpret_cast<D3D12_MESSAGE*>(bytes.data());
        if (SUCCEEDED(messages->GetMessage(i, message, &size)) && message->Severity <= D3D12_MESSAGE_SEVERITY_ERROR) {
            std::fprintf(stderr, "D3D12 validation: %s\n", message->pDescription);
            ++errors;
        }
    }
    return errors;
}
void require(bool value, const char *why) {
    ++checks;
    if (!value) throw std::runtime_error(why);
}
void close(float actual, float expected, const char *why, float tolerance = 1e-5f) {
    require(std::isfinite(actual) && std::abs(actual - expected) <= tolerance, why);
}
void upload(RenderBuffer *buffer, const void *data, size_t size) {
    const RenderRange noRead(0, 0);
    void *mapped = buffer->map(0, &noRead);
    require(mapped != nullptr, "upload mapping failed");
    std::memcpy(mapped, data, size);
    const RenderRange written(0, size);
    buffer->unmap(0, &written);
}
struct Probe {
    float position[3];
    uint32_t receiverDraw = UINT32_MAX;
    float normal[3] = {0, 0, 1};
    uint32_t receiverFace = UINT32_MAX;
    float native[4] = {0.8f, 0.6f, 0.4f, 0.7f};
    float fog[3] = {0.1f, 0.2f, 0.3f};
    uint32_t reserved = 0;
};
struct Result {
    float color[4], info[4], directionStrength[4], radiusIntervalBias[4];
    uint32_t flags[4];
};
static_assert(sizeof(Probe) == 64 && sizeof(Result) == 80);
using Vertex = std::array<float, 4>;

// Exercise the placed-footprint destination that a real swapchain capture uses.
// A buffer destination has no RenderTexture: pinned Plume dereferenced null here.
void textureReadback(RenderWorker& worker) {
    constexpr uint32_t width = 7, height = 3, rowBytes = 256;
    auto texture = worker.device->createTexture(RenderTextureDesc::ColorTarget(width, height, RenderFormat::B8G8R8A8_UNORM));
    const RenderTexture* source = texture.get();
    auto framebuffer = worker.device->createFramebuffer(RenderFramebufferDesc(&source, 1));
    auto buffer = worker.device->createBuffer(RenderBufferDesc::ReadbackBuffer(rowBytes * height));
    require(texture && framebuffer && buffer, "texture readback allocation failed");
    auto* list = worker.commandList.get();
    list->begin();
    list->barriers(RenderBarrierStage::GRAPHICS, RenderTextureBarrier(texture.get(), RenderTextureLayout::COLOR_WRITE));
    list->setFramebuffer(framebuffer.get());
    list->clearColor(0, RenderColor(0.6f, 0.4f, 0.2f, 1.0f));
    list->barriers(RenderBarrierStage::COPY, RenderTextureBarrier(texture.get(), RenderTextureLayout::COPY_SOURCE));
    list->copyTextureRegion(RenderTextureCopyLocation::PlacedFootprint(buffer.get(), RenderFormat::B8G8R8A8_UNORM,
        width, height, 1, rowBytes / 4), RenderTextureCopyLocation::Subresource(texture.get()));
    list->end();
    worker.execute();
    worker.wait();
    const RenderRange read(0, rowBytes * height);
    const auto* bytes = static_cast<const uint8_t*>(buffer->map(0, &read));
    require(bytes, "texture readback map failed");
    const std::array<uint8_t, 4> expected{51, 102, 153, 255};
    for (uint32_t y = 0; y < height; ++y) {
        for (uint32_t x = 0; x < width; ++x) {
            require(std::memcmp(bytes + rowBytes*y + 4*x, expected.data(), 4) == 0,
                "BGRA pixel or aligned row pitch mismatch");
        }
    }
    const RenderRange noWrite(0, 0);
    buffer->unmap(0, &noWrite);
}

// Independent dense polar-area integration over a disc emitter for a planar
// rectangular caster. This does not reimplement the production ray sampler.
float referenceVisibility(float x, float height, float radius, float bias) {
    constexpr int radial = 64, azimuthal = 128;
    int unblocked = 0;
    for (int r = 0; r < radial; ++r) {
        const double cosTheta = 1 - (r + 0.5) / radial * (1 - std::cos(double(radius)));
        const double slope = std::sqrt(1 - cosTheta*cosTheta) / cosTheta;
        for (int a = 0; a < azimuthal; ++a) {
            const double phi = (a + 0.5) / azimuthal * 6.283185307179586;
            const double hitX = x + (height - bias) * slope * std::cos(phi);
            const double hitY = (height - bias) * slope * std::sin(phi);
            unblocked += !(hitX >= -1 && hitX <= 1 && hitY >= -1 && hitY <= 1);
        }
    }
    return float(unblocked) / (radial * azimuthal);
}
}

int main(int argc, char **argv) {
    try {
        require(argc == 2 || argc == 3 || argc == 5,
            "Usage: lambo_rt_shadow_gpu sun-shadow-probe.dxil [native-material-probe.dxil [native-vs.dxil native-ps.dxil]]");
        std::ifstream input(argv[1], std::ios::binary);
        std::vector<char> code((std::istreambuf_iterator<char>(input)), {});
        require(!code.empty(), "shader missing");
        ID3D12Debug *debug = nullptr;
        const bool debugLayer = SUCCEEDED(D3D12GetDebugInterface(IID_ID3D12Debug,
            reinterpret_cast<void **>(&debug)));
        if (debugLayer) { debug->EnableDebugLayer(); debug->Release(); }
        D3D12Interface api;
        require(api.isValid(), "D3D12 interface unavailable");
        auto device = api.createDevice("");
        require(device != nullptr, "D3D12 device unavailable");
        auto *native = static_cast<D3D12Device *>(device.get());
        D3D12_FEATURE_DATA_D3D12_OPTIONS5 options{};
        D3D12_FEATURE_DATA_SHADER_MODEL model{D3D_SHADER_MODEL_6_5};
        // Do not trust Plume's cached SM6.0 or silently exercise WARP.
        DXGI_ADAPTER_DESC1 adapter{};
        require(SUCCEEDED(native->adapter->GetDesc1(&adapter)), "adapter query failed");
        if ((adapter.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) || !device->getCapabilities().raytracing ||
            FAILED(native->d3d->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS5, &options, sizeof(options))) ||
            options.RaytracingTier < D3D12_RAYTRACING_TIER_1_1 ||
            FAILED(native->d3d->CheckFeatureSupport(D3D12_FEATURE_SHADER_MODEL, &model, sizeof(model))) ||
            model.HighestShaderModel < D3D_SHADER_MODEL_6_5) {
            std::puts("SKIP: real D3D12 hardware with DXR1.1/SM6.5 required");
            return 77;
        }
        const auto &description = device->getDescription();
        std::printf("GPU: %s driver=0x%llX backend=D3D12 DXR1.1 SM6.5 debug=%s\n",
            description.name.c_str(), static_cast<unsigned long long>(description.driverVersion),
            debugLayer ? "enabled" : "unavailable");
        ID3D12InfoQueue *messages = nullptr;
        native->d3d->QueryInterface(IID_ID3D12InfoQueue, reinterpret_cast<void **>(&messages));
        const auto releaseMessages = [](ID3D12InfoQueue* queue) {
            if (!queue) return;
            if (std::uncaught_exceptions()) debugErrorCount(queue);
            queue->Release();
        };
        std::unique_ptr<ID3D12InfoQueue, decltype(releaseMessages)> messageOwner(messages, releaseMessages);
        if (messages) messages->ClearStoredMessages();

        constexpr uint32_t probeCount = 129;
        std::vector<Probe> probes(probeCount);
        for (uint32_t i = 0; i < probeCount; ++i) probes[i].position[0] = -1.6f + float(i) * 0.025f;
        std::vector<Vertex> vertices{{-1,-1,3,1}, {1,-1,3,1}, {1,1,3,1}, {-1,1,3,1},
            {-1,-1,1,1}, {1,-1,1,1}, {1,1,1,1}, {-1,1,1,1}};
        std::vector<uint32_t> indices{0,1,2,0,2,3,4,5,6,4,6,7};
        auto positions = device->createBuffer(RenderBufferDesc::DefaultBuffer(vertices.size() * 16,
            RenderBufferFlag::STORAGE | RenderBufferFlag::UNORDERED_ACCESS | RenderBufferFlag::ACCELERATION_STRUCTURE_INPUT));
        auto faces = device->createBuffer(RenderBufferDesc::DefaultBuffer(indices.size() * 4,
            RenderBufferFlag::STORAGE | RenderBufferFlag::ACCELERATION_STRUCTURE_INPUT));
        auto vertexUpload = device->createBuffer(RenderBufferDesc::UploadBuffer(vertices.size() * 16));
        auto indexUpload = device->createBuffer(RenderBufferDesc::UploadBuffer(indices.size() * 4));
        auto probeUpload = device->createBuffer(RenderBufferDesc::UploadBuffer(probes.size() * sizeof(Probe), RenderBufferFlag::STORAGE));
        auto output = device->createBuffer(RenderBufferDesc::DefaultBuffer(probes.size() * sizeof(Result),
            RenderBufferFlag::STORAGE | RenderBufferFlag::UNORDERED_ACCESS));
        auto readback = device->createBuffer(RenderBufferDesc::ReadbackBuffer(probes.size() * sizeof(Result)));
        require(positions && faces && vertexUpload && indexUpload && probeUpload && output && readback,
            "probe allocation failed");
        const SunShadowBuffer positionInput{positions.get(), vertices.size() * 16};
        const SunShadowBuffer indexInput{faces.get(), indices.size() * 4};
        RenderDescriptorSetBuilder descriptors;
        descriptors.begin();
        const auto asBinding = descriptors.addAccelerationStructure(0);
        const auto geometryBinding = descriptors.addStructuredBuffer(1);
        const auto probesBinding = descriptors.addStructuredBuffer(2);
        const auto resultsBinding = descriptors.addReadWriteStructuredBuffer(0);
        descriptors.end();
        auto set = descriptors.create(device.get());
        RenderPipelineLayoutBuilder pipelineLayout;
        pipelineLayout.begin();
        pipelineLayout.addPushConstant(0, 0, sizeof(SunShadowParams), RenderShaderStageFlag::COMPUTE);
        pipelineLayout.addDescriptorSet(descriptors.descriptorSetDesc);
        pipelineLayout.end();
        auto layout = pipelineLayout.create(device.get());
        auto shader = device->createShader(code.data(), code.size(), "CSMain", RenderShaderFormat::DXIL);
        require(set && layout && shader, "probe descriptors/shader failed");
        auto pipeline = device->createComputePipeline(RenderComputePipelineDesc(layout.get(), shader.get(), 64, 1, 1));
        require(pipeline != nullptr, "SM6.5 pipeline creation failed");
        const RenderBufferStructuredView probesView(sizeof(Probe)), resultsView(sizeof(Result)), geometryView(16);
        set->setBuffer(probesBinding, probeUpload.get(), probes.size() * sizeof(Probe), &probesView);
        set->setBuffer(resultsBinding, output.get(), probes.size() * sizeof(Result), &resultsView);
        RenderWorker worker(device.get(), "Sunlight shadow GPU probe", RenderCommandListType::DIRECT);
        textureReadback(worker);
        if (argc >= 3) nativeMaterialProbe(device.get(), worker, argv[2],
            argc == 5 ? argv[3] : nullptr, argc == 5 ? argv[4] : nullptr);
        auto timestamps = device->createQueryPool(3);
        require(timestamps != nullptr, "GPU timestamp support unavailable");
        std::vector<double> buildTimes, kernelTimes;
        SunShadowScene scene;
        SunShadowParams params;
        params.valid = 1;
        params.strength = 0.6f;
        params.rayMin = 0.01f;
        params.rayMax = 20;
        params.originBias = 0.005f;
        uint64_t presentation = 0;
        auto execute = [&](const std::vector<SunShadowRange> &ranges, bool validParams = true) {
            require(validSunShadowParams(params) == validParams, "parameter validator disagrees");
            ++presentation;
            require(scene.prepare(device.get(), positionInput, indexInput, uint32_t(vertices.size()), indices,
                ranges, presentation), scene.error().c_str());
            require(!scene.ready(presentation) && !scene.accelerationStructure(presentation),
                "unbuilt scene exposed as ready");
            upload(vertexUpload.get(), vertices.data(), vertices.size() * 16);
            upload(indexUpload.get(), indices.data(), indices.size() * 4);
            upload(probeUpload.get(), probes.data(), probes.size() * sizeof(Probe));
            auto *commands = worker.commandList.get();
            commands->begin();
            const RenderBufferBarrier copy[]{{positions.get(), RenderBufferAccess::WRITE}, {faces.get(), RenderBufferAccess::WRITE}};
            commands->barriers(RenderBarrierStage::COPY, copy, 2);
            commands->copyBufferRegion(positions->at(0), vertexUpload->at(0), vertices.size() * 16);
            commands->copyBufferRegion(faces->at(0), indexUpload->at(0), indices.size() * 4);
            commands->resetQueryPool(timestamps.get(), 0, 3);
            commands->writeTimestamp(timestamps.get(), 0);
            require(scene.recordBuild(commands), "AS build failed");
            commands->writeTimestamp(timestamps.get(), 1);
            require(scene.ready(presentation) && !scene.ready(presentation - 1), "stale presentation admitted");
            require(!scene.recordBuild(commands), "duplicate build admitted");
            set->setAccelerationStructure(asBinding, scene.accelerationStructure(presentation));
            set->setBuffer(geometryBinding, scene.metadata(presentation), scene.metadataBytes(), &geometryView);
            commands->barriers(RenderBarrierStage::COMPUTE, RenderBufferBarrier(output.get(), RenderBufferAccess::WRITE));
            commands->setComputePipelineLayout(layout.get());
            commands->setPipeline(pipeline.get());
            commands->setComputeDescriptorSet(set.get(), 0);
            commands->setComputePushConstants(0, &params);
            commands->dispatch((probeCount + 63) / 64, 1, 1);
            commands->writeTimestamp(timestamps.get(), 2);
            commands->barriers(RenderBarrierStage::COPY, RenderBufferBarrier(output.get(), RenderBufferAccess::READ));
            commands->copyBufferRegion(readback->at(0), output->at(0), probes.size() * sizeof(Result));
            commands->end();
            worker.execute();
            worker.wait(); // All resource mutation below is after the actual GPU fence.
            timestamps->queryResults();
            const auto *ticks = timestamps->getResults();
            require(ticks && ticks[1] >= ticks[0] && ticks[2] >= ticks[1], "GPU timestamps unordered");
            // Plume queryResults converts device ticks to nanoseconds.
            buildTimes.push_back(double(ticks[1] - ticks[0]) / 1000);
            kernelTimes.push_back(double(ticks[2] - ticks[1]) / 1000);
            std::vector<Result> results(probeCount);
            const RenderRange read(0, results.size() * sizeof(Result));
            const void *mapped = readback->map(0, &read);
            require(mapped != nullptr, "GPU readback failed");
            std::memcpy(results.data(), mapped, results.size() * sizeof(Result));
            const RenderRange noWrite(0, 0);
            readback->unmap(0, &noWrite);
            for (const auto &result : results) {
                require(std::memcmp(result.directionStrength, &params, 16) == 0 &&
                    std::memcmp(result.radiusIntervalBias, reinterpret_cast<const char *>(&params) + 16, 16) == 0 &&
                    std::memcmp(result.flags, reinterpret_cast<const char *>(&params) + 32, 16) == 0,
                    "CPU/HLSL ABI mismatch");
                require(result.info[0] >= 0 && result.info[0] <= 1, "visibility out of bounds");
                require(result.info[1] >= std::cos(params.angularRadius) - 1e-5f && result.info[2] < 1e-5f,
                    "direction outside unit emitter cone");
            }
            return results;
        };
        const std::vector<SunShadowRange> ceiling{{0,6,17,true}};
        auto hard = execute({{0,6,17,true}, {UINT32_MAX,3,99,true}, {6,2,99,true}, {6,6,99,false},
            {0,6,99,true,1}, {0,6,99,true,0,1,true}});
        require(scene.stats().admitted == 1 && scene.stats().rejected == 5 && scene.stats().triangles == 2,
            "geometry admission/metadata changed");
        std::printf("AS: triangles=%llu bytes=%llu scratch=%llu\n",
            static_cast<unsigned long long>(scene.stats().triangles),
            static_cast<unsigned long long>(scene.stats().asBytes),
            static_cast<unsigned long long>(scene.stats().scratchBytes));
        for (uint32_t i = 0; i < probeCount; ++i) {
            const float x = probes[i].position[0];
            if (std::abs(std::abs(x) - 1) < 1e-4f) continue; // Triangle edge tolerance is driver-dependent.
            const float expected = std::abs(x) < 1 ? 0 : 1;
            close(hard[i].info[0], expected, "hard ray disagrees with analytic rectangle");
            const float transmission = 1 - params.strength * (1 - expected);
            for (int c = 0; c < 3; ++c) close(hard[i].color[c],
                transmission * probes[i].native[c] + (1-transmission) * probes[i].fog[c], "fog composition changed");
            close(hard[i].color[3], probes[i].native[3], "native alpha changed");
        }
        auto unsupportedAlpha = execute({{0,6,17,false,0,1,true}});
        for (uint32_t i = 0; i < probeCount; ++i) {
            close(unsupportedAlpha[i].info[0], 1, "opaque-only shader turned cutouts into solids");
            if (std::abs(probes[i].position[0]) < 1)
                require(unsupportedAlpha[i].info[3] == 0, "missing candidate evaluator was not reported");
        }
        for (uint32_t quality : {4u,8u,16u}) {
            params.sampleCount = quality;
            params.angularRadius = 0;
            auto zero = execute(ceiling);
            for (uint32_t i = 0; i < probeCount; ++i)
                require(std::memcmp(&zero[i], &hard[i], 32) == 0, "zero-radius hard parity failed");
            params.angularRadius = 0.08f;
            auto soft = execute(ceiling);
            int partial = 0;
            double absoluteError = 0;
            for (uint32_t i = 0; i < probeCount; ++i) {
                partial += soft[i].info[0] > 0 && soft[i].info[0] < 1;
                absoluteError += std::abs(soft[i].info[0] - referenceVisibility(probes[i].position[0], 3, params.angularRadius, params.originBias));
            }
            require(partial > 6, "soft mode retained a hard scenery early-out");
            require(absoluteError / probeCount < 0.04, "cone visibility disagrees with independent dense reference");
            require(soft[0].info[1] < 1 - 0.65f * (1 - std::cos(params.angularRadius)), "quality shrank the emitter");
            std::printf("Cone: %u rays partial=%d mean_abs_error=%.6f\n", quality, partial, absoluteError / probeCount);
            // Moving the same caster closer reduces the world-space penumbra.
            for (int v = 0; v < 4; ++v) vertices[v][2] = 1;
            auto nearResults = execute(ceiling);
            int nearPartial = 0;
            for (const auto &r : nearResults) nearPartial += r.info[0] > 0 && r.info[0] < 1;
            require(nearPartial < partial / 2, "penumbra does not grow with receiver/caster separation");
            for (int v = 0; v < 4; ++v) vertices[v][2] = 3;
        }
        params.angularRadius = 0;
        // Exclude only the exact receiving primitive: another triangle in the
        // same draw at a different depth must still shadow it.
        for (auto &p : probes) { p.position[0] = 0.4f; p.position[1] = -0.8f; p.receiverDraw = 17; p.receiverFace = 6; }
        auto self = execute({{6,6,17,true}});
        close(self[0].info[0], 1, "originating face was not excluded");
        auto closed = execute({{6,6,17,true},{0,6,17,true}});
        close(closed[0].info[0], 0, "whole receiver draw was excluded");
        for (auto &p : probes) p.normal[2] = -1;
        auto reversed = execute({{6,6,17,true},{0,6,17,true}});
        close(reversed[0].info[0], closed[0].info[0], "winding changed the geometric bias");
        // Exact tangency previously chose opposite origins for reversed normals.
        // One side of this caster edge blocks; the canonical +X side is clear.
        for (auto &p : probes) {
            p.position[0] = 1.002f; p.position[1] = 0;
            p.normal[0] = 1; p.normal[2] = 0;
            p.receiverDraw = p.receiverFace = UINT32_MAX;
        }
        auto tangent = execute(ceiling);
        close(tangent[0].info[0], 1, "canonical tangent bias crossed caster edge");
        for (auto &p : probes) p.normal[0] = -1;
        auto tangentReversed = execute(ceiling);
        for (uint32_t i = 0; i < probeCount; ++i)
            close(tangentReversed[i].info[0], tangent[i].info[0], "tangent winding changed visibility");
        for (auto &p : probes) {
            p.position[0] = 0.4f; p.position[1] = -0.8f;
            p.normal[0] = 0; p.normal[2] = -1;
        }
        for (auto &p : probes) for (int c = 0; c < 3; ++c) p.fog[c] = p.native[c];
        auto fogged = execute(ceiling);
        for (int c = 0; c < 4; ++c) close(fogged[0].color[c], probes[0].native[c], "fully fogged pixel changed");
        params.valid = 0;
        auto disabled = execute(ceiling, false);
        close(disabled[0].info[0], 1, "invalid light performed attenuation");
        for (int c = 0; c < 4; ++c) close(disabled[0].color[c], probes[0].native[c], "disabled pixel changed");
        params.valid = 1;
        params.strength = 1.5f;
        auto badStrength = execute(ceiling, false);
        close(badStrength[0].info[0], 1, "invalid strength still queried");
        for (int c = 0; c < 4; ++c) close(badStrength[0].color[c], probes[0].native[c], "invalid strength changed output");
        params.strength = 0.6f;
        params.direction[0] = 0.6f;
        params.direction[2] = 0.8f;
        auto angled = execute(ceiling);
        close(angled[0].info[0], 1, "world light direction did not move the hard shadow");
        params.direction[0] = 0;
        params.direction[2] = 1;
        for (auto &v : vertices) v[0] += 100;
        auto moved = execute(ceiling);
        close(moved[0].info[0], 1, "fenced rebuild retained previous geometry");
        auto smallPositions = device->createBuffer(RenderBufferDesc::DefaultBuffer(16,
            RenderBufferFlag::ACCELERATION_STRUCTURE_INPUT));
        auto smallIndices = device->createBuffer(RenderBufferDesc::DefaultBuffer(4,
            RenderBufferFlag::ACCELERATION_STRUCTURE_INPUT));
        require(smallPositions && smallIndices, "small input allocation failed");
        require(!scene.prepare(device.get(), positionInput, indexInput, UINT32_MAX, indices,
            ceiling, ++presentation), "oversized vertex count exceeded GPU allocation");
        require(!scene.ready(presentation - 1) && !scene.recordBuild(worker.commandList.get()),
            "capacity failure retained prior scene readiness");
        require(!scene.prepare(device.get(), {smallPositions.get(), 16}, indexInput,
            uint32_t(vertices.size()), indices, ceiling, ++presentation), "undersized position buffer admitted");
        require(scene.error() == "GPU input buffer capacity exceeded", "position capacity failure changed");
        require(!scene.prepare(device.get(), positionInput, {smallIndices.get(), 4},
            uint32_t(vertices.size()), indices, ceiling, ++presentation), "undersized index buffer admitted");
        require(scene.error() == "GPU input buffer capacity exceeded" && !scene.ready(presentation),
            "index capacity failure retained readiness");
        indices[0] = UINT32_MAX;
        require(!scene.prepare(device.get(), positionInput, indexInput, uint32_t(vertices.size()), indices,
            ceiling, ++presentation), "bad vertex index admitted");
        require(!scene.ready(presentation) && !scene.accelerationStructure(presentation - 1), "failed scene retained old readiness");
        require(!scene.recordBuild(worker.commandList.get()), "invalid scene recorded a build");
        require(!scene.prepare(device.get(), positionInput, indexInput, uint32_t(vertices.size()), indices, {},
            ++presentation), "empty scene admitted");
        params.direction[0] = std::numeric_limits<float>::infinity();
        require(!validSunShadowParams(params), "invalid direction admitted");
        require(debugErrorCount(messages) == 0, "D3D12 validation errors");
        const auto average = [](const std::vector<double>& values) {
            double sum = 0;
            for (double value : values) sum += value;
            return sum / values.size();
        };
        std::printf("Synthetic GPU timings (129 probes, cold/mixed cases): AS_mean_us=%.3f kernel_mean_us=%.3f; not game frame-time evidence\n",
            average(buildTimes), average(kernelTimes));
        std::printf("PASS: %zu checks; real AS/queries, ABI, angular coverage/separation, primitive exclusion, fog/alpha, fenced rebuild and invalidation\n", checks);
        return 0;
    } catch (const std::exception &error) {
        std::fprintf(stderr, "FAIL after %zu checks: %s\n", checks, error.what());
        return 1;
    }
}
