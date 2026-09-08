// Interior crease lines. ModelMeshPass3D builds a triangle-list of thin ribbons
// on the CPU (import/CreaseLines) for edges whose two faces meet at a sharp
// angle - thicker for sharper folds, tinted with the desaturated average of the
// two faces' colours so it reads like baked ambient occlusion. This shader just
// transforms and passes the per-vertex colour through.
#include "common3d.hlsli"

struct VSIn  { float3 pos : POSITION; float4 color : COLOR; };
struct VSOut { float4 pos : SV_POSITION; float4 color : COLOR; };

VSOut VSMain(VSIn input)
{
    VSOut output;
    float4 worldPos = mul(float4(input.pos, 1.0f), world);
    output.pos = mul(worldPos, viewProj);
    output.color = input.color;
    return output;
}

float4 PSMain(VSOut input) : SV_TARGET
{
    return input.color;
}
