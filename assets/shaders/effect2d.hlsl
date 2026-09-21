// 2D procedural glow VFX (docs/circular-design.md §7 option A). Same "no
// per-vertex buffer, SV_VertexID builds the quad, irregular noise-perturbed
// blob shape" idea as assets/shaders/particle.hlsl, but this module has no
// camera matrix or depth - `ipos`/screen transform mirror sprite2d.hlsl's
// direct pixel-to-NDC mapping instead of a view/projection multiply. Always
// additive (EffectPass2D's only blend state); no texture, colour comes
// entirely from the per-instance colour.
cbuffer Screen : register(b0) { float2 screen; float2 unused; };

struct VSIn
{
    uint vid : SV_VertexID;
    float2 ipos : TEXCOORD1;      // screen-space centre, pixels
    float iradius : TEXCOORD2;    // billboard half-extent, pixels
    float irot : TEXCOORD3;       // screen-plane roll, radians
    float4 icol : COLOR0;
    float iseed : TEXCOORD4;      // per-instance blob noise phase
};

struct VSOut
{
    float4 pos : SV_POSITION;
    float2 uv : TEXCOORD0;
    float4 col : COLOR0;
    float seed : TEXCOORD1;
};

static const float2 kCorner[6] =
{
    float2(-1, -1), float2(1, -1), float2(-1, 1),
    float2(-1, 1),  float2(1, -1), float2(1, 1)
};

VSOut VSMain(VSIn i)
{
    const float2 c = kCorner[i.vid];
    const float s = sin(i.irot), co = cos(i.irot);
    const float2 rc = float2(c.x * co - c.y * s, c.x * s + c.y * co);   // screen-plane roll
    const float2 screenPos = i.ipos + rc * i.iradius;

    VSOut o;
    o.pos = float4(screenPos.x / screen.x * 2.0f - 1.0f,
                   1.0f - screenPos.y / screen.y * 2.0f, 0.0f, 1.0f);
    // UV stays keyed to the pre-roll corner `c` (same reasoning as
    // particle.hlsl) - the PS's noise runs in this "logical circle" space.
    o.uv = c * 0.5f + 0.5f;
    o.col = i.icol;
    o.seed = i.iseed;
    return o;
}

float Hash21(float2 p)
{
    p = frac(p * float2(123.34f, 456.21f));
    p += dot(p, p + 45.32f);
    return frac(p.x * p.y);
}

float4 PSMain(VSOut i) : SV_TARGET
{
    const float2 c = i.uv * 2.0f - 1.0f;   // -1..1, centre at origin
    const float ang = atan2(c.y, c.x);

    // 8 angular buckets, hashed per-instance and smoothly interpolated -
    // perturbs the edge radius into an irregular blob instead of a perfect
    // circle (identical construction to particle.hlsl's PSMain).
    const float turns = (ang / 6.2831853f + 0.5f) * 8.0f;
    const float bucket = floor(turns);
    const float n0 = Hash21(float2(bucket, i.seed * 91.7f));
    const float n1 = Hash21(float2(bucket + 1.0f, i.seed * 91.7f));
    const float noise = lerp(n0, n1, frac(turns));

    const float radius = 0.78f + noise * 0.35f;
    const float d = length(c) / radius;
    const float alpha = saturate(1.0f - d);
    const float soft = alpha * alpha;   // bias brightness toward the centre
    return float4(i.col.rgb, i.col.a * soft);
}
