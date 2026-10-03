// ROM-free execution of the same material evaluator called by native RasterPS.
#include "shaders/NativeMaterial.hlsli"

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
};
StructuredBuffer<MaterialProbe> Probes : register(t0, space4);
RWStructuredBuffer<MaterialResult> Results : register(u0, space4);

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
    Results[id.x] = result;
}
