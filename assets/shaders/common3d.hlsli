#ifndef COMMON3D_HLSLI
#define COMMON3D_HLSLI

// Shared by every 3D pass. Layout must match render/r3d/FrameConstants.h.
cbuffer Frame : register(b0)
{
    row_major float4x4 viewProj;
    row_major float4x4 lightViewProj;   // world -> shadow map clip
    float4 keyDirection;   // xyz = normalised travel direction, w = intensity
    float4 keyColor;       // rgb
    float4 ambientSky;     // rgb, hemisphere fill from above
    float4 ambientGround;  // rgb, hemisphere fill from below
    float4 shadowParams;   // x = texel size, y = depth bias, z = enabled (0/1)
};

cbuffer Object : register(b1)
{
    row_major float4x4 world;
    float4 objColor;
};

Texture2D               shadowMap     : register(t1);
SamplerComparisonState  shadowSampler : register(s1);

// Hemisphere ambient: sky tint on up-facing surfaces, ground tint on down-facing.
float3 HemisphereAmbient(float3 worldNormal)
{
    return lerp(ambientGround.rgb, ambientSky.rgb, saturate(normalize(worldNormal).y * 0.5f + 0.5f));
}

// 3x3 PCF directional shadow. Returns 1 (lit) .. 0 (fully shadowed). Points
// outside the shadow map, or when shadows are disabled, are lit.
float SampleShadow(float4 shadowClip)
{
    if (shadowParams.z < 0.5f) return 1.0f;

    float3 p = shadowClip.xyz / shadowClip.w;
    float2 uv = p.xy * float2(0.5f, -0.5f) + 0.5f;
    if (uv.x < 0.0f || uv.x > 1.0f || uv.y < 0.0f || uv.y > 1.0f || p.z > 1.0f) return 1.0f;

    float depth = p.z - shadowParams.y;
    float sum = 0.0f;
    [unroll] for (int y = -1; y <= 1; ++y)
    [unroll] for (int x = -1; x <= 1; ++x)
        sum += shadowMap.SampleCmpLevelZero(shadowSampler, uv + float2(x, y) * shadowParams.x, depth);
    return sum / 9.0f;
}

// Smooth Lambert key + hemisphere ambient, key modulated by `shadow` [0..1].
float3 ApplyLighting(float3 albedo, float3 worldNormal, float shadow)
{
    float ndl = saturate(dot(normalize(worldNormal), -keyDirection.xyz));
    float3 key = keyColor.rgb * (ndl * keyDirection.w * shadow);
    return albedo * (HemisphereAmbient(worldNormal) + key);
}

// Toon key term: quantised into 4 bands at 10 / 30 / 50 degrees (black -> white),
// coloured by the key light; ambient added on top; key modulated by `shadow`.
//   shadowBias (deg)   : per-material, keeps a surface lit through a wider angle
//   bandSoftness (deg) : transition half-width at each band edge (0 = hard)
//   wrap [0..1]         : compress the angle toward the lit side (half-Lambert)
float3 ApplyCelLighting(float3 albedo, float3 worldNormal,
                        float shadowBias, float bandSoftness, float wrap, float shadow)
{
    float ndl = dot(normalize(worldNormal), -keyDirection.xyz);
    float angleDeg = degrees(acos(clamp(ndl, -1.0f, 1.0f)));

    angleDeg = lerp(angleDeg, angleDeg * 0.5f, saturate(wrap));
    angleDeg -= shadowBias;

    float s = max(bandSoftness, 0.001f);
    float lamp = 1.0f;
    lamp = lerp(lamp, 0.66f, smoothstep(10.0f - s, 10.0f + s, angleDeg));
    lamp = lerp(lamp, 0.33f, smoothstep(30.0f - s, 30.0f + s, angleDeg));
    lamp = lerp(lamp, 0.00f, smoothstep(50.0f - s, 50.0f + s, angleDeg));

    float3 key = keyColor.rgb * (lamp * keyDirection.w * shadow);
    return albedo * (HemisphereAmbient(worldNormal) + key);
}

#endif
