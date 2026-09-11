// PostProcessPass, multisampled path: same job as composite.hlsl but the scene
// colour/depth targets are multisampled, so this also does the job the old
// end-of-frame ResolveSubresource used to (a plain per-pixel average across
// colour samples) - docs/post-process-gbuffer-research.md §3.2/§5. Two files
// instead of one because HLSL has no Texture2D/Texture2DMS polymorphism and
// this engine has no shader-permutation system yet (docs/shader-pipeline.md
// "다음").
#include "fullscreen.hlsli"

Texture2DMS<float4> sceneColor : register(t0);
Texture2D aoTex                  : register(t1);
Texture2DMS<float> depthTex     : register(t2);
SamplerState aoSampler            : register(s0);

cbuffer CompositeParams : register(b0)
{
    float4 aoParams;    // x = aoStrength (0 = no AO, 1 = full)
    float4 fogParams;   // x = fogNear, y = fogFar, z = enabled (0/1)
    float4 fogColor;    // rgb
    float4 depthProj;   // z = A, w = B (proj.m[10,14]) - ViewZFromDepth, fog only
};

float ViewZFromDepth(float depth) { return depthProj.w / (depth - depthProj.z); }

float4 PSMain(VSOut input) : SV_TARGET
{
    uint width, height, sampleCount;
    sceneColor.GetDimensions(width, height, sampleCount);

    const int2 pixel = int2(input.pos.xy);
    float3 sum = float3(0.0f, 0.0f, 0.0f);
    for (uint s = 0; s < sampleCount; ++s)
        sum += sceneColor.Load(pixel, s).rgb;
    float3 color = sum / max(sampleCount, 1u);

    const float ao = aoTex.Sample(aoSampler, input.uv).r;
    color *= lerp(1.0f, ao, aoParams.x);

    if (fogParams.z > 0.5f)
    {
        const float depth = depthTex.Load(pixel, 0);
        if (depth < 1.0f)
        {
            const float viewZ = ViewZFromDepth(depth);
            const float fog = saturate((viewZ - fogParams.x) / max(fogParams.y - fogParams.x, 1e-4f));
            color = lerp(color, fogColor.rgb, fog);
        }
    }

    return float4(color, 1.0f);
}
