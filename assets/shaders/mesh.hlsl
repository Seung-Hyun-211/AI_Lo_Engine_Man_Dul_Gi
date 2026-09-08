// MeshPass3D: untextured lit geometry (built-in cube / plane, gameplay meshes).
#include "common3d.hlsli"

struct VSIn  { float3 pos : POSITION; float3 nrm : NORMAL; };
struct VSOut { float4 pos : SV_POSITION; float3 nrm : NORMAL; float4 shadowClip : TEXCOORD1; };

VSOut VSMain(VSIn input)
{
    VSOut output;
    float4 worldPos = mul(float4(input.pos, 1.0f), world);
    output.pos = mul(worldPos, viewProj);
    output.nrm = mul(float4(input.nrm, 0.0f), world).xyz;
    output.shadowClip = mul(worldPos, lightViewProj);
    return output;
}

float4 PSMain(VSOut input) : SV_TARGET
{
    float shadow = SampleShadow(input.shadowClip);
    return float4(ApplyLighting(objColor.rgb, input.nrm, shadow), objColor.a);
}
