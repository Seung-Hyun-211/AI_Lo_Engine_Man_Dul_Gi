// Cel / toon shading for imported models (ModelMeshPass3D).
//
// The key light term is quantised into 4 bands at 10 / 30 / 50 degrees
// (angle between the surface normal and the light), shaded flat from black
// (facing away) to white (facing the light). See ApplyCelLighting in
// common3d.hlsli. Ambient (Frame cbuffer) lifts the darkest band; raise
// LAMP_FLOOR for a coloured shadow instead. Design notes: docs/toon-rendering.md.
#include "common3d.hlsli"

Texture2D    albedo : register(t0);
SamplerState samp   : register(s0);

#define LAMP_FLOOR 0.0f

struct VSIn  { float3 pos : POSITION; float3 nrm : NORMAL; float2 uv : TEXCOORD; };
struct VSOut { float4 pos : SV_POSITION; float3 nrm : NORMAL; float2 uv : TEXCOORD; };

VSOut VSMain(VSIn input)
{
    VSOut output;
    float4 worldPos = mul(float4(input.pos, 1.0f), world);
    output.pos = mul(worldPos, viewProj);
    output.nrm = mul(float4(input.nrm, 0.0f), world).xyz;
    output.uv = float2(input.uv.x, 1.0f - input.uv.y);   // FBX bottom-left -> D3D top-left
    return output;
}

float4 PSMain(VSOut input) : SV_TARGET
{
    float4 tex = albedo.Sample(samp, input.uv);
    clip(tex.a - 0.35f);                                  // cutout for hair / eyelashes
    float3 base = tex.rgb * objColor.rgb;
    return float4(ApplyCelLighting(base, input.nrm, LAMP_FLOOR), 1.0f);
}
