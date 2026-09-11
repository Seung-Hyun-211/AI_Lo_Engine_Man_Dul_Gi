// PostProcessPass, single-sample path: copies the scene colour target to the
// back buffer, multiplied by AO and blended toward fog colour with distance
// (docs/post-process-gbuffer-research.md §6/§7.1/§12.7/§12.11 step 8). `aoTex`
// is either the real (blurred) SSAO result or PostProcessPass's 1x1 white
// fallback (no 3D module, or nothing to occlude yet) - this shader never needs
// to know which. Fog is skipped entirely when `fogParams.z` is 0 (2D-only
// build, or Scene3D::postProcess.fogEnabled == false).
#include "fullscreen.hlsli"

Texture2D sceneColor      : register(t0);
Texture2D aoTex            : register(t1);
Texture2D<float> depthTex : register(t2);
SamplerState aoSampler     : register(s0);

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
    float3 color = sceneColor.Load(int3(input.pos.xy, 0)).rgb;

    const float ao = aoTex.Sample(aoSampler, input.uv).r;
    color *= lerp(1.0f, ao, aoParams.x);

    if (fogParams.z > 0.5f)
    {
        const float depth = depthTex.Load(int3(input.pos.xy, 0));
        if (depth < 1.0f)   // skip the far plane / sky - already background colour
        {
            const float viewZ = ViewZFromDepth(depth);
            const float fog = saturate((viewZ - fogParams.x) / max(fogParams.y - fogParams.x, 1e-4f));
            color = lerp(color, fogColor.rgb, fog);
        }
    }

    return float4(color, 1.0f);
}
