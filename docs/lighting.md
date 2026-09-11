# 조명 (Lighting)

`src/render/r3d/Lighting.h` + `FrameConstants.h` + `assets/shaders/common3d.hlsli`.

3D 씬의 조명 상태를 값 타입으로 스냅샷에 실어 모든 3D 패스가 같은 방식으로 소비한다. 이전엔 `CameraView` 에 `lightDirection` 하나만 있어 셀 셰이더가 대부분 검게 나왔다 — 방향광만 있고 앰비언트가 없으며, 4밴드 셀의 50° 컷오프 때문에 광원에서 50° 이상 벗어난 면은 순검정이 됐다.

## 자료형 (`render/r3d/Lighting.h`)

```cpp
struct DirectionalLight {
    math::Vec3  direction{ 0.0f, -0.70711f, 0.70711f };  // 빛 진행방향(월드), 정규화 불필요
    math::Color color{ 1.0f, 0.96f, 0.88f, 1.0f };       // rgb = 색, a = 세기(intensity)
};
struct AmbientLight {                       // 헤미스피어 필 (평면 회색보다 화사)
    math::Color sky{ 0.34f, 0.38f, 0.46f, 1.0f };     // 위 향한 면
    math::Color ground{ 0.20f, 0.18f, 0.16f, 1.0f };  // 아래 향한 면
};
struct Lighting { DirectionalLight key{}; AmbientLight ambient{}; };
```

`Scene3D::lighting` 로 스냅샷에 포함. `SnapshotBuilder::BuildLighting(elapsed)` 가 매 프레임 세팅한다:
- **빛이 움직인다**: key 방향이 정면 상단 45° 를 기준으로 좌우로 `sin(elapsed·0.5)·0.6 rad` (±34°) 스윕 (Y축 회전). 터미네이터가 얼굴을 가로질러 이동.
- key `intensity = 1.35`, 헤미스피어 앰비언트 sky/ground.

셰이더의 `HemisphereAmbient(n)` = `lerp(ground, sky, n.y·0.5+0.5)` → 위쪽은 하늘색, 아래쪽은 따뜻하게. `ApplyLighting` / `ApplyCelLighting` 둘 다 flat ambient 대신 이걸 쓴다.

## GPU 전달 (`render/r3d/FrameConstants.h`)

`FrameConstantsGpu` 가 b0 (`cbuffer Frame`) 레이아웃과 1:1. `FillFrameConstants(camera, lighting, out)` 이 채운다 — `viewProj = view*projection`, key 방향을 **정규화**해서 넣고 `w` 에 세기, key 색, 앰비언트 색.

```hlsl
cbuffer Frame : register(b0) {
    row_major float4x4 viewProj;
    float4 keyDirection;   // xyz = 정규화된 진행방향, w = 세기
    float4 keyColor;       // rgb
    float4 ambientSky;     // rgb, 위 향한 면
    float4 ambientGround;  // rgb, 아래 향한 면
};
```

`MeshPass3D` / `ModelMeshPass3D` 둘 다 `FrameConstantsGpu` + `FillFrameConstants` 를 쓴다 (중복 제거). `outline.hlsl` / `crease.hlsl` 도 `common3d.hlsli` 를 include 하므로 레이아웃은 맞아야 하지만 조명 필드는 읽지 않는다.

## 셰이딩 함수 (`common3d.hlsli`)

```hlsl
float3 HemisphereAmbient(float3 worldNormal);   // lerp(ground, sky, n.y*0.5+0.5)
float3 ApplyLighting(float3 albedo, float3 worldNormal);   // 램버트 key + 헤미스피어 ambient
float3 ApplyCelLighting(float3 albedo, float3 worldNormal,
                        float shadowBias, float bandSoftness, float wrap);   // 툰. docs/toon-rendering.md
```

- `mesh.hlsl`, `model.hlsl` → `ApplyLighting`
- `cel.hlsl` → `ApplyCelLighting` (얼굴 그림자 노브는 `docs/toon-rendering.md`)

최종색 = `albedo * (HemisphereAmbient(n) + keyColor.rgb * term * intensity)`. 셀은 `term` 이 {0, 0.33, 0.66, 1}(soft 전이). 앰비언트가 있어 가장 어두운 밴드도 검정 아님 — 순검정 원하면 `BuildLighting` 의 `ambient.sky/ground` 를 0으로.

## "너무 어두워" 대응

- key `intensity` 1.0 → 1.35, 배경 clearColor·바닥색을 밝은 청회색으로.
- flat ambient(회색 0.18) → **헤미스피어 앰비언트**(sky 0.36 / ground 0.21). 그림자면·아래면이 살아나 전체가 화사.
- 더 깊은 방법(미구현): sRGB/리니어 파이프라인(백버퍼·텍스처 `_SRGB` → 중간톤 밝기 정상화), rim 라이트(실루엣 pop), 노출/톤매핑, 두 번째 필 라이트.

## "시꺼멓다" 였던 이유 → 지금

| 전 | 후 |
|---|---|
| 앰비언트 없음 | `ambient.color ≈ 0.18` 회색 필 |
| 광원이 위(`-y`)에서만 → 카메라 정면(법선 ~ -z)은 50°+ → 검정 | key 방향 `{0.35,-0.55,0.75}` (전상 3/4) → 정면 chest ≈ 40° → 0.33 밴드 |
| key 색·세기 개념 없음 | `keyColor` rgb + `w` 세기 |

## 로드맵 (미구현)

- **포인트/스팟 라이트**: 설계 완료(미구현) — [light-types-design.md](light-types-design.md). `Frame`이 아니라 별도 cbuffer(b4)에 고정 배열로 시작, 개수 늘면 StructuredBuffer.
- **그림자**: key 라이트 뷰에서 depth 맵 렌더 → 셰이더에서 비교. 오프스크린 RT 인프라 선행 ([shader-pipeline.md](shader-pipeline.md) 로드맵).
- **rim 라이트**: `1 - dot(N, V)` 로 실루엣 강조 — 셀 룩에 흔함. `common3d.hlsli` 에 함수 추가, cel.hlsl 에서 더함.
- **시간대/색온도**: `BuildLighting` 이 게임 시간에서 보간.
- **라이트 프로브/IBL**: 앰비언트를 방향별로 (SH9). 나중에.

## 사용 방법 (How to use)

### 조명 바꾸기 (지금)

`src/game/SnapshotBuilder.cpp` `BuildLighting(float elapsed)`:
```cpp
lighting.key.direction = { 0.70711f * s, -0.70711f, 0.70711f * c };  // s/c = sin/cos(스윕)
lighting.key.color     = { 1.0f, 0.97f, 0.90f, 1.35f };   // rgb + a=세기
lighting.ambient.sky    = { 0.36f, 0.40f, 0.48f, 1.0f };
lighting.ambient.ground = { 0.22f, 0.20f, 0.18f, 1.0f };
```
재빌드 필요 (C++). 셰이딩 공식·`HemisphereAmbient` 만 바꾸려면 `common3d.hlsli` → 핫리로드. 빛 스윕 속도/폭은 `elapsed * 0.5f` / `* 0.6f` 상수.

### 화면이 너무 어두우면

`key.color.a`(intensity) ↑, `ambient.sky/ground` ↑, `RenderSnapshot::clearColor` / 바닥 `MeshDraw.color` ↑. 근본적으론 sRGB/리니어 파이프라인 (백버퍼·텍스처 `_SRGB`) — 미구현, `docs/msaa.md` 인근에 추가 예정.

### 새 3D 패스에서 조명 쓰기

`common3d.hlsli` include → `ApplyLighting(albedo, worldNormal)` 호출. C++ 쪽은 `FrameConstantsGpu frame; FillFrameConstants(scene.camera, scene.lighting, frame); UpdateSubresource(m_frameConstants, ...)` (`MeshPass3D` 참고).

### 하지 말 것

- `Frame` cbuffer 레이아웃을 `FrameConstants.h` 와 어긋나게 고치기 (둘을 항상 같이).
- 패스마다 `FrameConstants` 구조체 재정의 — `FrameConstantsGpu` 공유.
- key 방향을 셰이더에서 다시 정규화 안 하고 쓰기 (이미 `FillFrameConstants` 가 정규화함 — `-keyDirection.xyz` 바로 사용).
