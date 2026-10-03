// ROM-free execution of the same material evaluator called by native RasterPS.
#include "shaders/NativeMaterial.hlsli"
#include "shaders/NativeRayHit.hlsli"

struct MaterialProbe {
    float2 uv;
    float2 dx;
    float2 dy;
    float shadeAlpha;
    uint seed;
};
struct MaterialResult {
    float4 values; // survived, combined alpha, coverage, alpha compare value.
    uint4 identity; // post-evaluation seed and input index.
    float4 rayValues; // visibility, supported, rejected behind-eye/degenerate inputs.
};
StructuredBuffer<MaterialProbe> Probes : register(t0, space4);
RWStructuredBuffer<MaterialResult> Results : register(u0, space4);
RaytracingAccelerationStructure Scene : register(t1, space4);
StructuredBuffer<uint4> Geometry : register(t2, space4);
StructuredBuffer<uint> Faces : register(t3, space4);
StructuredBuffer<float4> Screen : register(t4, space4);
StructuredBuffer<float2> UV : register(t5, space4);
StructuredBuffer<float4> Shade : register(t6, space4);

bool ProbeCandidateCoverage(uint4 range, uint face, float2 barycentric, out bool supported) {
    supported = false;
    if (range.w != 1) return false;
    float4 screen[3], shade[3];
    float2 uv[3];
    for (uint i = 0; i < 3; ++i) {
        uint vertex = Faces[face + i];
        screen[i] = Screen[vertex];
        uv[i] = UV[vertex];
        shade[i] = Shade[vertex];
    }
    RenderParams rp = DynamicRenderParams[range.y];
    RenderIndices indices = instanceRenderIndices[range.y];
    if (!NativeRayCoverageInputsSupported(rp, shade)) return false;
    NativeHitAttributes attributes;
    supported = NativeRayHitAttributes(rp, float3(1 - barycentric.x - barycentric.y, barycentric),
        screen, uv, shade, float4(0, 0, 32, 32), float2(1, 1), float2(0, 0), attributes);
    if (!supported) return false;
    uint2 pixel = (uint2)floor(attributes.position.xy);
    uint seed = initRand(FrParams.frameCount, indices.instanceIndex * pixel.x * pixel.y, 16);
    float4 materialShade, combined;
    float alphaCompareValue, coverage;
    return EvaluateNativeMaterial(rp, indices, attributes.shade, attributes.position,
        attributes.uv, attributes.dx, attributes.dy, seed,
        materialShade, combined, alphaCompareValue, coverage);
}
#define SUN_SHADOW_CANDIDATE_COVERAGE ProbeCandidateCoverage
#include "shaders/SunShadow.hlsli"

[numthreads(64, 1, 1)]
void CSMain(uint3 id : SV_DispatchThreadID) {
    uint count, stride;
    Probes.GetDimensions(count, stride);
    if (id.x >= count) return;
    MaterialProbe probe = Probes[id.x];
    RenderParams rp = DynamicRenderParams[id.x];
    RenderIndices indices = instanceRenderIndices[id.x];
    uint seed = probe.seed;
    float4 shade, combined;
    float alphaCompareValue, coverage;
    bool survived = EvaluateNativeMaterial(rp, indices,
        float4(1, 1, 1, probe.shadeAlpha), float4(8.5f, 12.5f, 0, 1),
        probe.uv, probe.dx, probe.dy, seed, shade, combined, alphaCompareValue, coverage);
    MaterialResult result;
    result.values = float4(survived ? 1 : 0, combined.a, coverage, alphaCompareValue);
    result.identity = uint4(seed, id.x, 0, 0);
    SunShadowParams p = (SunShadowParams)0;
    p.direction = float3(0, 0, 1);
    p.rayMax = 4;
    p.sampleCount = 4;
    p.valid = 1;
    bool coverageValid;
    float visibility = SunShadowVisibility(Scene, Geometry, float3(2 * id.x + 0.25f, 0.375f, 0),
        float3(0, 0, 1), 0xFFFFFFFF, 0xFFFFFFFF, p, coverageValid);
    result.rayValues = float4(visibility, coverageValid ? 1 : 0, 0, 0);
    float4 invalidScreen[3], validShade[3];
    float2 validUV[3];
    for (uint j = 0; j < 3; ++j) {
        uint vertex = Faces[6 * id.x + j];
        invalidScreen[j] = Screen[vertex];
        validUV[j] = UV[vertex];
        validShade[j] = Shade[vertex];
    }
    NativeHitAttributes unsupported;
    invalidScreen[0].w = 0;
    result.rayValues.z = NativeRayHitAttributes(rp, float3(0.5f, 0.25f, 0.25f),
        invalidScreen, validUV, validShade, float4(0, 0, 32, 32), float2(1, 1), float2(0, 0), unsupported) ? 0 : 1;
    invalidScreen[0].w = 1;
    invalidScreen[1].xy = invalidScreen[2].xy = invalidScreen[0].xy;
    result.rayValues.w = NativeRayHitAttributes(rp, float3(0.5f, 0.25f, 0.25f),
        invalidScreen, validUV, validShade, float4(0, 0, 32, 32), float2(1, 1), float2(0, 0), unsupported) ? 0 : 1;
    Results[id.x] = result;
}
