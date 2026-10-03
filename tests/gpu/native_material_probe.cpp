// Uses native descriptor layouts and the production material evaluator. All
// textures/parameters are synthetic; native game alpha admission remains gated.
#define HLSL_CPU
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <type_traits>
#include <vector>

#include "render/rt64_descriptor_sets.h"
#include "render/rt64_render_worker.h"
#include "render/rt64_sun_shadow_scene.h"
#include "shared/rt64_f3d_defines.h"
#include "shared/rt64_framebuffer_params.h"
#include "shared/rt64_frame_params.h"
#include "shared/rt64_gpu_tile.h"
#include "shared/rt64_rdp_params.h"
#include "shared/rt64_rdp_tile.h"
#include "shared/rt64_render_indices.h"
#include "shared/rt64_render_params.h"
#include "shared/rt64_raster_params.h"
#include "plume_d3d12.h"
#include <directx/d3d12sdklayers.h>

using namespace plume;
using namespace RT64;
namespace {
struct MaterialProbe { float uv[2], dx[2], dy[2], alpha; uint32_t seed; };
struct MaterialResult { float values[4]; uint32_t identity[4]; float rayValues[4]; };
static_assert(sizeof(MaterialProbe) == 32 && sizeof(MaterialResult) == 48);

void require(bool value, const char* reason) {
    if (!value) throw std::runtime_error(reason);
}
void upload(RenderBuffer* buffer, const void* data, size_t size) {
    void* mapped = buffer->map();
    require(mapped != nullptr, "native material upload map failed");
    std::memcpy(mapped, data, size);
    const RenderRange written(0, size);
    buffer->unmap(0, &written);
}

// Suppress only the two errors intentionally produced by pipeline failure
// injection. The outer probe still reports every other D3D12 error, including
// errors from the subsequent successful native raster and query submissions.
struct ExpectedPipelineFailureMessages {
    ID3D12InfoQueue* queue = nullptr;
    explicit ExpectedPipelineFailureMessages(RenderDevice* device) {
        auto* native = static_cast<D3D12Device*>(device);
        native->d3d->QueryInterface(IID_ID3D12InfoQueue, reinterpret_cast<void**>(&queue));
        if (!queue) return;
        D3D12_MESSAGE_ID ids[]{D3D12_MESSAGE_ID_CREATEGRAPHICSPIPELINESTATE_MISSING_ROOT_SIGNATURE_FLAGS,
            D3D12_MESSAGE_ID_CREATECOMPUTEPIPELINESTATE_CS_ROOT_SIGNATURE_MISMATCH};
        D3D12_INFO_QUEUE_FILTER filter{};
        filter.DenyList.NumIDs = 2; filter.DenyList.pIDList = ids;
        if (FAILED(queue->PushStorageFilter(&filter))) {
            queue->Release(); queue = nullptr;
            throw std::runtime_error("expected pipeline failure filter unavailable");
        }
    }
    ~ExpectedPipelineFailureMessages() {
        if (queue) { queue->PopStorageFilter(); queue->Release(); }
    }
};
}

void nativeMaterialProbe(RenderDevice* device, RenderWorker& worker, const char* shaderPath,
    const char* rasterVsPath, const char* rasterPsPath) {
    std::ifstream input(shaderPath, std::ios::binary);
    std::vector<char> code((std::istreambuf_iterator<char>(input)), {});
    require(!code.empty(), "native material shader missing");
    // Alpha holes, the exact 1/8 coverage boundary, wrap/mirror/clamp endpoints,
    // native 3-point filtering, alpha compare and HDR coverage precision.
    struct Case {
        float u, expectedAlpha;
        int address;
        bool bilerp, hdr;
        uint32_t alphaCompare;
        float threshold;
        bool dynamic = false, replacement = false, rawTmem = false, lod = false;
        uint32_t sampler = 0;
        bool shadeTest = false;
        bool subpixelLod = false;
    };
    const std::vector<Case> cases{
        {0, 0, 0, false, false, 0, 0}, {1, 0.125f, 0, false, false, 0, 0},
        {2, 0.5f, 0, false, false, 0, 0}, {3, 1, 0, false, false, 0, 0},
        {4, 0, 0, false, false, 0, 0}, {7, 1, 0, false, false, 0, 0},
        {4, 1, 1, false, false, 0, 0}, {7, 0, 1, false, false, 0, 0},
        {4, 1, 2, false, false, 0, 0}, {-1, 0, 2, false, false, 0, 0},
        {0.5f, 0.0625f, 0, true, false, 0, 0}, {1.5f, 0.3125f, 0, true, false, 0, 0},
        {1, 0.125f, 0, false, true, 0, 0}, {0, 0, 0, false, true, 0, 0},
        {2, 0.5f, 0, false, false, 1, 0.5f}, {2, 0.5f, 0, false, false, 1, 0.75f},
        // Dynamic tile addressing must override the static shader wrap bits.
        {4, 1, 1, false, false, 0, 0, true},
        {4, 1, 2, false, false, 0, 0, true},
        // A replacement has different alpha values and twice the texel scale.
        {0, 1, 0, false, false, 0, 0, false, true},
        {1, 0, 0, false, false, 0, 0, false, true},
        {2, 0.25f, 0, false, false, 0, 0, false, true},
        {4, 1, 1, false, false, 0, 0, false, true},
        {0, 0, 0, false, false, 0, 0, false, false, true},
        {1, 32.0f / 255, 0, false, false, 0, 0, false, false, true},
        {2, 128.0f / 255, 0, false, false, 0, 0, false, false, true},
        {3, 1, 0, false, false, 0, 0, false, false, true},
        // A derivative of two selects tile 1, whose texture is the replacement.
        {0, 1, 0, false, false, 0, 0, false, false, false, true},
        {1, 0, 0, false, false, 0, 0, false, false, false, true},
        {4, 1, 1, false, false, 0, 0, false, false, false, false, NATIVE_SAMPLER_MIRROR_CLAMP},
        {4, 1, 2, false, false, 0, 0, false, false, false, false, NATIVE_SAMPLER_CLAMP_CLAMP},
        {2, 0.125f, 0, false, false, 0, 0, false, false, false, false, 0, true},
        {1, 0.03125f, 0, false, false, 0, 0, false, false, false, false, 0, true},
        {0, 1, 0, false, false, 0, 0, false, false, false, true, 0, false, true},
    };
    std::vector<MaterialProbe> probes(cases.size());
    std::vector<interop::RenderParams> params(cases.size());
    std::vector<interop::RenderIndices> indices(cases.size());
    std::vector<interop::RDPParams> rdp(cases.size());
    std::vector<interop::RDPTile> tiles(cases.size() * 2);
    std::vector<interop::GPUTile> gpu(cases.size() * 2);
    for (uint32_t i = 0; i < cases.size(); ++i) {
        const auto& c = cases[i];
        probes[i] = {{c.u, 0}, {c.lod ? 2.0f : 0.5f, 0}, {0, 0.5f}, c.shadeTest ? 0.25f : 1, 1234};
        auto& p = params[i];
        p.ccL = 0xFC127FFFu; p.ccH = 0xFFFFF238u;
        if (c.shadeTest) {
            // Native alpha cycle: (TEXEL0_ALPHA - ZERO) * SHADE_ALPHA + ZERO.
            p.ccL = (p.ccL & ~((7u << 12) | (7u << 9))) | (1u << 12) | (4u << 9);
            p.ccH = (p.ccH & ~(7u << 9)) | (7u << 9);
        }
        p.omL = (0xCB023038u & ~3u) | c.alphaCompare;
        p.omH = (0x18ACFFu & ~(3u << 12)) | (c.bilerp ? (2u << 12) : 0u);
        if (c.rawTmem) p.omH &= ~(3u << 14); // IA16 uses no palette lookup.
        if (c.lod) p.omH |= G_TL_LOD;
        p.flags.usesTexture0 = p.flags.upscale2D = 1;
        p.flags.cms0 = c.dynamic ? G_TX_WRAP : c.address;
        p.flags.cmt0 = G_TX_CLAMP;
        p.flags.usesHDR = c.hdr;
        p.flags.dynamicTiles = c.dynamic;
        p.flags.canDecodeTMEM = c.rawTmem;
        p.flags.nativeSampler0 = c.sampler;
        p.flags.smoothShade = c.shadeTest;
        indices[i] = {i, 0, 2 * i, 2, 0};
        rdp[i].primColor = hlslpp::float4(1); rdp[i].blendColor = hlslpp::float4(0, 0, 0, c.threshold);
        auto& tile = tiles[2 * i];
        tile.masks = 4; tile.maskt = 1; tile.shifts = tile.shiftt = 1;
        tile.lrs = 12; tile.cms = c.address; tile.cmt = 2;
        tile.fmt = G_IM_FMT_IA; tile.siz = G_IM_SIZ_16b; tile.stride = 8;
        auto& g = gpu[2 * i];
        g.ulScale = hlslpp::float2(1);
        g.tcScale = hlslpp::float2(c.replacement ? 2 : 1, 1);
        g.texelMask = {UINT32_MAX, UINT32_MAX};
        g.textureDimensions = hlslpp::float3(c.replacement ? 8 : 4, 1, 1);
        g.textureIndex = c.replacement ? 1 : 0;
        g.flags.highRes = c.replacement;
        g.flags.rawTMEM = c.rawTmem;
        tiles[2 * i + 1] = tile;
        gpu[2 * i + 1] = g;
        if (c.lod) {
            gpu[2 * i + 1].textureIndex = 1;
            gpu[2 * i + 1].textureDimensions = hlslpp::float3(8, 1, 1);
            gpu[2 * i + 1].tcScale = hlslpp::float2(2, 1);
            gpu[2 * i + 1].flags.highRes = 1;
        }
    }
    SamplerLibrary samplers;
    auto populate = [&](SamplerSet& set, RenderFilter filter) {
        std::array<std::unique_ptr<RenderSampler>*, 9> slots{&set.wrapWrap, &set.wrapMirror, &set.wrapClamp,
            &set.mirrorWrap, &set.mirrorMirror, &set.mirrorClamp, &set.clampWrap, &set.clampMirror, &set.clampClamp};
        const std::array<RenderTextureAddressMode, 3> modes{RenderTextureAddressMode::WRAP,
            RenderTextureAddressMode::MIRROR, RenderTextureAddressMode::CLAMP};
        for (size_t i = 0; i < slots.size(); ++i) {
            RenderSamplerDesc desc;
            desc.minFilter = desc.magFilter = filter;
            desc.addressU = modes[i / 3]; desc.addressV = modes[i % 3]; desc.addressW = RenderTextureAddressMode::CLAMP;
            *slots[i] = device->createSampler(desc);
            require(bool(*slots[i]), "native material sampler allocation failed");
        }
    };
    populate(samplers.linear, RenderFilter::LINEAR);
    populate(samplers.nearest, RenderFilter::NEAREST);
    FramebufferRendererDescriptorCommonSet common(samplers, false, device);
    FramebufferRendererDescriptorTextureSet textures(device, 2), tmem(device, 1);
    FramebufferRendererDescriptorFramebufferSet framebuffer(device);
    RenderDescriptorSetBuilder probesBuilder;
    probesBuilder.begin();
    const auto inputBinding = probesBuilder.addStructuredBuffer(0);
    const auto outputBinding = probesBuilder.addReadWriteStructuredBuffer(0);
    const auto sceneBinding = probesBuilder.addAccelerationStructure(1);
    const auto geometryBinding = probesBuilder.addStructuredBuffer(2);
    const auto facesBinding = probesBuilder.addStructuredBuffer(3);
    const auto screenBinding = probesBuilder.addStructuredBuffer(4);
    const auto uvBinding = probesBuilder.addStructuredBuffer(5);
    const auto shadeBinding = probesBuilder.addStructuredBuffer(6);
    probesBuilder.end();
    auto probeSet = probesBuilder.create(device);
    RenderPipelineLayoutBuilder layoutBuilder;
    layoutBuilder.begin();
    layoutBuilder.addDescriptorSet(common); layoutBuilder.addDescriptorSet(textures);
    layoutBuilder.addDescriptorSet(tmem); layoutBuilder.addDescriptorSet(framebuffer);
    layoutBuilder.addDescriptorSet(probesBuilder.descriptorSetDesc);
    layoutBuilder.end();
    auto layout = layoutBuilder.create(device);
    auto shader = device->createShader(code.data(), code.size(), "CSMain", RenderShaderFormat::DXIL);
    require(layout && shader && probeSet, "native material layout/shader unavailable");
    auto pipeline = device->createComputePipeline(RenderComputePipelineDesc(layout.get(), shader.get(), 64, 1, 1));
    require(layout && shader && pipeline && probeSet, "native material compute pipeline unavailable");
    std::vector<std::unique_ptr<RenderBuffer>> uploads;
    auto bind = [&](RenderDescriptorSetBase& set, uint32_t binding, const auto& values) {
        using Value = typename std::decay_t<decltype(values)>::value_type;
        const size_t bytes = values.size() * sizeof(Value);
        auto buffer = device->createBuffer(RenderBufferDesc::UploadBuffer(bytes, RenderBufferFlag::STORAGE));
        require(bool(buffer), "native material buffer allocation failed");
        upload(buffer.get(), values.data(), bytes);
        set.setBuffer(binding, buffer.get(), bytes, RenderBufferStructuredView(sizeof(Value)));
        uploads.push_back(std::move(buffer));
    };
    bind(common, common.instanceRDPParams, rdp); bind(common, common.RDPTiles, tiles);
    bind(common, common.GPUTiles, gpu); bind(common, common.instanceRenderIndices, indices);
    bind(common, common.DynamicRenderParams, params);
    auto frameUpload = device->createBuffer(RenderBufferDesc::UploadBuffer(256));
    require(bool(frameUpload), "native material frame upload unavailable");
    const interop::FrameParams frame{123, 0, 0};
    upload(frameUpload.get(), &frame, sizeof(frame));
    common.setBuffer(common.FrParams, frameUpload.get(), 256);
    std::vector<std::array<float, 4>> positions, screen, shade;
    std::vector<std::array<float, 2>> uv;
    std::vector<uint32_t> faces;
    std::vector<SunShadowRange> ranges;
    for (uint32_t i = 0; i < cases.size(); ++i) {
        const uint32_t first = uint32_t(positions.size());
        const auto& c = cases[i];
        for (const auto& corner : std::array<std::array<float, 2>, 4>{{{0,0}, {1,0}, {1,1}, {0,1}}}) {
            const float x = corner[0], y = corner[1];
            const float w = c.subpixelLod ? 1 : 1 + 0.25f * x + 0.125f * y;
            const float scale = c.subpixelLod ? 0.5f : 16;
            const float hitW = c.subpixelLod ? 1 : 1 + 0.25f * 0.25f + 0.125f * 0.375f;
            positions.push_back({2 * float(i) + x, y, 1, 1});
            // The query hit projects exactly onto raster pixel (11,12).
            screen.push_back({11.5f + scale * (x / w - 0.25f / hitW),
                12.5f + scale * (y / w - 0.375f / hitW), 0.5f, w});
            uv.push_back({c.u + (c.lod && !c.subpixelLod ? 64 : 1) * (x - 0.25f), 0});
            shade.push_back({1, 1, 1, c.shadeTest ? x : 1});
        }
        ranges.push_back({uint32_t(faces.size()), 6, i, false, 0, 1, true});
        for (uint32_t vertex : {0u, 1u, 2u, 0u, 2u, 3u}) faces.push_back(first + vertex);
    }
    auto bindProbe = [&](uint32_t binding, const auto& values) {
        using Value = typename std::decay_t<decltype(values)>::value_type;
        const size_t bytes = values.size() * sizeof(Value);
        auto buffer = device->createBuffer(RenderBufferDesc::UploadBuffer(bytes,
            RenderBufferFlag::STORAGE | RenderBufferFlag::VERTEX));
        require(bool(buffer), "native ray-hit attribute allocation failed");
        upload(buffer.get(), values.data(), bytes);
        const RenderBufferStructuredView view(sizeof(Value));
        probeSet->setBuffer(binding, buffer.get(), bytes, &view);
        auto* result = buffer.get();
        uploads.push_back(std::move(buffer));
        return result;
    };
    auto* screenBuffer = bindProbe(screenBinding, screen);
    auto* uvBuffer = bindProbe(uvBinding, uv);
    auto* shadeBuffer = bindProbe(shadeBinding, shade);
    auto positionUpload = device->createBuffer(RenderBufferDesc::UploadBuffer(positions.size() * 16));
    auto indexUpload = device->createBuffer(RenderBufferDesc::UploadBuffer(faces.size() * 4));
    auto worldBuffer = device->createBuffer(RenderBufferDesc::DefaultBuffer(positions.size() * 16,
        RenderBufferFlag::STORAGE | RenderBufferFlag::ACCELERATION_STRUCTURE_INPUT));
    auto faceBuffer = device->createBuffer(RenderBufferDesc::DefaultBuffer(faces.size() * 4,
        RenderBufferFlag::STORAGE | RenderBufferFlag::INDEX | RenderBufferFlag::ACCELERATION_STRUCTURE_INPUT));
    require(positionUpload && indexUpload && worldBuffer && faceBuffer, "native ray-hit geometry allocation failed");
    upload(positionUpload.get(), positions.data(), positions.size() * 16);
    upload(indexUpload.get(), faces.data(), faces.size() * 4);
    const RenderBufferStructuredView faceView(4), geometryView(16);
    probeSet->setBuffer(facesBinding, faceBuffer.get(), faces.size() * 4, &faceView);
    SunShadowScene scene;
    require(scene.prepare(device, {worldBuffer.get(), positions.size() * 16},
        {faceBuffer.get(), faces.size() * 4}, uint32_t(positions.size()), faces, ranges, 1), scene.error().c_str());
    auto probeUpload = device->createBuffer(RenderBufferDesc::UploadBuffer(probes.size() * sizeof(MaterialProbe), RenderBufferFlag::STORAGE));
    auto output = device->createBuffer(RenderBufferDesc::DefaultBuffer(cases.size() * sizeof(MaterialResult),
        RenderBufferFlag::STORAGE | RenderBufferFlag::UNORDERED_ACCESS));
    auto readback = device->createBuffer(RenderBufferDesc::ReadbackBuffer(cases.size() * sizeof(MaterialResult)));
    auto fbUpload = device->createBuffer(RenderBufferDesc::UploadBuffer(256));
    auto texture = device->createTexture(RenderTextureDesc::Texture(RenderTextureDimension::TEXTURE_2D,
        4, 1, 1, 1, 1, RenderFormat::R32G32B32A32_FLOAT));
    auto replacement = device->createTexture(RenderTextureDesc::Texture(RenderTextureDimension::TEXTURE_2D,
        8, 1, 1, 1, 1, RenderFormat::R32G32B32A32_FLOAT));
    auto rawTmem = device->createTexture(RenderTextureDesc::Texture(RenderTextureDimension::TEXTURE_1D,
        4096, 1, 1, 1, 1, RenderFormat::R32_UINT));
    auto textureUpload = device->createBuffer(RenderBufferDesc::UploadBuffer(256));
    auto replacementUpload = device->createBuffer(RenderBufferDesc::UploadBuffer(256));
    auto tmemUpload = device->createBuffer(RenderBufferDesc::UploadBuffer(4096 * sizeof(uint32_t)));
    auto backgroundDepth = device->createTexture(RenderTextureDesc::DepthTarget(1, 1, RenderFormat::D32_FLOAT));
    auto backgroundDepthView = backgroundDepth ? backgroundDepth->createTextureView(
        RenderTextureViewDesc::Texture2D(RenderFormat::R32_FLOAT)) : nullptr;
    require(probeUpload && output && readback && fbUpload && texture && replacement && rawTmem &&
        textureUpload && replacementUpload && tmemUpload && backgroundDepth && backgroundDepthView,
        "native material GPU allocation failed");
    const std::array<std::array<float, 4>, 4> texels{{{1,1,1,0}, {1,1,1,0.125f}, {1,1,1,0.5f}, {1,1,1,1}}};
    upload(textureUpload.get(), texels.data(), sizeof(texels));
    const std::array<float, 8> replacementAlpha{1, 1, 0, 0, 0.25f, 0.25f, 1, 1};
    std::array<std::array<float, 4>, 8> replacementTexels{};
    for (size_t i = 0; i < replacementTexels.size(); ++i)
        replacementTexels[i] = {1, 1, 1, replacementAlpha[i]};
    upload(replacementUpload.get(), replacementTexels.data(), sizeof(replacementTexels));
    std::array<uint32_t, 4096> tmemBytes{};
    const std::array<uint32_t, 4> tmemAlpha{0, 32, 128, 255};
    for (size_t i = 0; i < tmemAlpha.size(); ++i) {
        tmemBytes[2 * i] = 255;
        tmemBytes[2 * i + 1] = tmemAlpha[i];
    }
    upload(tmemUpload.get(), tmemBytes.data(), sizeof(tmemBytes));
    upload(probeUpload.get(), probes.data(), probes.size() * sizeof(MaterialProbe));
    const interop::FramebufferParams fb{hlslpp::float2(32), hlslpp::float2(1), 0};
    upload(fbUpload.get(), &fb, sizeof(fb));
    framebuffer.setBuffer(framebuffer.FbParams, fbUpload.get(), 256);
    framebuffer.setTexture(framebuffer.gBackgroundColor, texture.get(), RenderTextureLayout::SHADER_READ);
    framebuffer.setTexture(framebuffer.gBackgroundDepth, backgroundDepth.get(),
        RenderTextureLayout::SHADER_READ, backgroundDepthView.get());
    const RenderBufferStructuredView probeView(sizeof(MaterialProbe)), resultView(sizeof(MaterialResult));
    probeSet->setBuffer(inputBinding, probeUpload.get(), probes.size() * sizeof(MaterialProbe), &probeView);
    probeSet->setBuffer(outputBinding, output.get(), cases.size() * sizeof(MaterialResult), &resultView);
    textures.setTexture(textures.gTextures, texture.get(), RenderTextureLayout::SHADER_READ);
    textures.setTexture(textures.gTextures + 1, replacement.get(), RenderTextureLayout::SHADER_READ);
    tmem.setTexture(tmem.gTMEM, rawTmem.get(), RenderTextureLayout::SHADER_READ);
    auto* commands = worker.commandList.get();
    commands->begin();
    commands->barriers(RenderBarrierStage::COPY, {
        RenderBufferBarrier(worldBuffer.get(), RenderBufferAccess::WRITE),
        RenderBufferBarrier(faceBuffer.get(), RenderBufferAccess::WRITE)});
    commands->copyBufferRegion(worldBuffer->at(0), positionUpload->at(0), positions.size() * 16);
    commands->copyBufferRegion(faceBuffer->at(0), indexUpload->at(0), faces.size() * 4);
    require(scene.recordBuild(commands), "native material AS build failed");
    probeSet->setAccelerationStructure(sceneBinding, scene.accelerationStructure(1));
    probeSet->setBuffer(geometryBinding, scene.metadata(1), scene.metadataBytes(), &geometryView);
    commands->barriers(RenderBarrierStage::COPY, {
        RenderTextureBarrier(texture.get(), RenderTextureLayout::COPY_DEST),
        RenderTextureBarrier(replacement.get(), RenderTextureLayout::COPY_DEST),
        RenderTextureBarrier(rawTmem.get(), RenderTextureLayout::COPY_DEST)});
    commands->copyTextureRegion(RenderTextureCopyLocation::Subresource(texture.get()),
        RenderTextureCopyLocation::PlacedFootprint(textureUpload.get(), RenderFormat::R32G32B32A32_FLOAT, 4, 1, 1, 16));
    commands->copyTextureRegion(RenderTextureCopyLocation::Subresource(replacement.get()),
        RenderTextureCopyLocation::PlacedFootprint(replacementUpload.get(), RenderFormat::R32G32B32A32_FLOAT, 8, 1, 1, 16));
    commands->copyTextureRegion(RenderTextureCopyLocation::Subresource(rawTmem.get()),
        RenderTextureCopyLocation::PlacedFootprint(tmemUpload.get(), RenderFormat::R32_UINT, 4096, 1, 1, 4096));
    commands->barriers(RenderBarrierStage::COMPUTE,
         {RenderTextureBarrier(texture.get(), RenderTextureLayout::SHADER_READ),
         RenderTextureBarrier(replacement.get(), RenderTextureLayout::SHADER_READ),
         RenderTextureBarrier(rawTmem.get(), RenderTextureLayout::SHADER_READ),
         RenderTextureBarrier(backgroundDepth.get(), RenderTextureLayout::SHADER_READ)});
    commands->barriers(RenderBarrierStage::COMPUTE, RenderBufferBarrier(output.get(), RenderBufferAccess::WRITE));
    commands->setComputePipelineLayout(layout.get()); commands->setPipeline(pipeline.get());
    commands->setComputeDescriptorSet(common.descriptorSet.get(), 0);
    commands->setComputeDescriptorSet(textures.descriptorSet.get(), 1);
    commands->setComputeDescriptorSet(tmem.descriptorSet.get(), 2);
    commands->setComputeDescriptorSet(framebuffer.descriptorSet.get(), 3);
    commands->setComputeDescriptorSet(probeSet.get(), 4);
    commands->dispatch(1, 1, 1);
    commands->barriers(RenderBarrierStage::COPY, RenderBufferBarrier(output.get(), RenderBufferAccess::READ));
    commands->copyBufferRegion(RenderBufferReference(readback.get()), RenderBufferReference(output.get()), cases.size() * sizeof(MaterialResult));
    commands->end(); worker.execute(); worker.wait();
    const RenderRange readRange(0, cases.size() * sizeof(MaterialResult));
    const auto* result = static_cast<const MaterialResult*>(readback->map(0, &readRange));
    require(result != nullptr, "native material readback failed");
    for (size_t i = 0; i < cases.size(); ++i) {
        const auto& c = cases[i];
        const bool survived = c.expectedAlpha >= 0.125f && c.expectedAlpha >= c.threshold;
        if (std::abs(result[i].values[1] - c.expectedAlpha) > 1e-6f || result[i].values[0] != (survived ? 1 : 0)) {
            std::fprintf(stderr, "Native material case %zu: alpha=%f pass=%f expected=%f/%d\n", i,
                result[i].values[1], result[i].values[0], c.expectedAlpha, int(survived));
            throw std::runtime_error("native texture/coverage result differs from analytic fixture");
        }
        require(result[i].identity[0] == 1664525u * probes[i].seed + 1013904223u && result[i].identity[1] == i,
            "native material random-seed advancement or input identity changed");
        const bool hitSupported = !c.shadeTest && !c.subpixelLod;
        const float expectedVisibility = hitSupported && survived ? 0 : 1;
        if (result[i].rayValues[0] != expectedVisibility || result[i].rayValues[1] != (hitSupported ? 1 : 0)) {
            std::fprintf(stderr, "Native candidate case %zu: visibility=%f supported=%f expected=%d/1\n", i,
                result[i].rayValues[0], result[i].rayValues[1], int(expectedVisibility));
            throw std::runtime_error("ray candidate coverage differs from native material fixture");
        }
        require(result[i].rayValues[2] == 1 && result[i].rayValues[3] == 1,
            "behind-eye or degenerate native hit input admitted");
    }
    const RenderRange none(0, 0); readback->unmap(0, &none);
    if (rasterVsPath && rasterPsPath) {
        auto loadShader = [&](const char* path, const char* entry) {
            std::ifstream file(path, std::ios::binary);
            std::vector<char> bytes((std::istreambuf_iterator<char>(file)), {});
            require(!bytes.empty(), "native raster probe shader missing");
            return device->createShader(bytes.data(), bytes.size(), entry, RenderShaderFormat::DXIL);
        };
        auto vs = loadShader(rasterVsPath, "VSMain"), ps = loadShader(rasterPsPath, "PSMain");
        RenderPipelineLayoutBuilder rasterLayoutBuilder;
        rasterLayoutBuilder.begin(false, true); // Native vertex input layout.
        rasterLayoutBuilder.addPushConstant(0, 0, sizeof(interop::RasterParams),
            RenderShaderStageFlag::VERTEX | RenderShaderStageFlag::PIXEL);
        rasterLayoutBuilder.addDescriptorSet(common); rasterLayoutBuilder.addDescriptorSet(textures);
        rasterLayoutBuilder.addDescriptorSet(tmem); rasterLayoutBuilder.addDescriptorSet(framebuffer);
        rasterLayoutBuilder.end();
        auto rasterLayout = rasterLayoutBuilder.create(device);
        const RenderInputSlot slots[]{RenderInputSlot(0, 16), RenderInputSlot(1, 8), RenderInputSlot(2, 16)};
        const RenderInputElement elements[]{
            RenderInputElement("POSITION", 0, 0, RenderFormat::R32G32B32A32_FLOAT, 0, 0),
            RenderInputElement("TEXCOORD", 0, 1, RenderFormat::R32G32_FLOAT, 1, 0),
            RenderInputElement("COLOR", 0, 2, RenderFormat::R32G32B32A32_FLOAT, 2, 0)};
        RenderGraphicsPipelineDesc desc;
        desc.pipelineLayout = rasterLayout.get(); desc.vertexShader = vs.get(); desc.pixelShader = ps.get();
        desc.inputSlots = slots; desc.inputSlotsCount = 3; desc.inputElements = elements; desc.inputElementsCount = 3;
        desc.renderTargetCount = 1; desc.renderTargetFormat[0] = RenderFormat::R32G32_UINT;
        desc.renderTargetBlend[0] = RenderBlendDesc::Copy(); desc.cullMode = RenderCullMode::NONE;
        desc.depthEnabled = desc.depthWriteEnabled = desc.depthClipEnabled = true;
        desc.depthFunction = RenderComparisonFunction::LESS; desc.depthTargetFormat = RenderFormat::D32_FLOAT;
        desc.primitiveTopology = RenderPrimitiveTopology::TRIANGLE_LIST;
        auto rasterPipeline = device->createGraphicsPipeline(desc);
        require(bool(rasterPipeline), "native raster pipeline creation failed");
        {
            ExpectedPipelineFailureMessages expected(device);
            RenderPipelineLayoutBuilder wrongGraphicsBuilder;
            wrongGraphicsBuilder.begin(); // Deliberately missing native vertex-input flag.
            wrongGraphicsBuilder.addPushConstant(0, 0, sizeof(interop::RasterParams),
                RenderShaderStageFlag::VERTEX | RenderShaderStageFlag::PIXEL);
            wrongGraphicsBuilder.addDescriptorSet(common); wrongGraphicsBuilder.addDescriptorSet(textures);
            wrongGraphicsBuilder.addDescriptorSet(tmem); wrongGraphicsBuilder.addDescriptorSet(framebuffer);
            wrongGraphicsBuilder.end();
            auto wrongGraphicsLayout = wrongGraphicsBuilder.create(device);
            require(bool(wrongGraphicsLayout), "failure injection root signature unavailable");
            auto wrongGraphicsDesc = desc;
            wrongGraphicsDesc.pipelineLayout = wrongGraphicsLayout.get();
            require(!device->createGraphicsPipeline(wrongGraphicsDesc), "failed native graphics PSO exposed as ready");
            RenderPipelineLayoutBuilder emptyBuilder;
            emptyBuilder.begin(); emptyBuilder.end();
            auto emptyLayout = emptyBuilder.create(device);
            require(bool(emptyLayout), "failure injection compute root signature unavailable");
            require(!device->createComputePipeline(RenderComputePipelineDesc(emptyLayout.get(), shader.get(), 64, 1, 1)),
                "failed native compute PSO exposed as ready");
        }
        std::puts("PASS: native graphics/compute pipeline creation failures return null; device remains usable");
        auto owner = device->createTexture(RenderTextureDesc::ColorTarget(32, 32, RenderFormat::R32G32_UINT));
        auto depth = device->createTexture(RenderTextureDesc::DepthTarget(32, 32, RenderFormat::D32_FLOAT));
        const RenderTexture* ownerTarget = owner.get();
        auto rasterFramebuffer = device->createFramebuffer(RenderFramebufferDesc(&ownerTarget, 1, depth.get()));
        constexpr size_t imageBytes = 32 * 32 * 8;
        auto rasterReadback = device->createBuffer(RenderBufferDesc::ReadbackBuffer(cases.size() * imageBytes));
        require(vs && ps && rasterLayout && rasterPipeline && owner && depth && rasterFramebuffer && rasterReadback,
            "native raster comparison allocation failed");
        const RenderVertexBufferView vertices[]{
            {screenBuffer->at(0), uint32_t(screen.size() * 16)},
            {uvBuffer->at(0), uint32_t(uv.size() * 8)},
            {shadeBuffer->at(0), uint32_t(shade.size() * 16)}};
        const RenderIndexBufferView indexView(faceBuffer->at(0), uint32_t(faces.size() * 4), RenderFormat::R32_UINT);
        const RenderViewport viewport(0, 0, 32, 32);
        const RenderRect scissor(0, 0, 32, 32);
        commands->begin();
        commands->barriers(RenderBarrierStage::GRAPHICS, RenderTextureBarrier(depth.get(), RenderTextureLayout::DEPTH_WRITE));
        commands->barriers(RenderBarrierStage::GRAPHICS, RenderBufferBarrier(faceBuffer.get(), RenderBufferAccess::READ));
        commands->setGraphicsPipelineLayout(rasterLayout.get()); commands->setPipeline(rasterPipeline.get());
        commands->setGraphicsDescriptorSet(common.descriptorSet.get(), 0);
        commands->setGraphicsDescriptorSet(textures.descriptorSet.get(), 1);
        commands->setGraphicsDescriptorSet(tmem.descriptorSet.get(), 2);
        commands->setGraphicsDescriptorSet(framebuffer.descriptorSet.get(), 3);
        commands->setVertexBuffers(0, vertices, 3, slots); commands->setIndexBuffer(&indexView);
        commands->setViewports(&viewport, 1); commands->setScissors(&scissor, 1);
        for (uint32_t i = 0; i < cases.size(); ++i) {
            commands->barriers(RenderBarrierStage::GRAPHICS, RenderTextureBarrier(owner.get(), RenderTextureLayout::COLOR_WRITE));
            commands->setFramebuffer(rasterFramebuffer.get()); commands->clearColor(); commands->clearDepth();
            interop::RasterParams raster{};
            raster.renderIndex = i; raster.screenScale = hlslpp::float2(1); raster.screenOffset = hlslpp::float2(0);
            commands->setGraphicsPushConstants(0, &raster);
            commands->drawIndexedInstanced(6, 1, 6 * i, 0, 0);
            commands->barriers(RenderBarrierStage::COPY, RenderTextureBarrier(owner.get(), RenderTextureLayout::COPY_SOURCE));
            commands->copyTextureRegion(RenderTextureCopyLocation::PlacedFootprint(rasterReadback.get(),
                RenderFormat::R32G32_UINT, 32, 32, 1, 32, imageBytes * i), RenderTextureCopyLocation::Subresource(owner.get()));
        }
        commands->end(); worker.execute(); worker.wait();
        const RenderRange rasterRange(0, cases.size() * imageBytes);
        const auto* pixels = static_cast<const uint32_t*>(rasterReadback->map(0, &rasterRange));
        require(pixels != nullptr, "native raster comparison readback failed");
        size_t unsupportedCases = 0;
        for (size_t i = 0; i < cases.size(); ++i) {
            const auto& c = cases[i];
            const bool covered = c.expectedAlpha >= 0.125f && c.expectedAlpha >= c.threshold;
            const uint32_t drawOwner = pixels[i * imageBytes / 4 + 2 * (12 * 32 + 11)];
            if (c.shadeTest || c.subpixelLod) {
                ++unsupportedCases;
                std::printf("Unsupported material case %zu: native_owner=%u raw_alpha=%f; ray coverage remains invalid\n",
                    i, drawOwner, c.expectedAlpha);
                continue;
            }
            if (drawOwner != (covered ? i + 1 : 0)) {
                std::fprintf(stderr, "Native raster case %zu: owner=%u expected=%u\n", i, drawOwner,
                    uint32_t(covered ? i + 1 : 0));
                throw std::runtime_error("native raster coverage differs from ray-candidate material");
            }
        }
        rasterReadback->unmap(0, &none);
        std::printf("PASS: native RasterVS/RasterPS ownership agrees with %zu supported ray candidates; %zu paths remain unsupported\n",
            cases.size() - unsupportedCases, unsupportedCases);
    }
    std::printf("PASS: native material GPU: %zu analytic cases and real ray-candidate coverage through production evaluator\n", cases.size());
}
