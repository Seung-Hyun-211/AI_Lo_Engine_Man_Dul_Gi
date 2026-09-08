#ifndef COMMON3D_HLSLI
#define COMMON3D_HLSLI

// Shared by every 3D pass. Layout must match render/r3d/FrameConstants.h.
cbuffer Frame : register(b0)
{
    row_major float4x4 viewProj;
    float4 keyDirection;   // xyz = normalised travel direction, w = intensity
    float4 keyColor;       // rgb
    float4 ambientSky;     // rgb, hemisphere fill from above
    float4 ambientGround;  // rgb, hemisphere fill from below
};

cbuffer Object : register(b1)
{
    row_major float4x4 world;
    float4 objColor;
};

// Hemisphere ambient: sky tint on up-facing surfaces, ground tint on down-facing.
float3 HemisphereAmbient(float3 worldNormal)
{
    return lerp(ambientGround.rgb, ambientSky.rgb, saturate(normalize(worldNormal).y * 0.5f + 0.5f));
}

// Smooth Lambert key + hemisphere ambient. albedo is the surface colour.
float3 ApplyLighting(float3 albedo, float3 worldNormal)
{
    float ndl = saturate(dot(normalize(worldNormal), -keyDirection.xyz));
    float3 key = keyColor.rgb * (ndl * keyDirection.w);
    return albedo * (HemisphereAmbient(worldNormal) + key);
}

// Toon key term: quantised into 4 bands at 10 / 30 / 50 degrees (black -> white),
// coloured by the key light; ambient is added on top.
//
//   shadowBias   (deg) : subtract from the effective angle -> the surface stays
//                        lit through a wider angle. Per-material; use a larger
//                        value on the face so the self-shadow terminator does
//                        not carve it up.
//   bandSoftness (deg) : transition half-width at each band edge. 0 = hard steps.
//   wrap         [0..1]: compress the angle toward the lit side (half-Lambert
//                        analogue) so the shadow side does not fall into black.
float3 ApplyCelLighting(float3 albedo, float3 worldNormal,
                        float shadowBias, float bandSoftness, float wrap)
{
    float ndl = dot(normalize(worldNormal), -keyDirection.xyz);   // -1 .. 1
    float angleDeg = degrees(acos(clamp(ndl, -1.0f, 1.0f)));      // 0 .. 180

    angleDeg = lerp(angleDeg, angleDeg * 0.5f, saturate(wrap));
    angleDeg -= shadowBias;

    float s = max(bandSoftness, 0.001f);
    float lamp = 1.0f;
    lamp = lerp(lamp, 0.66f, smoothstep(10.0f - s, 10.0f + s, angleDeg));
    lamp = lerp(lamp, 0.33f, smoothstep(30.0f - s, 30.0f + s, angleDeg));
    lamp = lerp(lamp, 0.00f, smoothstep(50.0f - s, 50.0f + s, angleDeg));

    float3 key = keyColor.rgb * (lamp * keyDirection.w);
    return albedo * (HemisphereAmbient(worldNormal) + key);
}

#endif
