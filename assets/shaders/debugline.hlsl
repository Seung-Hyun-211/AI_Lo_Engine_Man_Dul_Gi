// DebugDrawPass: world-space colored line list. Dev visualisation only
// (colliders, rays, skeletons). No lighting, no shadow. See docs/roadmap.md.
#include "common3d.hlsli"   // cbuffer Frame (viewProj) at b0

struct VSIn  { float3 pos : POSITION; float4 color : COLOR; };
struct VSOut { float4 pos : SV_POSITION; float4 color : COLOR; };

VSOut VSMain(VSIn input)
{
    VSOut output;
    output.pos = mul(float4(input.pos, 1.0f), viewProj);
    output.color = input.color;
    return output;
}

float4 PSMain(VSOut input) : SV_TARGET
{
    return input.color;
}
