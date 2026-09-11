// Cel-shaded variant of mesh_instanced.hlsl. Identical VS (per-instance
// transform + optional VAT); the PS quantises the key term into toon bands.
// Same input layout, so InstanceBatch.shader picks between them per batch.
// docs/instanced-rendering.md §9.7.
#include "common3d.hlsli"

Texture2D          diffuse : register(t0);
SamplerState       samp    : register(s0);
Texture2D<float4>  vatPos  : register(t2);

cbuffer VatInfo : register(b2)
{
    float4 vatParams;   // x = sample rate, y = frame count (0 => no VAT), zw unused
};

struct VSIn
{
    float3 pos    : POSITION;
    float3 nrm    : NORMAL;
    float2 uv     : TEXCOORD0;
    float3 ipos   : TEXCOORD1;
    float  iyaw   : TEXCOORD2;
    float  iscale : TEXCOORD3;
    float4 icol   : COLOR0;
    float  itime  : TEXCOORD4;
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

    float3 local = input.pos;
    if (vatParams.y >= 1.0f)
    {
        uint fc = (uint)vatParams.y;
        uint frame = ((uint)(input.itime * vatParams.x)) % fc;
        local = vatPos.Load(int3((int)input.vid, (int)frame, 0)).xyz;
    }
    local *= input.iscale;

    float3 wp = float3(c * local.x + s * local.z, local.y, -s * local.x + c * local.z) + input.ipos;
    float3 wn = float3(c * input.nrm.x + s * input.nrm.z, input.nrm.y, -s * input.nrm.x + c * input.nrm.z);

    VSOut output;
    output.pos        = mul(float4(wp, 1.0f), viewProj);
    output.nrm        = wn;
    output.shadowClip = mul(float4(wp, 1.0f), lightViewProj);
    output.uv         = input.uv;
    output.col        = input.icol;
    return output;
}

GeometryPSOut PSMain(VSOut input)
{
    float4 tex = diffuse.Sample(samp, input.uv);
    float shadow = SampleShadow(input.shadowClip);
    // shadowBias 0, bandSoftness 6 deg, wrap 0.3
    float3 lit = ApplyCelLighting(tex.rgb * input.col.rgb, input.nrm, 0.0f, 6.0f, 0.3f, shadow);
    GeometryPSOut output;
    output.color = float4(lit, tex.a * input.col.a);
    output.normal = float4(WorldToViewNormal(input.nrm) * 0.5f + 0.5f, 1.0f);
    return output;
}
