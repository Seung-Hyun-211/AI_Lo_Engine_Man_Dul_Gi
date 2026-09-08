#ifndef COMMON3D_HLSLI
#define COMMON3D_HLSLI

// Shared by every 3D pass. Layout must match render/r3d/FrameConstants.h.
cbuffer Frame : register(b0)
{
    row_major float4x4 viewProj;
    float4 keyDirection;   // xyz = normalised travel direction, w = intensity
    float4 keyColor;       // rgb
    float4 ambientColor;   // rgb
};

cbuffer Object : register(b1)
{
    row_major float4x4 world;
    float4 objColor;
};

// Smooth Lambert key + flat ambient. albedo is the surface colour.
float3 ApplyLighting(float3 albedo, float3 worldNormal)
{
    float ndl = saturate(dot(normalize(worldNormal), -keyDirection.xyz));
    float3 key = keyColor.rgb * (ndl * keyDirection.w);
    return albedo * (ambientColor.rgb + key);
}

// Toon: the key term is quantised into 4 bands at 10 / 30 / 50 degrees
// (black -> white), then coloured by the key light; ambient is added on top so
// the darkest band is not fully black unless ambient is 0.
float3 ApplyCelLighting(float3 albedo, float3 worldNormal, float lampFloor)
{
    float ndl = dot(normalize(worldNormal), -keyDirection.xyz);   // -1 .. 1
    float angleDeg = degrees(acos(clamp(ndl, -1.0f, 1.0f)));      // 0 .. 180

    float lamp;
    if      (angleDeg < 10.0f) lamp = 1.00f;
    else if (angleDeg < 30.0f) lamp = 0.66f;
    else if (angleDeg < 50.0f) lamp = 0.33f;
    else                       lamp = 0.00f;
    lamp = max(lamp, lampFloor);

    float3 key = keyColor.rgb * (lamp * keyDirection.w);
    return albedo * (ambientColor.rgb + key);
}

#endif
