// Billboard VFX particles (docs/particle-system-research.md §5.4/§7.3/§12). No
// per-vertex buffer - the VS builds a camera-facing quad from SV_VertexID and
// the camera's world-space right/up axes (Frame.view COLUMNS 0/1 - see the
// comment in VSMain; an earlier version of this file read rows instead, which
// is the bug §12's "카메라를 향하지 않아 얇게 보임" 구현 노트 tracks down). No
// texture: shape is a procedural noise-perturbed soft blob (PS), colour comes
// entirely from the per-instance colour (see "텍스처 없이" in particle-
// system-research.md).
//
// A plain circular billboard reads as "a flat sticker sliding through space"
// rather than a 3D puff/chunk (playtest feedback, §12 "3D로 보이기") - two
// fixes here: (1) the PS perturbs the circle's edge with per-instance angular
// noise so it is an irregular blob, which makes rotation actually visible;
// (2) SnapshotBuilder elongates fast-moving particles (`istretch`) along
// their direction of travel, so they read as tumbling/streaking debris
// instead of a translating disc.
#include "common3d.hlsli"

struct VSIn
{
    uint vid : SV_VertexID;
    float3 ipos : TEXCOORD1;      // world-space centre
    float isize : TEXCOORD2;      // billboard half-extent (pre-stretch)
    float irot : TEXCOORD3;       // screen-plane roll, radians
    float4 icol : COLOR0;         // already age-lerped by SnapshotBuilder
    float istretch : TEXCOORD4;   // local-X elongation, 1 = circular
};

struct VSOut
{
    float4 pos : SV_POSITION;
    float2 uv : TEXCOORD0;
    float4 col : COLOR0;
    float seed : TEXCOORD1;       // per-instance, for the PS blob noise
};

static const float2 kCorner[6] =
{
    float2(-1, -1), float2(1, -1), float2(-1, 1),
    float2(-1, 1),  float2(1, -1), float2(1, 1)
};

VSOut VSMain(VSIn i)
{
    const float2 c = kCorner[i.vid];
    const float2 sc = float2(c.x * i.istretch, c.y);   // elongate along local X, before rotating
    const float s = sin(i.irot), co = cos(i.irot);
    const float2 rc = float2(sc.x * co - sc.y * s, sc.x * s + sc.y * co);   // screen-plane roll

    // view is orthonormal (no scale) - its COLUMNS are the world-space camera
    // axes, not its rows (docs/particle-system-research.md §12 구현 노트
    // "카메라를 향하지 않아 얇게 보임" - math::LookAtLH packs xAxis/yAxis/zAxis
    // one component per row, i.e. column-major per axis: column 0 = xAxis,
    // column 1 = yAxis. Reading rows here gave a vector that mixes one
    // component of each axis - usually still roughly plausible-looking, but
    // its length shrinks toward zero at particular yaw angles, which is
    // exactly the "thin at 45/135/225/315°" symptom reported).
    const float3 camRight = float3(view._11, view._21, view._31);
    const float3 camUp    = float3(view._12, view._22, view._32);
    const float3 worldOffset = (camRight * rc.x + camUp * rc.y) * i.isize;

    VSOut o;
    o.pos = mul(float4(i.ipos + worldOffset, 1.0f), viewProj);
    // UV stays keyed to the pre-stretch corner `c`, not the stretched/rotated
    // `rc` - the PS's circular alpha/noise logic runs in this "logical circle"
    // space, and stretching the geometry around it is what turns the result
    // into an ellipse/blob on screen, not a warped noise pattern.
    o.uv = c * 0.5f + 0.5f;
    o.col = i.icol;
    o.seed = frac(sin(dot(i.ipos.xz, float2(12.9898f, 78.233f)) + i.ipos.y * 37.719f) * 43758.5453f);
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

    // 8 angular buckets around the particle, hashed per-instance (i.seed) and
    // smoothly interpolated - perturbs the effective edge radius so the
    // silhouette is an irregular blob instead of a perfect circle. No
    // texture: this noise *is* the shape.
    const float turns = (ang / 6.2831853f + 0.5f) * 8.0f;
    const float bucket = floor(turns);
    const float n0 = Hash21(float2(bucket, i.seed * 91.7f));
    const float n1 = Hash21(float2(bucket + 1.0f, i.seed * 91.7f));
    const float noise = lerp(n0, n1, frac(turns));

    const float radius = 0.78f + noise * 0.35f;   // 0.78..1.13 - irregular but still recognisably round
    const float d = length(c) / radius;
    const float alpha = saturate(1.0f - d);
    const float soft = alpha * alpha;   // bias brightness toward the centre
    return float4(i.col.rgb, i.col.a * soft);
}
