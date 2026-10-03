// Probe wrapper around the reusable kernel; the sampling/query/fog code lives
// in RT64's local patch and is the code a future receiver pipeline will include.
#include "shaders/SunShadow.hlsli"
ConstantBuffer<SunShadowParams> Params : register(b0);
RaytracingAccelerationStructure Scene : register(t0);
StructuredBuffer<uint4> Geometry : register(t1);
struct Probe {
    float3 position;
    uint receiverDraw;
    float3 normal;
    uint receiverFace;
    float4 nativeColor;
    float3 fogContribution;
    uint reserved;
};
struct Result {
    float4 color;
    float4 info; // visibility, minimum cone dot, maximum length error, spare.
    float4 directionStrength;
    float4 radiusIntervalBias;
    uint4 flags;
};
StructuredBuffer<Probe> Probes : register(t2);
RWStructuredBuffer<Result> Results : register(u0);
[numthreads(64, 1, 1)]
void CSMain(uint3 id : SV_DispatchThreadID) {
    uint count, stride;
    Probes.GetDimensions(count, stride);
    if (id.x >= count) return;
    Probe input = Probes[id.x];
    Result result;
    bool coverageValid;
    float visibility = SunShadowVisibility(Scene, Geometry, input.position,
        input.normal, input.receiverDraw, input.receiverFace, Params, coverageValid);
    result.color = SunShadowCompose(input.nativeColor, input.fogContribution, visibility, Params);
    float minDot = 1, lengthError = 0;
    for (uint i = 0; i < Params.sampleCount; ++i) {
        float3 direction = SunShadowDirection(Params, i);
        minDot = min(minDot, dot(direction, Params.direction));
        lengthError = max(lengthError, abs(length(direction) - 1));
    }
    result.info = float4(visibility, minDot, lengthError, coverageValid ? 1 : 0);
    result.directionStrength = float4(Params.direction, Params.strength);
    result.radiusIntervalBias = float4(Params.angularRadius, Params.rayMin, Params.rayMax, Params.originBias);
    result.flags = uint4(Params.sampleCount, Params.valid, Params.debugMode, Params.reserved);
    Results[id.x] = result;
}
