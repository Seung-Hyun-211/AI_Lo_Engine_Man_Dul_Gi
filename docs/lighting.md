# 조명 (Lighting)

`src/render/r3d/Lighting.h` + `FrameConstants.h` + `assets/shaders/common3d.hlsli`.

3D 씬의 조명 상태를 값 타입으로 스냅샷에 실어 모든 3D 패스가 같은 방식으로 소비한다. 이전엔 `CameraView` 에 `lightDirection` 하나만 있어 셀 셰이더가 대부분 검게 나왔다 — 방향광만 있고 앰비언트가 없으며, 4밴드 셀의 50° 컷오프 때문에 광원에서 50° 이상 벗어난 면은 순검정이 됐다.

## 자료형 (`render/r3d/Lighting.h`)

```cpp
struct DirectionalLight {
    math::Vec3  direction{ 0.35f, -0.55f, 0.75f };  // 빛이 진행하는 방향(월드), 정규화 불필요
    math::Color color{ 1.0f, 0.96f, 0.88f, 1.0f };  // rgb = 색, a = 세기
};
struct AmbientLight {
    math::Color color{ 0.17f, 0.18f, 0.22f, 1.0f };  // 모든 면에 더해지는 균일 필
};
struct Lighting {
    DirectionalLight key{};
    AmbientLight ambient{};
};
```

`Scene3D::lighting` 로 스냅샷에 포함. `SnapshotBuilder::BuildLighting()` 이 매 프레임 값을 세팅한다 (지금은 고정값; 게임 로직·시간대·트리거로 바꿀 수 있음).

## GPU 전달 (`render/r3d/FrameConstants.h`)

`FrameConstantsGpu` 가 b0 (`cbuffer Frame`) 레이아웃과 1:1. `FillFrameConstants(camera, lighting, out)` 이 채운다 — `viewProj = view*projection`, key 방향을 **정규화**해서 넣고 `w` 에 세기, key 색, 앰비언트 색.

```hlsl
cbuffer Frame : register(b0) {
    row_major float4x4 viewProj;
    float4 keyDirection;   // xyz = 정규화된 진행방향, w = 세기
    float4 keyColor;       // rgb
    float4 ambientColor;   // rgb
};
```

`MeshPass3D` / `ModelMeshPass3D` 둘 다 `FrameConstantsGpu` + `FillFrameConstants` 를 쓴다 (중복 제거). `outline.hlsl` / `crease.hlsl` 도 `common3d.hlsli` 를 include 하므로 레이아웃은 맞아야 하지만 조명 필드는 읽지 않는다.

## 셰이딩 함수 (`common3d.hlsli`)

```hlsl
// 부드러운 램버트 key + 균일 앰비언트
float3 ApplyLighting(float3 albedo, float3 worldNormal);

// 툰: key 항을 10/30/50도에서 4밴드로 양자화(검정→흰색) 후 key 색 곱, 앰비언트 더함
float3 ApplyCelLighting(float3 albedo, float3 worldNormal, float lampFloor);
```

- `mesh.hlsl`, `model.hlsl` → `ApplyLighting`
- `cel.hlsl` → `ApplyCelLighting(base, nrm, LAMP_FLOOR)`

최종색 = `albedo * (ambientColor.rgb + keyColor.rgb * term * intensity)`. 셀은 `term` 이 {0, 0.33, 0.66, 1} 중 하나. 앰비언트가 있어 가장 어두운 밴드도 `albedo * ambient` (검정 아님). 순검정 원하면 `SnapshotBuilder::BuildLighting` 의 `ambient.color` 를 0으로.

## "시꺼멓다" 였던 이유 → 지금

| 전 | 후 |
|---|---|
| 앰비언트 없음 | `ambient.color ≈ 0.18` 회색 필 |
| 광원이 위(`-y`)에서만 → 카메라 정면(법선 ~ -z)은 50°+ → 검정 | key 방향 `{0.35,-0.55,0.75}` (전상 3/4) → 정면 chest ≈ 40° → 0.33 밴드 |
| key 색·세기 개념 없음 | `keyColor` rgb + `w` 세기 |

## 로드맵 (미구현)

- **포인트/스팟 라이트**: `Lighting` 에 `std::vector<PointLight>` (위치·반경·색). `Frame` cbuffer 를 라이트 배열(cbuffer 상한이면 StructuredBuffer)로, 셰이더에서 누적. 개수 상한·컬링 필요.
- **그림자**: key 라이트 뷰에서 depth 맵 렌더 → 셰이더에서 비교. 오프스크린 RT 인프라 선행 ([shader-pipeline.md](shader-pipeline.md) 로드맵).
- **rim 라이트**: `1 - dot(N, V)` 로 실루엣 강조 — 셀 룩에 흔함. `common3d.hlsli` 에 함수 추가, cel.hlsl 에서 더함.
- **시간대/색온도**: `BuildLighting` 이 게임 시간에서 보간.
- **라이트 프로브/IBL**: 앰비언트를 방향별로 (SH9). 나중에.

## 사용 방법 (How to use)

### 조명 바꾸기 (지금)

`src/game/SnapshotBuilder.cpp` `BuildLighting()`:
```cpp
lighting.key.direction = { 0.35f, -0.55f, 0.75f };  // 빛 진행방향
lighting.key.color     = { 1.0f, 0.96f, 0.88f, 1.0f };  // rgb + a=세기
lighting.ambient.color = { 0.17f, 0.18f, 0.22f, 1.0f };
```
재빌드 필요 (C++). 셰이딩 공식만 바꾸려면 `common3d.hlsli` 편집 → 저장 즉시 핫리로드.

### 게임 상태에 반응하는 조명

`BuildLighting()` 시그니처를 `BuildLighting(const Simulation&)` 로 바꿔 시간·이벤트에서 방향/색 보간. `Scene3D::lighting` 은 이미 스냅샷에 있으니 그 아래는 안 건드려도 됨.

### 새 3D 패스에서 조명 쓰기

`common3d.hlsli` include → `ApplyLighting(albedo, worldNormal)` 호출. C++ 쪽은 `FrameConstantsGpu frame; FillFrameConstants(scene.camera, scene.lighting, frame); UpdateSubresource(m_frameConstants, ...)` (`MeshPass3D` 참고).

### 하지 말 것

- `Frame` cbuffer 레이아웃을 `FrameConstants.h` 와 어긋나게 고치기 (둘을 항상 같이).
- 패스마다 `FrameConstants` 구조체 재정의 — `FrameConstantsGpu` 공유.
- key 방향을 셰이더에서 다시 정규화 안 하고 쓰기 (이미 `FillFrameConstants` 가 정규화함 — `-keyDirection.xyz` 바로 사용).
