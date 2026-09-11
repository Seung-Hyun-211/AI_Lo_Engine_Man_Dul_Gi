#ifndef FULLSCREEN_HLSLI
#define FULLSCREEN_HLSLI

// Shared full-screen-triangle vertex shader for post-process passes. Including
// this from a .hlsl file gives it a working VSMain with no vertex or index
// buffer bound - the pass just calls DrawInstanced(3, 1, 0, 0). One triangle
// (not two) covers the whole clip-space square, so there is no diagonal seam.
// See docs/post-process-gbuffer-research.md §12.7.
struct VSOut
{
    float4 pos : SV_POSITION;
    float2 uv  : TEXCOORD0;
};

VSOut VSMain(uint vertexId : SV_VertexID)
{
    // vertexId 0,1,2 -> uv (0,0) (2,0) (0,2); the two corners past 1 are off
    // screen and get clipped, leaving exactly the visible square covered once.
    const float2 uv = float2((vertexId << 1) & 2, vertexId & 2);
    VSOut o;
    o.uv = uv;
    o.pos = float4(uv.x * 2.0f - 1.0f, 1.0f - uv.y * 2.0f, 0.0f, 1.0f);
    return o;
}

#endif
