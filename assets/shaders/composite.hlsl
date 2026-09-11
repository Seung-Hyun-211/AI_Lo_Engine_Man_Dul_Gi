// PostProcessPass, single-sample path: copies the scene colour target straight
// to the back buffer. This is the pass-through seed of the composite stage in
// docs/post-process-gbuffer-research.md §6/§12.7/§12.11 step 3 - AO and fog
// land here as this same shader grows, not as a replacement for it.
#include "fullscreen.hlsli"

Texture2D sceneColor : register(t0);

float4 PSMain(VSOut input) : SV_TARGET
{
    return float4(sceneColor.Load(int3(input.pos.xy, 0)).rgb, 1.0f);
}
