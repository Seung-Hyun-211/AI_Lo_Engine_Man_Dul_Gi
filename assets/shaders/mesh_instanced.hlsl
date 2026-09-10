// MeshPass3D instanced path: many copies of one mesh. Transform + colour +
// anim time per instance from vertex stream slot 1 (no Object cbuffer).
// Y-rotation only, so no matrix - two sin/cos. Diffuse at t0 (1x1 white when
// the crowd has no texture). Optional VAT at t2: pre-skinned local positions
// per (vertex, frame); when vatParams.y >= 1 the VS reads a row instead of the
// bind-pose vertex. docs/instanced-rendering.md §9.6-A/B.
#include "common3d.hlsli"

Texture2D          diffuse : register(t0);
SamplerState       samp    : register(s0);
Texture2D<float4>  vatPos  : register(t2);   // width = vertex count, height = total frames

cbuffer VatInfo : register(b2)
{
    float4 vatParams;   // x = sample rate, y = frame count (0 => no VAT), zw unused
};

struct VSIn
{
    float3 pos    : POSITION;    // slot 0 - mesh vertex (bind pose)
    float3 nrm    : NORMAL;
    float2 uv     : TEXCOORD0;
    float3 ipos   : TEXCOORD1;   // slot 1 - MeshInstance
    float  iyaw   : TEXCOORD2;
    float  iscale : TEXCOORD3;
    float4 icol   : COLOR0;      // R8G8B8A8_UNORM
    float  itime  : TEXCOORD4;   // seconds into the VAT clip
    uint   vid    : SV_VertexID;
};

struct VSOut
{
    float4 pos        : SV_POSITION;
    float3 nrm        : NORMAL;
    float4 shadowClip : TEXCOORD1;
    float2 uv         : TEXCOORD0;
    float4 col        : COLOR0;
};

VSOut VSMain(VSIn input)
{
    float s = sin(input.iyaw);
    float c = cos(input.iyaw);

    // Local-space position: VAT row when a clip is baked, else the bind pose.
    float3 local = input.pos;
    if (vatParams.y >= 1.0f)
    {
        uint fc = (uint)vatParams.y;
        uint frame = ((uint)(input.itime * vatParams.x)) % fc;
        local = vatPos.Load(int3((int)input.vid, (int)frame, 0)).xyz;
    }
    local *= input.iscale;

    float3 wp = float3(c * local.x + s * local.z, local.y, -s * local.x + c * local.z) + input.ipos;
    // Normal stays the bind-pose normal (no normal VAT yet) - fine for a mob.
    float3 wn = float3(c * input.nrm.x + s * input.nrm.z, input.nrm.y, -s * input.nrm.x + c * input.nrm.z);

    VSOut output;
    output.pos        = mul(float4(wp, 1.0f), viewProj);
    output.nrm        = wn;
    output.shadowClip = mul(float4(wp, 1.0f), lightViewProj);
    output.uv         = input.uv;
    output.col        = input.icol;
    return output;
}

float4 PSMain(VSOut input) : SV_TARGET
{
    float4 tex = diffuse.Sample(samp, input.uv);
    float shadow = SampleShadow(input.shadowClip);
    return float4(ApplyLighting(tex.rgb * input.col.rgb, input.nrm, shadow), tex.a * input.col.a);
}
