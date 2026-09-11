# 라이트 타입 설계: 포인트(전구) · 스팟(손전등) · 방향광(태양)

설계만. 미구현. 지금은 `Scene3D::lighting`([lighting.md](lighting.md))이 방향광(key) 1개 + 헤미스피어 앰비언트뿐 — `lighting.md` 로드맵의 "포인트/스팟 라이트" 항목을 구체화한 문서.

## 1. 세 가지 타입이 다른 것

| 타입 | 예 | 위치 | 방향 | 감쇠 |
|---|---|---|---|---|
| 방향광(Directional) | 태양 — **이미 구현됨** | 없음(무한 원점) | 고정 벡터 하나 | 없음(전 씬 동일 세기) |
| 포인트(Point) | 전구, 횃불 | 월드 위치 | 없음(전방위) | 거리 기반, `radius` 밖은 0 |
| 스팟(Spot) | 손전등, 무대 조명 | 월드 위치 | 콘 축 벡터 | 거리 기반 × 콘 각도 기반(원 밖은 0) |

손전등은 "스팟 + 위치/방향이 카메라(또는 캐릭터 손)에 매 프레임 종속"인 특수 케이스일 뿐, 새 타입이 아니다 — §7.

## 2. 데이터 모델 (`render/r3d/Lighting.h` 확장안)

```cpp
struct PointLight
{
    math::Vec3  position{};
    float       radius{ 5.0f };     // 이 거리에서 감쇠 0으로 수렴 (§4)
    math::Color color{ 1.0f, 0.9f, 0.7f, 1.0f };   // rgb + a = 세기
};

struct SpotLight
{
    math::Vec3  position{};
    math::Vec3  direction{ 0.0f, 0.0f, 1.0f };   // 콘이 향하는 방향, 정규화 불필요
    float       range{ 10.0f };
    float       innerConeCos{ 0.97f };   // cos(내부 각) - 이 안쪽은 감쇠 없음
    float       outerConeCos{ 0.86f };   // cos(외부 각) - 이 밖은 0
    math::Color color{ 1.0f, 1.0f, 0.95f, 1.0f };
};

struct Lighting
{
    DirectionalLight key{};
    AmbientLight ambient{};
    std::vector<PointLight> pointLights;   // 값 타입 배열 - 스냅샷 규칙(불변 규칙 3) 그대로
    std::vector<SpotLight> spotLights;
    math::Mat4 lightViewProj{ math::Mat4::Identity() };   // 태양 그림자만 (§5)
    bool shadowsEnabled{ false };
};
```

`std::vector`라 스레드 경계를 값으로 넘는 기존 규칙(`RenderSnapshot`)을 그대로 만족 — `MeshInstance`/`DebugLine`과 같은 패턴.

## 3. GPU 전달 — cbuffer 배열 vs StructuredBuffer

**시작은 cbuffer 고정 배열, 라이트 수가 늘면 StructuredBuffer로.** 손전등 하나 + 전구 몇 개(방 하나에 3~5개) 규모에서는 고정 배열이 훨씬 단순하다:

```hlsl
// common3d.hlsli 또는 별도 cbuffer(register b4) - Frame(b0) 자체를 불리지 않는 편을 권장(§9)
#define MAX_POINT_LIGHTS 8
#define MAX_SPOT_LIGHTS  4
cbuffer Lights : register(b4)
{
    uint  pointLightCount;
    uint  spotLightCount;
    uint2 _lightsPad;
    float4 pointPositionRadius[MAX_POINT_LIGHTS];   // xyz = pos, w = radius
    float4 pointColorIntensity[MAX_POINT_LIGHTS];   // rgb + a = intensity
    float4 spotPositionRange[MAX_SPOT_LIGHTS];      // xyz = pos, w = range
    float4 spotDirectionOuterCos[MAX_SPOT_LIGHTS];  // xyz = dir, w = outerConeCos
    float4 spotColorIntensity[MAX_SPOT_LIGHTS];     // rgb + a = intensity
    float4 spotInnerCos[MAX_SPOT_LIGHTS];           // x = innerConeCos (y/z/w 예비)
}
```

- **별도 cbuffer(b4)로 두고 `Frame`(b0)은 안 건드리는 걸 권장** — CLAUDE.md 불변 규칙("`Frame`/`FrameConstantsGpu`는 항상 같이 고친다")의 대상을 늘리지 않는다. `common3d.hlsli`를 쓰는 모든 패스가 라이트를 강제로 알아야 할 필요는 없음(크리즈·아웃라인·그림자 depth 패스는 여전히 무시).
- 8+4개면 `float4` 배열 합계 약 400바이트 — cbuffer 64KB 한도에 전혀 문제없음.
- **라이트 수가 늘어나면(호드 씬의 다수 횃불, 다수 총구 이펙트 — `particle-system-research.md`) `StructuredBuffer<PointLightGpu> t2`로 전환**: 고정 배열은 미사용 슬롯도 루프를 돌아야 하고(또는 카운트로 조기 종료), 배열 크기를 다시 늘리려면 셰이더·C++ 양쪽을 또 고쳐야 한다. `instanced-rendering.md`가 이미 `StructuredBuffer` 패턴(인스턴스 데이터)을 쓰고 있어 선례가 있다.

## 4. 감쇠(attenuation) 수식

**포인트** — UE4 스타일 부드러운 컷오프(역제곱이 무한대로 안 가면서 `radius`에서 매끄럽게 0):
```hlsl
float PointAttenuation(float dist, float radius)
{
    float falloff = saturate(1.0f - pow(dist / radius, 4.0f));
    return (falloff * falloff) / (dist * dist + 1.0f);   // +1 로 0 나눗셈 방지
}
```

**스팟** = 포인트 감쇠 × 콘 감쇠:
```hlsl
float ConeAttenuation(float3 toLightDir, float3 spotDir, float innerCos, float outerCos)
{
    float cosAngle = dot(-toLightDir, normalize(spotDir));
    return smoothstep(outerCos, innerCos, cosAngle);   // outerCos 밖 0, innerCos 안쪽 1
}
```

## 5. 셀 셰이딩과의 통합 — 키만 밴드, 보조광은 스무스 (권장)

`ApplyCelLighting`은 지금 키 라이트 하나만 각도 4단 밴드로 양자화한다. 포인트/스팟까지 밴드로 만들면 두 광원의 밴드 경계가 얼굴 위에서 서로 어긋나며 겹쳐 계단이 지저분해진다(특히 손전등처럼 카메라에 붙어 계속 움직이는 광원은 밴드가 실시간으로 씰룩거림).

**권장**: 방향광(키)만 기존처럼 밴드 처리하고, 포인트/스팟은 **연속 감쇠 그대로 더한다** — 손전등이 얼굴을 비추면 부드러운 빛 웅덩이가 생기는 게 오히려 자연스럽고(실사 손전등도 하드 밴드로 안 끊긴다), 셀 룩의 주 표현(캐릭터 실루엣의 4단 명암)은 키 라이트가 계속 전담해 흔들리지 않는다.

```hlsl
// common3d.hlsli 확장안
float3 ApplyCelLighting(float3 albedo, float3 worldNormal,
                        float shadowBias, float bandSoftness, float wrap, float shadow)
{
    /* 기존 그대로: 키 라이트 4단 밴드 */
}

float3 ApplyPointSpotLights(float3 albedo, float3 worldNormal, float3 worldPos)
{
    float3 accum = 0;
    for (uint i = 0; i < pointLightCount; ++i)
    {
        float3 toLight = pointPositionRadius[i].xyz - worldPos;
        float dist = length(toLight);
        float ndl = saturate(dot(normalize(worldNormal), toLight / dist));
        accum += pointColorIntensity[i].rgb * pointColorIntensity[i].a
               * ndl * PointAttenuation(dist, pointPositionRadius[i].w);
    }
    // spotLightCount 루프도 같은 형태 + ConeAttenuation 곱
    return albedo * accum;
}
```

`cel.hlsl` `PSMain`에서 `lit += ApplyPointSpotLights(base, input.nrm, input.worldPos);` — `input.worldPos`는 이미 프레넬(§`docs/toon-fresnel-research.md`)에서 추가해둔 필드라 재사용.

## 6. 그림자 — 태양만, 포인트/스팟은 처음엔 없음

방향광 그림자는 이미 구현됨([shadows.md](shadows.md)). 포인트는 6면 큐브맵, 스팟은 단일 원근 맵이 필요 — 각각 셰도우맵 리소스 + 렌더 패스 반복이 추가로 필요해 비용이 크다. **1단계는 포인트/스팟을 "그림자 없는 라이트"로만 넣는다** — 방 안 전구·손전등이 물체를 비추기는 하되 그 자체가 그림자를 드리우진 않음. 그림자가 꼭 필요해지면(손전등이 캐릭터를 가려야 하는 연출 등) 스팟 하나만 원근 셰도우맵을 추가하는 걸 다음 단계로.

## 7. 손전등(플래시라이트) — 스팟의 카메라 종속 케이스

새 타입이 아니라 **매 프레임 카메라를 따라가는 `SpotLight` 하나**. `SnapshotBuilder`가 카메라 뷰 행렬에서 위치/전방을 뽑아 채운다(파티클 리서치의 카메라 billboard와 같은 자리 — `Frame.view`의 행 0/1/2를 이미 그 목적으로 쓴다, `post-process-gbuffer-research.md` §3.3):

```cpp
// SnapshotBuilder.cpp, BuildLighting 근처
render::SpotLight flashlight{};
flashlight.position = camera.EyePosition();          // CameraView에 없으면 카메라 빌더 함수가 값으로 반환하도록 확장
flashlight.direction = camera.ForwardDirection();
flashlight.range = 15.0f;
flashlight.innerConeCos = std::cos(math::ToRadians(10.0f));
flashlight.outerConeCos = std::cos(math::ToRadians(18.0f));
lighting.spotLights.push_back(flashlight);
```
껐다 켜는 토글은 `spotLights`에서 push_back을 스킵하면 그만(GPU 쪽은 `spotLightCount=0`).

## 8. 컬링 — 처음엔 없음

라이트가 몇 개뿐이면(방 하나 + 손전등 1) 모든 픽셀이 전체 라이트 배열을 도는 게 충분히 싸다. 호드/크라우드 씬처럼 라이트가 수십 개로 늘면(횃불, 다수 총구 플래시) 타일/클러스터 컬링이 필요 — `instanced-rendering.md`의 거리 컬링과 비슷한 방식으로 나중에.

## 9. 왜 `Frame`(b0)이 아니라 새 cbuffer(b4)인가

- `Frame`은 CLAUDE.md 불변 규칙 대상(C++ `FrameConstantsGpu` ↔ HLSL `cbuffer Frame` 항상 동시 수정) — 이미 뷰/그림자/조명 필드로 꽤 커졌다. 라이트 배열까지 얹으면 그 파일 하나가 점점 더 많은 걱정거리를 떠안는다.
- 라이트를 쓰는 패스(`cel.hlsl`, 나중에 `mesh.hlsl`)와 안 쓰는 패스(`outline.hlsl`, `crease.hlsl`, `shadow.hlsl`, 포스트프로세스)가 갈린다 — 별도 슬롯이면 후자는 아예 바인딩 안 해도 됨. `CelParams`(b3)가 이미 이 패턴(패스 로컬 cbuffer)이다.

## 사용 방법 (How to use, 착수 시)

**새 전구 배치**: `lighting.pointLights.push_back({ position, radius, color })` — `SnapshotBuilder`에서 씬 구성 시. GPU 반영은 `FillFrameConstants`(또는 별도 `FillLightConstants`)가 `MAX_POINT_LIGHTS` 상한까지 배열에 채우고 `pointLightCount`를 세팅.

**손전등 켜기/끄기**: `lighting.spotLights`에 push_back 하거나 비우기(§7) — 별도 온/오프 플래그 불필요.

**셀 캐릭터에 반영**: `cel.hlsl`이 `ApplyPointSpotLights`를 키 라이트 결과에 더함(§5) — 밴드는 안 건드림.

**라이트가 늘어나면**: 8개(포인트)/4개(스팟) 넘어갈 예정이 보이면 cbuffer 배열을 늘리기보다 `StructuredBuffer`로 전환(§3) — 상한 상수만 계속 올리는 건 언젠가 cbuffer 크기·루프 비용 문제로 돌아온다.

**하지 말 것**: 포인트/스팟에 `ApplyCelLighting`의 4단 밴드를 그대로 적용(§5 — 밴드 겹침 지저분함), 라이트 배열을 `Frame`(b0)에 얹기(§9), 그림자 없는 스팟에 그림자용 리소스(셰도우맵 등)를 미리 만들어두기(YAGNI — 필요해지면 §6).

## 관련 문서

[lighting.md](lighting.md)(지금 구현된 키+앰비언트, 로드맵에 이 설계의 출발점 있음), [toon-rendering.md](toon-rendering.md)/[toon-fresnel-research.md](toon-fresnel-research.md)(`cel.hlsl`의 `input.worldPos` — §5가 재사용), [shadows.md](shadows.md)(태양 그림자 구현 — §6이 포인트/스팟 그림자를 미룬 이유의 비교 대상), [particle-system-research.md](particle-system-research.md)(총구 플래시 — 나중에 포인트 라이트로 승격될 가능성), [instanced-rendering.md](instanced-rendering.md)(StructuredBuffer 선례, §3).
