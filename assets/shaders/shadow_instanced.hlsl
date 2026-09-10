// Depth-only instanced shadow draw. The renderer binds lightViewProj at b0
// (ShadowFrame); transform comes from the per-instance vertex stream (slot 1),
// so there is no Object cbuffer here. docs/instanced-rendering.md §4.5.
cbuffer ShadowFrame : register(b0) { row_major float4x4 lightViewProj; };

struct VSIn
{
    float3 pos    : POSITION;    // slot 0 - mesh vertex
    float3 ipos   : TEXCOORD1;   // slot 1 - MeshInstance (pos / yaw / scale)
    float  iyaw   : TEXCOORD2;
    float  iscale : TEXCOORD3;
};

float4 VSMain(VSIn input) : SV_POSITION
{
    float s = sin(input.iyaw);
    float c = cos(input.iyaw);
    float3 lp = input.pos * input.iscale;
    float3 wp = float3(c * lp.x + s * lp.z, lp.y, -s * lp.x + c * lp.z) + input.ipos;
    return mul(float4(wp, 1.0f), lightViewProj);
}

// Present so ShaderLibrary (VSMain + PSMain) compiles; renderer binds null PS.
void PSMain() {}
