# 그림자 (Cascaded Directional Shadow Map)

`src/render/Dx11Renderer.cpp` + `assets/shaders/shadow.hlsl`/`shadow_instanced.hlsl` + `common3d.hlsli`. key 라이트 하나의 캐스트 그림자, 2캐스케이드.

## 방식

프레임마다 **씬 이전에** 라이트 시점 depth-only 렌더를 **캐스케이드당 1회**(지금 2회) → `Texture2DArray` 셰도우맵(슬라이스당 4096²). 씬 패스는 그 배열을 `t1` 에 받아 캐스케이드 0(근접, 좁음)부터 시도하고 밖이면 캐스케이드 1(원거리, 넓음)로 폴백해 PCF 샘플, key 항을 그림자 계수로 곱한다.

```text
Dx11Renderer::Render():
  PollHotReload
  if lighting.shadowsEnabled:
    RenderShadowMap(snapshot):
      t1 detach → viewport 4096² · m_shadowRaster(depth bias) · m_shadowDepthState
      for cascade in [0, 1]:
        OMSetRenderTargets(none, m_shadowDsv[cascade]) → Clear depth
        b0 = cascadeViewProj[cascade] (m_shadowFrameCb)
        각 IRenderPass::RenderShadow(ctx)   ← 자기 지오메트리를 world·b1 로 depth-only 드로우 (캐스케이드마다 다시 그림)
  씬 타깃 bind + clear
  if shadows: PSSetShaderResources(1, m_shadowSrv) · PSSetSamplers(1, m_shadowSampler)   ← 배열 SRV, 슬라이스 2개 모두
  각 IRenderPass::Execute
  MSAA resolve → Present
```

캐스케이드당 지오메트리를 통째로 다시 그리므로 섀도우 드로우콜이 캐스케이드 수(2) 배가 된다 — 씬 규모가 커지면 감수할 비용, §"한계".

## 구성

| 리소스 (Dx11Renderer) | 내용 |
|---|---|
| `m_shadowDepth` | `R32_TYPELESS` 4096² `Texture2DArray`(`ArraySize=kShadowCascadeCount`), `BIND_DEPTH_STENCIL \| BIND_SHADER_RESOURCE` |
| `m_shadowDsv[cascade]` | 캐스케이드당 `D32_FLOAT` `TEXTURE2DARRAY` DSV(그 슬라이스 1장만) |
| `m_shadowSrv` | `R32_FLOAT` `TEXTURE2DARRAY` SRV(슬라이스 전체) — 씬 패스가 `t1` 로 읽음 |
| `m_shadowSampler` | `COMPARISON_MIN_MAG_MIP_LINEAR`, `LESS_EQUAL`, CLAMP (`s1`) |
| `m_shadowRaster` | solid, cull none, `DepthBias 400` + `SlopeScaledDepthBias 1.5` (모든 캐스케이드 공유) |
| `m_shadowFrameCb` | `b0` = 현재 그리는 캐스케이드의 view-proj (depth 패스 전용, 캐스케이드마다 `UpdateSubresource`) |
| `kShadowCascadeCount` | `render/r3d/Lighting.h`, 2 |
| `kShadowMapSize` | `render/r3d/FrameConstants.h`, 4096 (캐스케이드당 해상도) |

- **라이트 행렬**: `SnapshotBuilder::BuildLighting` 이 `lighting.cascadeViewProj[2]` 를 만든다 — 캐스케이드 0(근접)은 **모든 씬에서 플레이어 중심 12×12×28m 박스로 고정**, 캐스케이드 1(원거리)은 씬마다 다름(`FitShadowOrtho` 호출부 참고: 씬 1 30/60/22, 씬 2 64/90/32, 씬 3 50/80/28). `Scene3D::lighting` 로 스냅샷에 값으로 전달.
- **Frame cbuffer(b0)** 확장: `cascadeViewProj[2]` + `shadowParams`(x=텍셀, y=바이어스, z=활성화). `FrameConstantsGpu` + `common3d.hlsli` 함께 — 항상 같이 고친다(CLAUDE.md 불변 규칙).
- **`IRenderPass::RenderShadow`**: 기본 빈 구현. `MeshPass3D`·`ModelMeshPass3D` 가 override 해서 POSITION-only 레이아웃 + `"shadow"` VS + null PS 로 자기 VB/IB 를 다시 그린다. b1(world)은 기존 `m_objectConstants` 재사용. 캐스케이드마다 다시 호출되므로 이 함수 안에서 캐스케이드를 몰라도 된다 — 렌더러가 b0만 바꿔 낀다.
- **수신 (`common3d.hlsli::SampleShadow`)**: VS는 `worldPos` 만 PS로 넘기고(캐스케이드별 클립 변환을 미리 계산하지 않음), PS의 `SampleShadow(worldPos)` 가 캐스케이드 0부터 `SampleShadowCascade`로 클립 변환 + 5×5 PCF `SampleCmpLevelZero` 시도 → 박스 밖(`uv`/`depth` 범위 밖)이면 캐스케이드 1로 폴백. 둘 다 밖이면 1.0(밝음). `ApplyLighting`/`ApplyCelLighting` 이 `shadow` 인자로 key 항을 곱. 그림자면은 헤미스피어 앰비언트만 남음.

## 켜고 끄기

`Scene3D::lighting.shadowsEnabled` (지금 `BuildLighting` 이 `true`). `false` 면 `RenderShadowMap` 스킵 + `shadowParams.z=0` 이라 셰이더가 그림자 계산 안 함 (분기).

## 왜 캐스케이드인가 — 씬 1/2에서 겪은 문제

단일 정사영 프레임 하나로는 "좁혀야 선명한 근접 그림자"와 "넓혀야 다 담기는 원거리 그림자"를 동시에 만족 못 한다는 게 실제로 드러난 사례:

- **씬 1(단일 캐릭터), 뿌옇다/이상하다**: 정사영 span이 22m로 2048² 맵 전체에 깔려 있었는데, 실제 캐스터는 키 ~1.8m 캐릭터 하나뿐 — 텍셀 밀도 ≈1.07cm/텍셀로 성기게 샘플되고 선형 비교 필터 + 3×3 PCF가 그걸 더 넓게 블러해 뿌옇게 보였다. 거기에 이중 바이어스(래스터라이저 `DepthBias`/`SlopeScaledDepthBias` + 셰이더 `shadowParams.y`)가 과해 발밑에서 그림자가 살짝 떨어지는 peter-panning도 있었다.
- **씬 2(군중), 경계 불명확·그라데이션 불균일**: 정사영 span이 64m(군중 전체를 담아야 해서 좁힐 수 없음)라 텍셀 밀도가 ≈3.1cm/텍셀까지 성겨졌고, 3×3 PCF가 그 성긴 텍셀을 계단처럼 그대로 보여줘 부드러운 그라데이션이 아니라 듬성듬성한 값이 이어 붙은 것처럼 보였다.

한쪽을 좁히면 다른 쪽이 못 담기고, 한쪽에 맞춰 해상도/PCF를 조정하면 다른 쪽이 어긋나는 게 근본 원인 — **정사영 프레임을 하나 더 둬서 "근접은 항상 좁게, 원거리는 씬에 맞게 넓게"를 동시에 만족**시킨 게 지금의 캐스케이드 구조. 캐스케이드 0(플레이어 중심 12m, 모든 씬 공통)이 텍셀 밀도 ≈0.29cm/텍셀(4096 기준)로 항상 선명하고, 캐스케이드 1은 씬마다 필요한 만큼만 넓혀 원거리도 놓치지 않는다.

**미검증(시각)**: 이 컨테이너는 GPU 없음(WARP도 실행 불가) — 값·수식·바인딩 정합성만 확인. 실제 캐스케이드 전환 이음새·접지감·선명도는 F5(Windows/VS)에서 확인 필요(§"씬 3"이 바로 그 확인용).

## 씬 3: 그림자/조명 쇼케이스

`Simulation::kDemoScene = 3`(지금 활성값), `SnapshotBuilder::BuildShadowShowcaseScene` — 크라우드·와글거리는 데모 액터 없이, 그래픽 확인만을 위해 배치한 정적 씬:

- 밝은 중립색 바닥(그림자 대비가 잘 보이도록) + 낮은 각도 그림자를 받는 뒷벽(acne/peter-panning이 큰 평면에서 한눈에 보임).
- 높이가 0.5m씩 올라가는 계단형 플린스 5개 — 경사면을 따라 그림자 길이/그라데이션이 어떻게 변하는지.
- 플레이어에서 3/7/11/17/24m 거리에 선 필러 5개 — **캐스케이드 0(12m 박스)의 경계가 정확히 이 사이 어딘가를 지난다**, 근접(선명)→원거리(성김) 전환 이음새가 보이는지 확인하는 용도.
- 캐릭터 바로 옆의 작은 소품 2개 — 클로즈업에서 셀 셰이딩/림 라이트(`docs/toon-fresnel-research.md`) 확인용.

캐스케이드 1은 이 씬 전용으로 50/80/28(§구성 표) — 24m 필러까지 여유 있게 담는다.

## 한계 / 다음 (미구현)

- 캐스케이드 2개, 고정 프러스텀(카메라 뷰 프러스텀을 슬라이스하는 진짜 CSM이 아니라 플레이어 중심의 동심 박스 두 개 — 구현이 훨씬 단순하지만 카메라가 아주 멀리/비스듬히 볼 때는 여전히 최적은 아님). 카메라가 두 박스 다 벗어나면 그림자 잘림 → 필요해지면 카메라 뷰 프러스텀 기반 진짜 CSM 또는 3번째 캐스케이드.
- key 라이트 1개만. 포인트/스팟 그림자는 큐브맵/추가 아틀라스 — [light-types-design.md](light-types-design.md) §6.
- PCF 5×5 고정. 소프트 섀도우(PCSS), 블러 프리패스는 없음.
- 캐스케이드마다 바이어스(`shadowParams.y`)가 같은 값 — 서로 다른 깊이 범위를 커버하는데 하나의 정규화 바이어스를 공유하는 근사. 어느 한쪽에서 acne/peter-panning이 남으면 캐스케이드별 바이어스 분리 고려.
- 셰도우맵 리사이즈 없음(고정 4096, 캐스케이드당). 품질/성능은 `kShadowMapSize` 로.
- 알파 컷아웃(머리카락)은 depth 패스에서 clip 안 함 → 컷아웃 구멍이 그림자에 안 반영. 필요하면 `shadow.hlsl` 에 PS + 텍스처.
- `DepthBias` 값은 WARP 기준. peter-panning/acne 는 실기에서 재조정.

## 사용 방법 (How to use)

**그림자 범위/방향 조정**: `SnapshotBuilder::BuildLighting` 의 `FitShadowOrtho(dir, center, span, depth, eyeDist)` 호출부 — 캐스케이드 0은 항상 플레이어 중심(모든 씬 공통, 건드릴 일 거의 없음), 캐스케이드 1은 `if constexpr (kDemoScene == ...)` 분기로 씬별 값. 새 씬을 추가하면 여기에 그 씬의 캐스케이드 1 분기를 추가한다. 텍셀 밀도(= span / `kShadowMapSize`)가 너무 떨어지면 "왜 캐스케이드인가" 절처럼 그라데이션이 계단져 보인다 — 넓히기 전에 정말 그 범위가 다 필요한지부터 확인, 그래도 필요하면 3번째 캐스케이드를 고려한다.

**바이어스 튜닝**: 표면에 줄무늬(acne) → `m_shadowRaster` 의 `DepthBias`/`SlopeScaledDepthBias` ↑ 또는 `common3d.hlsli` `SampleShadowCascade` 의 `shadowParams.y`(`FrameConstants.h` 에서 세팅) ↑. 그림자가 물체에서 떠 보이면(peter-panning) ↓. 지금은 두 캐스케이드가 값을 공유 — 한쪽만 문제면 `shadowParams`에 캐스케이드별 필드를 추가해야 한다.

**새 3D 패스가 그림자를 드리우게**: `RenderShadow(const ShadowContext&)` override — b0 는 렌더러가 캐스케이드마다 다시 바인드하니, POSITION-only 레이아웃 + `shaders.Get(device, "shadow", posLayout, 1)` 의 VS + `PSSetShader(nullptr)` 로 자기 지오메트리를 그린다(캐스케이드 수만큼 자동으로 다시 호출됨 - 패스 쪽은 신경 쓸 것 없음). world 는 b1.

**그림자를 받기만**: `common3d.hlsli` 를 쓰는 패스면 자동. VS 에서 `output.worldPos = worldPos.xyz`(TEXCOORD1), PS 에서 `SampleShadow(input.worldPos)` 를 `ApplyLighting`/`ApplyCelLighting` 에 전달 — 캐스케이드 선택은 함수 내부에서 처리되므로 호출부는 캐스케이드를 몰라도 된다.

**새 씬 추가**: `Simulation.h`의 `kDemoScene`에 값 추가 + `Simulation::SpawnActors`/`SnapshotBuilder::BuildCamera`/`BuildLighting`/`BuildScene3D`에 `if constexpr (kDemoScene == N)` 분기(씬 3처럼) — 캐스케이드 0은 그대로 두고 캐스케이드 1만 그 씬에 맞게.

**끄기**: `BuildLighting` 에서 `lighting.shadowsEnabled = false`.

**하지 말 것**: `t1` 을 detach 안 하고 셰도우맵에 쓰기(입력·출력 동시 바인드), `Frame` cbuffer 레이아웃을 `FrameConstants.h` 와 어긋나게, 씬 프러스텀보다 훨씬 큰 ortho(해상도 낭비), 캐스케이드 인덱스를 하드코딩(`kShadowCascadeCount` 대신 리터럴 `2`를 여기저기 흩뿌리기 — `Dx11Renderer.h`의 `m_shadowDsv[2]`만 예외, 그 이유는 그 파일의 주석 참고).
