# 그림자 (Directional Shadow Map)

`src/render/Dx11Renderer.cpp` + `assets/shaders/shadow.hlsl` + `common3d.hlsli`. key 라이트 하나의 캐스트 그림자.

## 방식

프레임마다 **씬 이전에** 라이트 시점 depth-only 렌더 → 4096² 셰도우맵. 씬 패스는 그 맵을 `t1` 에 받아 PCF 로 샘플, key 항을 그림자 계수로 곱한다.

```text
Dx11Renderer::Render():
  PollHotReload
  if lighting.shadowsEnabled:
    RenderShadowMap(snapshot):
      t1 detach → OMSetRenderTargets(none, m_shadowDsv) → Clear depth
      viewport 4096² · m_shadowRaster(depth bias) · m_shadowDepthState
      b0 = lightViewProj (m_shadowFrameCb)
      각 IRenderPass::RenderShadow(ctx)   ← 자기 지오메트리를 world·b1 로 depth-only 드로우
  씬 타깃 bind + clear
  if shadows: PSSetShaderResources(1, m_shadowSrv) · PSSetSamplers(1, m_shadowSampler)
  각 IRenderPass::Execute
  MSAA resolve → Present
```

## 구성

| 리소스 (Dx11Renderer) | 내용 |
|---|---|
| `m_shadowDepth` | `R32_TYPELESS` 4096² Texture2D, `BIND_DEPTH_STENCIL | BIND_SHADER_RESOURCE` |
| `m_shadowDsv` | `D32_FLOAT` 뷰 |
| `m_shadowSrv` | `R32_FLOAT` 뷰 (씬 패스가 `t1` 로 읽음) |
| `m_shadowSampler` | `COMPARISON_MIN_MAG_MIP_LINEAR`, `LESS_EQUAL`, CLAMP (`s1`) |
| `m_shadowRaster` | solid, cull none, `DepthBias 400` + `SlopeScaledDepthBias 1.5` (acne 방지, §"뿌옇다" 참고 — 예전 1200/2.5는 과했음) |
| `m_shadowFrameCb` | `b0` = `lightViewProj` (depth 패스 전용) |
| `kShadowMapSize` | `render/r3d/FrameConstants.h`, 4096 (씬 2 대응, §"경계가 불명확" 참고 — 예전 2048) |

- **라이트 행렬**: `SnapshotBuilder::BuildLighting` 이 `lighting.lightViewProj` 를 만든다 — key 방향으로 `LookAtLH` + 씬을 감싸는 `OrthographicLH`. 씬 1(단일 캐릭터)은 `(12, 12, 0.1, 28)`, 씬 2(군중)는 `(64, 64, 0.1, 90)` — 씬마다 다른 이유는 아래 "뿌옇다" 참고. `Scene3D::lighting` 로 스냅샷에 값으로 전달.
- **Frame cbuffer(b0)** 확장: `lightViewProj` + `shadowParams`(x=texel, y=bias, z=enabled). `FrameConstantsGpu` + `common3d.hlsli` 함께.
- **`IRenderPass::RenderShadow`**: 기본 빈 구현. `MeshPass3D`·`ModelMeshPass3D` 가 override 해서 POSITION-only 레이아웃 + `"shadow"` VS + null PS 로 자기 VB/IB 를 다시 그린다. b1(world)은 기존 `m_objectConstants` 재사용.
- **수신 (`common3d.hlsli::SampleShadow`)**: `shadowClip` 을 VS 에서 계산해 PS 로 전달 → NDC → uv → 5×5 PCF `SampleCmpLevelZero`(§"경계가 불명확" — 예전 3×3). 맵 밖·비활성이면 1.0(밝음). `ApplyLighting` / `ApplyCelLighting` 이 `shadow` 인자로 key 항을 곱. 그림자면은 헤미스피어 앰비언트만 남음.

## 켜고 끄기

`Scene3D::lighting.shadowsEnabled` (지금 `BuildLighting` 이 `true`). `false` 면 `RenderShadowMap` 스킵 + `shadowParams.z=0` 이라 셰이더가 그림자 계산 안 함 (분기).

## "뿌옇다/이상하다" 였던 이유 → 지금

캐릭터 그림자가 흐릿하고 붕 떠 보인다는 피드백. 원인 두 가지가 겹쳐 있었다:

1. **씬 1(단일 캐릭터)의 정사영 프레임이 너무 넓었다.** span 22m가 2048² 맵 전체에 깔려 있었는데, 실제로 그림자를 드리우는 건 캐릭터 하나(키 ~1.8m)뿐 — 텍셀 밀도가 22m/2048px ≈ 1.07cm/텍셀이라 캐릭터 실루엣이 맵의 극히 일부만 차지해 픽셀당 텍셀이 성기게 샘플되고, `SampleShadow`의 선형 비교 필터(`COMPARISON_MIN_MAG_MIP_LINEAR`) + 3×3 PCF가 그 성긴 텍셀을 더 넓게 블러해 "뿌옇게" 보였다. → span 12m·depth 28m로 좁혀 텍셀 밀도 약 1.8배(≈0.586cm/텍셀).
2. **바이어스가 이중으로 과했다.** 래스터라이저 `DepthBias`(맵을 쓸 때 미는 값)와 셰이더 쪽 `shadowParams.y`(샘플링 시 비교 깊이에서 빼는 값) 둘 다 크게 잡혀 있어(1200/2.5 + 0.0018) 그림자가 캐릭터 발밑에서 살짝 떨어져 보이는 peter-panning이 있었다 — "이상하다"의 정체. → `DepthBias 400`/`SlopeScaledDepthBias 1.5` + `shadowParams.y 0.0012`로 낮춰 접지감을 개선.

씬 2(군중, 넓은 필드)는 카메라·크라우드가 훨씬 넓게 퍼져 있어 프레임을 좁힐 수 없다 — 지금도 span 64m 그대로. 좁은 씬일수록 그림자 프레임을 씬에 맞추는 게 해상도보다 먼저 챙길 레버.

## 씬 2: "경계가 불명확하고 그라데이션이 일정하지 않다"

씬 1과는 원인이 다르다 — 씬 1은 "과도하게 블러됨"(위 §1)이었지만, 씬 2는 **텍셀 자체가 너무 성겨서** 부드러운 그라데이션을 만들 재료가 없는 쪽이다.

- 씬 2 정사영 span은 64m(군중 전체를 담아야 해서 좁힐 수 없음, 바로 위 문단). 예전 `kShadowMapSize=2048`에서 텍셀 밀도는 64/2048 ≈ **3.1cm/텍셀** — 씬 1(12m span, ≈0.6cm/텍셀)의 5배 이상 성기다.
- 3×3 PCF는 텍셀 3개(≈9.4cm) 폭만 훑는데, 이건 인스턴스드 크라우드 캐릭터의 팔다리 굵기 정도라 그 경계가 텍셀 하나하나의 계단으로 드러난다 — "부드러운 그라데이션"이 아니라 "듬성듬성한 값이 이어 붙은 것"처럼 보임(경계 불명확 + 그라데이션 불균일의 정체). 게다가 600~1,500개 별도 인스턴스가 촘촘히 서 있어 서로 다른 깊이값이 근접 텍셀에 섞이는 것도 얼룩(blotchy)을 거든다.
- 대응: **`kShadowMapSize` 2048 → 4096**(텍셀 밀도 2배, 씬2 ≈1.6cm/텍셀) + **PCF 3×3 → 5×5**(블러 폭을 유지/확대해 남은 성김을 가려 그라데이션을 다시 매끈하게). 씬 1도 같은 맵을 공유하므로 덩달아 더 선명해짐(≈0.29cm/텍셀) — §1의 "블러 과함" 진단과 상충하지 않는지 유의(더 뿌옇게 느껴지면 씬 1 쪽 PCF만 별도로 3×3으로 되돌리는 것도 고려, 지금은 공유 함수라 두 씬이 같은 커널을 쓴다).
- 근본 해결은 **캐스케이드 섀도우맵(CSM)**: 씬 1처럼 좁은 근접 캐스케이드 + 씬 2처럼 넓은 원거리 캐스케이드를 따로 둬서 "좁힐 수 없는 넓은 씬"과 "고밀도가 필요한 근접 씬"을 동시에 만족 — 지금은 미구현, 아래 "한계/다음" 참고.

**미검증(시각)**: 이 컨테이너는 GPU 없음(WARP도 실행 불가) — 값·수식 정합성만 확인. 실제 접지감·선명도는 F5(Windows/VS)에서 확인 필요. 그래도 뿌옇거나 들떠 보이면(씬 1): `shadowParams.y`를 0.0008 부근까지 더 낮춰본다(acne 재발 시 다시 올림). 씬 2가 여전히 계단져 보이면: PCF를 7×7로 더 넓히거나(비용↑) `kShadowMapSize`를 8192로(메모리 4배, ≈256MB) — 둘 다 근본 해결(CSM)의 임시방편임을 감안.

## 한계 / 다음 (미구현)

- 싱글 캐스케이드 고정 프러스텀. 카메라가 씬 밖으로 나가면 그림자 잘림 → **CSM**(씬 1/2의 텍셀 밀도 딜레마를 근본적으로 푸는 방법, 위 "씬 2" 절 참고) 또는 카메라 추종 프러스텀.
- key 라이트 1개만. 포인트/스팟 그림자는 큐브맵/추가 아틀라스.
- PCF 5×5 고정(예전 3×3). 소프트 섀도우(PCSS), 블러 프리패스는 없음.
- 셰도우맵 리사이즈 없음(고정 4096, 예전 2048). 품질/성능은 `kShadowMapSize` 로.
- 알파 컷아웃(머리카락)은 depth 패스에서 clip 안 함 → 컷아웃 구멍이 그림자에 안 반영. 필요하면 `shadow.hlsl` 에 PS + 텍스처.
- `DepthBias` 값은 WARP 기준. peter-panning/acne 는 실기에서 재조정.

## 사용 방법 (How to use)

**그림자 범위/방향 조정**: `SnapshotBuilder::BuildLighting` 의 `span`/`depth`(`OrthographicLH` 크기), `center`, `eye = center - dir * 거리` — 씬별로 분기됨(씬 1: 12/28/11, 씬 2: 64/90/32). 씬이 커지면 ortho 크기를 키우되, 텍셀 밀도(= span / `kShadowMapSize`)가 너무 떨어지면 "씬 2" 절처럼 그라데이션이 계단져 보인다 — 넓히기 전에 정말 그 범위가 다 필요한지부터 확인.

**바이어스 튜닝**: 표면에 줄무늬(acne) → `m_shadowRaster` 의 `DepthBias`/`SlopeScaledDepthBias` ↑ 또는 `common3d.hlsli` `SampleShadow` 의 `shadowParams.y`(`FrameConstants.h` 에서 세팅) ↑. 그림자가 물체에서 떠 보이면(peter-panning) ↓.

**새 3D 패스가 그림자를 드리우게**: `RenderShadow(const ShadowContext&)` override — b0 는 렌더러가 이미 `lightViewProj` 로 바인드했으니, POSITION-only 레이아웃 + `shaders.Get(device, "shadow", posLayout, 1)` 의 VS + `PSSetShader(nullptr)` 로 자기 지오메트리를 그린다. world 는 b1.

**그림자를 받기만**: `common3d.hlsli` 를 쓰는 패스면 자동. VS 에서 `output.shadowClip = mul(worldPos, lightViewProj)`, PS 에서 `SampleShadow(input.shadowClip)` 를 `ApplyLighting`/`ApplyCelLighting` 에 전달.

**끄기**: `BuildLighting` 에서 `lighting.shadowsEnabled = false`.

**하지 말 것**: `t1` 을 detach 안 하고 셰도우맵에 쓰기(입력·출력 동시 바인드), `Frame` cbuffer 레이아웃을 `FrameConstants.h` 와 어긋나게, 씬 프러스텀보다 훨씬 큰 ortho(해상도 낭비).
