// MeshPass3D: untextured lit geometry (built-in cube / plane, gameplay meshes).
#include "common3d.hlsli"

struct VSIn  { float3 pos : POSITION; float3 nrm : NORMAL; };
struct VSOut { float4 pos : SV_POSITION; float3 nrm : NORMAL; };

VSOut VSMain(VSIn input)
{
    VSOut output;
    float4 worldPos = mul(float4(input.pos, 1.0f), world);
    output.pos = mul(worldPos, viewProj);
    output.nrm = mul(float4(input.nrm, 0.0f), world).xyz;
    return output;
}

float4 PSMain(VSOut input) : SV_TARGET
{
    return float4(ApplyLighting(objColor.rgb, input.nrm), objColor.a);
}
