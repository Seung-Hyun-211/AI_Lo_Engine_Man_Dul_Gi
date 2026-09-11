// PostProcessPass SSAO stage, multisampled path - same algorithm as ssao.hlsl,
// reading sample 0 of the multisampled depth/normal G-buffer (the same
// approximation composite_ms.hlsl and depth reads elsewhere in this pipeline
// use - docs/post-process-gbuffer-research.md §3.2). Two files instead of
// one because HLSL has no Texture2D/Texture2DMS polymorphism and this engine
// has no shader-permutation system yet (docs/shader-pipeline.md "다음").
#include "fullscreen.hlsli"

Texture2DMS<float> depthTex   : register(t0);
Texture2DMS<float4> normalTex : register(t1);
Texture2D noiseTex            : register(t2);
SamplerState wrapSamp         : register(s0);

cbuffer SsaoParams : register(b0)
{
    float4 kernel[16];
    float4 projParams;    // x=xScale, y=yScale, z=A, w=B (proj.m[0,5,10,14])
    float4 params;        // x=radius, y=power, z=bias, w=kernel count
    float4 screenSize;    // x=width, y=height (pixels)
};

float ViewZFromDepth(float depth) { return projParams.w / (depth - projParams.z); }

float3 ViewPosFromDepth(float2 uv, float depth)
{
    const float viewZ = ViewZFromDepth(depth);
    const float2 ndc = float2(uv.x * 2.0f - 1.0f, 1.0f - uv.y * 2.0f);
    return float3(ndc.x * viewZ / projParams.x, ndc.y * viewZ / projParams.y, viewZ);
}

float4 PSMain(VSOut input) : SV_TARGET
{
    const int2 pixel = int2(input.pos.xy);
    const float depth = depthTex.Load(pixel, 0);
    if (depth >= 1.0f) return float4(1.0f, 1.0f, 1.0f, 1.0f);

    const float3 viewPos = ViewPosFromDepth(input.uv, depth);
    const float3 n = normalize(normalTex.Load(pixel, 0).xyz * 2.0f - 1.0f);

    const float3 randomVec = normalize(noiseTex.Sample(wrapSamp, input.uv * (screenSize.xy * 0.25f)).xyz);
    const float3 tangent = normalize(randomVec - n * dot(randomVec, n));
    const float3 bitangent = cross(n, tangent);
    const float3x3 tbn = float3x3(tangent, bitangent, n);

    const int kernelCount = (int)params.w;
    float occlusion = 0.0f;
    for (int i = 0; i < kernelCount; ++i)
    {
        const float3 samplePos = viewPos + mul(kernel[i].xyz, tbn) * params.x;
        const float2 sampleNdc = float2(samplePos.x * projParams.x / samplePos.z,
                                         samplePos.y * projParams.y / samplePos.z);
        const float2 sampleUv = float2(sampleNdc.x * 0.5f + 0.5f, 0.5f - sampleNdc.y * 0.5f);
        if (sampleUv.x < 0.0f || sampleUv.x > 1.0f || sampleUv.y < 0.0f || sampleUv.y > 1.0f) continue;

        const int2 samplePixel = int2(sampleUv * screenSize.xy);
        const float sampleDepthVz = ViewZFromDepth(depthTex.Load(samplePixel, 0));

        const float rangeCheck = smoothstep(0.0f, 1.0f, params.x / max(abs(viewPos.z - sampleDepthVz), 1e-4f));
        occlusion += (sampleDepthVz <= samplePos.z - params.z ? 1.0f : 0.0f) * rangeCheck;
    }

    float ao = 1.0f - occlusion / max((float)kernelCount, 1.0f);
    ao = pow(saturate(ao), params.y);
    return float4(ao, ao, ao, 1.0f);
}
