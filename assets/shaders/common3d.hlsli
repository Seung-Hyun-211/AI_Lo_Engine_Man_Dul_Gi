#ifndef COMMON3D_HLSLI
#define COMMON3D_HLSLI

// Shared by every 3D pass. b0 = per-frame (camera + light), b1 = per-object.
cbuffer Frame  : register(b0) { row_major float4x4 viewProj; float4 lightDir; };
cbuffer Object : register(b1) { row_major float4x4 world;    float4 objColor; };

// One directional light, Lambert diffuse, flat ambient term.
float3 ApplyDirectionalLight(float3 albedo, float3 worldNormal, float ambient)
{
    float3 n = normalize(worldNormal);
    float ndotl = saturate(dot(n, -normalize(lightDir.xyz)));
    return albedo * (ambient + (1.0f - ambient) * ndotl);
}

#endif
