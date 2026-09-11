// ModelMeshPass3D: textured lit geometry from an imported FBX (bind pose).
#include "common3d.hlsli"

Texture2D    albedo : register(t0);
SamplerState samp   : register(s0);

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

GeometryPSOut PSMain(VSOut input)
{
    float4 tex = albedo.Sample(samp, input.uv);
    clip(tex.a - 0.35f);                                  // cutout for hair / eyelashes
    float3 base = tex.rgb * objColor.rgb;
    float shadow = SampleShadow(input.shadowClip);
    GeometryPSOut output;
    output.color = float4(ApplyLighting(base, input.nrm, shadow), 1.0f);
    output.normal = float4(WorldToViewNormal(input.nrm) * 0.5f + 0.5f, 1.0f);
    return output;
}
