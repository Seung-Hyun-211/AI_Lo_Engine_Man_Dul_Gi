// PostProcessPass, multisampled path: same job as composite.hlsl but the scene
// colour target is multisampled, so this also does the job the old end-of-frame
// ResolveSubresource used to (a plain per-pixel average across samples) -
// docs/post-process-gbuffer-research.md §4.3/§6/§12.7/§12.11 step 3, AO added
// in step 8. Two files instead of one because HLSL has no Texture2D/Texture2DMS
// polymorphism and this engine has no shader-permutation system yet
// (docs/shader-pipeline.md "다음").
#include "fullscreen.hlsli"

Texture2DMS<float4> sceneColor : register(t0);
Texture2D aoTex                 : register(t1);
SamplerState aoSampler           : register(s0);

float4 PSMain(VSOut input) : SV_TARGET
{
    uint width, height, sampleCount;
    sceneColor.GetDimensions(width, height, sampleCount);

    const int2 pixel = int2(input.pos.xy);
    float3 sum = float3(0.0f, 0.0f, 0.0f);
    for (uint s = 0; s < sampleCount; ++s)
        sum += sceneColor.Load(pixel, s).rgb;
    const float3 color = sum / max(sampleCount, 1u);

    const float ao = aoTex.Sample(aoSampler, input.uv).r;
    return float4(color * ao, 1.0f);
}
