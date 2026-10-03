// Uses native descriptor layouts and the production material evaluator. All
// textures/parameters are synthetic; native game alpha admission remains gated.
#define HLSL_CPU
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
#include "shared/rt64_f3d_defines.h"
#include "shared/rt64_framebuffer_params.h"
#include "shared/rt64_gpu_tile.h"
#include "shared/rt64_rdp_params.h"
#include "shared/rt64_rdp_tile.h"
#include "shared/rt64_render_indices.h"
#include "shared/rt64_render_params.h"

using namespace plume;
using namespace RT64;
namespace {
struct MaterialProbe { float uv[2], dx[2], dy[2], alpha; uint32_t seed; };
struct MaterialResult { float values[4]; uint32_t identity[4]; };
static_assert(sizeof(MaterialProbe) == 32 && sizeof(MaterialResult) == 32);

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
}

void nativeMaterialProbe(RenderDevice* device, RenderWorker& worker, const char* shaderPath) {
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
    };
    std::vector<MaterialProbe> probes(cases.size());
    std::vector<interop::RenderParams> params(cases.size());
    std::vector<interop::RenderIndices> indices(cases.size());
    std::vector<interop::RDPParams> rdp(cases.size());
    std::vector<interop::RDPTile> tiles(cases.size() * 2);
    std::vector<interop::GPUTile> gpu(cases.size() * 2);
    for (uint32_t i = 0; i < cases.size(); ++i) {
        const auto& c = cases[i];
        probes[i] = {{c.u, 0}, {c.lod ? 2.0f : 0.5f, 0}, {0, 0.5f}, 1, 1234};
        auto& p = params[i];
        p.ccL = 0xFC127FFFu; p.ccH = 0xFFFFF238u;
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
    require(probeUpload && output && readback && fbUpload && texture && replacement && rawTmem &&
        textureUpload && replacementUpload && tmemUpload,
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
    const RenderBufferStructuredView probeView(sizeof(MaterialProbe)), resultView(sizeof(MaterialResult));
    probeSet->setBuffer(inputBinding, probeUpload.get(), probes.size() * sizeof(MaterialProbe), &probeView);
    probeSet->setBuffer(outputBinding, output.get(), cases.size() * sizeof(MaterialResult), &resultView);
    textures.setTexture(textures.gTextures, texture.get(), RenderTextureLayout::SHADER_READ);
    textures.setTexture(textures.gTextures + 1, replacement.get(), RenderTextureLayout::SHADER_READ);
    tmem.setTexture(tmem.gTMEM, rawTmem.get(), RenderTextureLayout::SHADER_READ);
    auto* commands = worker.commandList.get();
    commands->begin();
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
         RenderTextureBarrier(rawTmem.get(), RenderTextureLayout::SHADER_READ)});
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
    }
    const RenderRange none(0, 0); readback->unmap(0, &none);
    std::printf("PASS: native material GPU: %zu analytic texture/coverage cases through production evaluator\n", cases.size());
}
