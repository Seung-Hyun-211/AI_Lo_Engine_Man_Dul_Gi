// Cel / toon shading for imported models (ModelMeshPass3D).
//
// The key light term is quantised into 4 bands at 10 / 30 / 50 degrees, shaded
// flat from black (facing away) to white (facing the light). See
// ApplyCelLighting in common3d.hlsli. Design notes: docs/toon-rendering.md.
//
// Face-shadow control:
//   CEL_BAND_SOFTNESS / CEL_WRAP - global look (edit + save -> hot reload)
//   uShadowBias (b3)             - per-material; ModelMeshPass3D sets a larger
//                                  value on face / skin materials so the
//                                  self-shadow does not carve up the face.
#include "common3d.hlsli"

Texture2D    albedo : register(t0);
SamplerState samp   : register(s0);

#define CEL_BAND_SOFTNESS 6.0f    // band-edge transition half-width, degrees (0 = hard)
#define CEL_WRAP          0.35f   // 0 = hard angle bands, 1 = full half-Lambert wrap

cbuffer CelParams : register(b3)
{
    float uShadowBias;   // degrees
    float3 _celPad;
};

struct VSIn  { float3 pos : POSITION; float3 nrm : NORMAL; float2 uv : TEXCOORD; };
struct VSOut { float4 pos : SV_POSITION; float3 nrm : NORMAL; float2 uv : TEXCOORD; float4 shadowClip : TEXCOORD1; };

VSOut VSMain(VSIn input)
{
    VSOut output;
    float4 worldPos = mul(float4(input.pos, 1.0f), world);
    output.pos = mul(worldPos, viewProj);
    output.nrm = mul(float4(input.nrm, 0.0f), world).xyz;
    output.uv = float2(input.uv.x, 1.0f - input.uv.y);   // FBX bottom-left -> D3D top-left
    output.shadowClip = mul(worldPos, lightViewProj);
    return output;
}

float4 PSMain(VSOut input) : SV_TARGET
{
    float4 tex = albedo.Sample(samp, input.uv);
    clip(tex.a - 0.35f);                                  // cutout for hair / eyelashes
    float3 base = tex.rgb * objColor.rgb;
    float shadow = SampleShadow(input.shadowClip);
    return float4(ApplyCelLighting(base, input.nrm, uShadowBias, CEL_BAND_SOFTNESS, CEL_WRAP, shadow), 1.0f);
}
