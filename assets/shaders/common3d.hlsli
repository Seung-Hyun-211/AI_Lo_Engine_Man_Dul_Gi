#ifndef COMMON3D_HLSLI
#define COMMON3D_HLSLI

// Must match render/r3d/Lighting.h kShadowCascadeCount.
#define SHADOW_CASCADE_COUNT 2

// Shared by every 3D pass. Layout must match render/r3d/FrameConstants.h.
cbuffer Frame : register(b0)
{
    row_major float4x4 viewProj;
    row_major float4x4 view;            // camera view alone (world -> view space)
    row_major float4x4 cascadeViewProj[SHADOW_CASCADE_COUNT];   // world -> shadow map clip, per cascade
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

// One array slice per cascade (docs/shadows.md "캐스케이드") - a single SRV/
// sampler covers both, indexed by slice in SampleShadow.
Texture2DArray          shadowMap     : register(t1);
SamplerComparisonState  shadowSampler : register(s1);

// Hemisphere ambient: sky tint on up-facing surfaces, ground tint on down-facing.
float3 HemisphereAmbient(float3 worldNormal)
{
    return lerp(ambientGround.rgb, ambientSky.rgb, saturate(normalize(worldNormal).y * 0.5f + 0.5f));
}

// World-space normal -> view-space normal, for a geometry pass's G-buffer output
// (docs/post-process-gbuffer-research.md §3.3/§3.4). `view` has no scale, so a
// plain 3x3 rotation is enough - no inverse-transpose needed.
float3 WorldToViewNormal(float3 worldNormal)
{
    return normalize(mul(float4(worldNormal, 0.0f), view).xyz);
}

// Two-target output for the geometry stage: colour plus the view-space normal
// G-buffer (docs/post-process-gbuffer-research.md §3.4). A pass with no
// meaningful normal to contribute (outline, debug lines) just returns a plain
// `float4 : SV_TARGET` instead of this struct - D3D11 leaves an unwritten
// render target slot at its cleared value for that pixel, no error.
struct GeometryPSOut
{
    float4 color  : SV_TARGET0;
    float4 normal : SV_TARGET1;
};

// 5x5 PCF sample against one cascade slice. Returns 1 (lit) .. 0 (fully
// shadowed), or -1 if `worldPos` falls outside this cascade's box (caller
// tries the next cascade). shadowParams.y (bias) is shared by every cascade -
// an approximation, since each covers a different depth range (docs/shadows.md).
float SampleShadowCascade(int cascade, float3 worldPos)
{
    float4 clip = mul(float4(worldPos, 1.0f), cascadeViewProj[cascade]);
    float3 p = clip.xyz / clip.w;
    float2 uv = p.xy * float2(0.5f, -0.5f) + 0.5f;
    if (uv.x < 0.0f || uv.x > 1.0f || uv.y < 0.0f || uv.y > 1.0f || p.z < 0.0f || p.z > 1.0f)
        return -1.0f;

    float depth = p.z - shadowParams.y;
    float sum = 0.0f;
    [unroll] for (int y = -2; y <= 2; ++y)
    [unroll] for (int x = -2; x <= 2; ++x)
        sum += shadowMap.SampleCmpLevelZero(shadowSampler, float3(uv + float2(x, y) * shadowParams.x, cascade), depth);
    return sum / 25.0f;
}

// Directional shadow, cascaded (docs/shadows.md "캐스케이드"): tries cascade 0
// (tight box around the player - crisp near shadow) first, falls back to
// cascade 1 (wide box - a crowd field, or just a safety margin) when the point
// falls outside it. Was a single fixed frustum + 3x3 PCF; see SampleShadowCascade
// for why 5x5. Returns 1 (lit) .. 0 (fully shadowed). Shadows disabled, or
// outside every cascade, is lit.
float SampleShadow(float3 worldPos)
{
    if (shadowParams.z < 0.5f) return 1.0f;

    [unroll] for (int cascade = 0; cascade < SHADOW_CASCADE_COUNT; ++cascade)
    {
        float lit = SampleShadowCascade(cascade, worldPos);
        if (lit >= 0.0f) return lit;
    }
    return 1.0f;
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

// Banded rim/fresnel light from the grazing view angle (view-space N.V). No new
// Frame field needed - the camera sits at the view-space origin, so `view`
// (added for SSAO, docs/post-process-gbuffer-research.md §3.3) is all this
// needs. threshold/softness use the same vocabulary as ApplyCelLighting's band
// edges (0..1 grazing instead of degrees). docs/toon-fresnel-research.md.
float RimLight(float3 worldNormal, float3 worldPos, float threshold, float softness)
{
    float3 viewPos = mul(float4(worldPos, 1.0f), view).xyz;
    float3 v = normalize(-viewPos);
    float3 n = normalize(WorldToViewNormal(worldNormal));
    float grazing = 1.0f - saturate(dot(n, v));
    float s = max(softness, 0.001f);
    return smoothstep(threshold - s, threshold + s, grazing);
}

#endif
