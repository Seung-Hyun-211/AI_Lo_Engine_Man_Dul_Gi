// PostProcessPass SSAO blur stage: a 4x4 box blur over the raw AO buffer,
// sized to match ssao.hlsl/ssao_ms.hlsl's noise tile exactly so it cancels the
// periodic dithering that per-pixel kernel rotation introduces. Without this,
// SSAO reads as a "dot density" stipple instead of a smooth gradient - the
// noise+blur combo is standard for hemisphere-kernel SSAO, not optional.
// docs/post-process-gbuffer-research.md §4.
//
// The AO buffer is always single-sample regardless of scene MSAA (PostProcessPass
// renders it full-resolution, one sample), so this shader has no _ms variant.
#include "fullscreen.hlsli"

Texture2D aoTex : register(t0);

float4 PSMain(VSOut input) : SV_TARGET
{
    const int2 basePixel = int2(input.pos.xy) - int2(2, 2);
    float sum = 0.0f;
    [unroll] for (int y = 0; y < 4; ++y)
        [unroll] for (int x = 0; x < 4; ++x)
            sum += aoTex.Load(int3(basePixel + int2(x, y), 0)).r;
    const float ao = sum / 16.0f;
    return float4(ao, ao, ao, 1.0f);
}
