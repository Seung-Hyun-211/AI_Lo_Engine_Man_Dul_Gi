// Silhouette outline via the inverted-hull method: the mesh is drawn a second
// time, expanded along its normals, with front faces culled so only the shell
// behind the model survives as a constant-width ring. Flat black.
//
// ModelMeshPass3D draws this BEFORE the shaded model, with a front-cull
// rasterizer state.
#include "common3d.hlsli"

cbuffer OutlineParams : register(b2)
{
    float outlineWidth;   // screen-space fraction of half-width
    float3 _pad0;
};

struct VSIn  { float3 pos : POSITION; float3 nrm : NORMAL; };
struct VSOut { float4 pos : SV_POSITION; };

VSOut VSMain(VSIn input)
{
    VSOut output;
    float4 worldPos = mul(float4(input.pos, 1.0f), world);
    float4 clip = mul(worldPos, viewProj);

    // Offset in clip space along the (approx) projected normal, scaled by w so
    // the ring keeps a constant width on screen regardless of distance.
    float3 worldNrm = normalize(mul(float4(input.nrm, 0.0f), world).xyz);
    float2 clipNrm = normalize(mul(float4(worldNrm, 0.0f), viewProj).xy);
    clip.xy += clipNrm * outlineWidth * clip.w;

    output.pos = clip;
    return output;
}

float4 PSMain(VSOut input) : SV_TARGET
{
    return float4(0.0f, 0.0f, 0.0f, 1.0f);
}
