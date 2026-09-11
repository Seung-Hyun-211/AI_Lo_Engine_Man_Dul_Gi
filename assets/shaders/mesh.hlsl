// MeshPass3D: untextured lit geometry (built-in cube / plane, gameplay meshes).
#include "common3d.hlsli"

struct VSIn  { float3 pos : POSITION; float3 nrm : NORMAL; };
struct VSOut { float4 pos : SV_POSITION; float3 nrm : NORMAL; float3 worldPos : TEXCOORD1; };

VSOut VSMain(VSIn input)
{
    VSOut output;
    float4 worldPos = mul(float4(input.pos, 1.0f), world);
    output.pos = mul(worldPos, viewProj);
    output.nrm = mul(float4(input.nrm, 0.0f), world).xyz;
    output.worldPos = worldPos.xyz;
    return output;
}

GeometryPSOut PSMain(VSOut input)
{
    float shadow = SampleShadow(input.worldPos);
    GeometryPSOut output;
    output.color = float4(ApplyLighting(objColor.rgb, input.nrm, shadow), objColor.a);
    output.normal = float4(WorldToViewNormal(input.nrm) * 0.5f + 0.5f, 1.0f);
    return output;
}
