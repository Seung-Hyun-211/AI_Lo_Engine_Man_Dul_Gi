// MeshPass3D instanced path: many copies of one built-in mesh, transform + colour
// per instance from vertex stream slot 1 (no Object cbuffer). Y-rotation only, so
// no matrix - two sin/cos. docs/instanced-rendering.md §4.4.
#include "common3d.hlsli"

struct VSIn
{
    float3 pos    : POSITION;    // slot 0 - mesh vertex
    float3 nrm    : NORMAL;
    float3 ipos   : TEXCOORD1;   // slot 1 - MeshInstance
    float  iyaw   : TEXCOORD2;
    float  iscale : TEXCOORD3;
    float4 icol   : COLOR0;      // R8G8B8A8_UNORM
};

struct VSOut
{
    float4 pos        : SV_POSITION;
    float3 nrm        : NORMAL;
    float4 shadowClip : TEXCOORD1;
    float4 col        : COLOR0;
};

VSOut VSMain(VSIn input)
{
    float s = sin(input.iyaw);
    float c = cos(input.iyaw);

    float3 lp = input.pos * input.iscale;
    float3 wp = float3(c * lp.x + s * lp.z, lp.y, -s * lp.x + c * lp.z) + input.ipos;
    float3 wn = float3(c * input.nrm.x + s * input.nrm.z, input.nrm.y, -s * input.nrm.x + c * input.nrm.z);

    VSOut output;
    output.pos        = mul(float4(wp, 1.0f), viewProj);
    output.nrm        = wn;
    output.shadowClip = mul(float4(wp, 1.0f), lightViewProj);
    output.col        = input.icol;
    return output;
}

float4 PSMain(VSOut input) : SV_TARGET
{
    float shadow = SampleShadow(input.shadowClip);
    return float4(ApplyLighting(input.col.rgb, input.nrm, shadow), input.col.a);
}
