// Depth-only pass from the light's point of view. The renderer runs this once
// per shadow cascade (docs/shadows.md), rebinding lightViewProj at b0 to that
// cascade's view-proj each time; each casting pass binds its per-object world
// at b1 and draws its geometry with no pixel shader.
cbuffer ShadowFrame : register(b0) { row_major float4x4 lightViewProj; };
cbuffer Object       : register(b1) { row_major float4x4 world; float4 _pad; };

float4 VSMain(float3 pos : POSITION) : SV_POSITION
{
    return mul(mul(float4(pos, 1.0f), world), lightViewProj);
}

// PSMain exists so ShaderLibrary (which compiles VSMain + PSMain) is happy; the
// renderer binds a null pixel shader for the actual depth-only draw.
void PSMain() {}
