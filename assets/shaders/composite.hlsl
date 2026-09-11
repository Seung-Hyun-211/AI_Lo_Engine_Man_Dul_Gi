// PostProcessPass, single-sample path: copies the scene colour target to the
// back buffer, multiplied by AO (docs/post-process-gbuffer-research.md
// §6/§12.7/§12.11 step 8). `aoTex` is either the real SSAO result or
// PostProcessPass's 1x1 white fallback (no 3D module, or nothing to occlude
// yet) - this shader never needs to know which.
#include "fullscreen.hlsli"

Texture2D sceneColor : register(t0);
Texture2D aoTex       : register(t1);
SamplerState aoSampler : register(s0);

float4 PSMain(VSOut input) : SV_TARGET
{
    const float3 color = sceneColor.Load(int3(input.pos.xy, 0)).rgb;
    const float ao = aoTex.Sample(aoSampler, input.uv).r;
    return float4(color * ao, 1.0f);
}
