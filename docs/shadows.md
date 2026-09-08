# 그림자 (Directional Shadow Map)

`src/render/Dx11Renderer.cpp` + `assets/shaders/shadow.hlsl` + `common3d.hlsli`. key 라이트 하나의 캐스트 그림자.

## 방식

프레임마다 **씬 이전에** 라이트 시점 depth-only 렌더 → 2048² 셰도우맵. 씬 패스는 그 맵을 `t1` 에 받아 PCF 로 샘플, key 항을 그림자 계수로 곱한다.

```text
Dx11Renderer::Render():
  PollHotReload
  if lighting.shadowsEnabled:
    RenderShadowMap(snapshot):
      t1 detach → OMSetRenderTargets(none, m_shadowDsv) → Clear depth
      viewport 2048² · m_shadowRaster(depth bias) · m_shadowDepthState
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
| `m_shadowDepth` | `R32_TYPELESS` 2048² Texture2D, `BIND_DEPTH_STENCIL | BIND_SHADER_RESOURCE` |
| `m_shadowDsv` | `D32_FLOAT` 뷰 |
| `m_shadowSrv` | `R32_FLOAT` 뷰 (씬 패스가 `t1` 로 읽음) |
| `m_shadowSampler` | `COMPARISON_MIN_MAG_MIP_LINEAR`, `LESS_EQUAL`, CLAMP (`s1`) |
| `m_shadowRaster` | solid, cull none, `DepthBias 1200` + `SlopeScaledDepthBias 2.5` (acne 방지) |
| `m_shadowFrameCb` | `b0` = `lightViewProj` (depth 패스 전용) |
| `kShadowMapSize` | `render/r3d/FrameConstants.h`, 2048 |

- **라이트 행렬**: `SnapshotBuilder::BuildLighting` 이 `lighting.lightViewProj` 를 만든다 — key 방향으로 `LookAtLH` + 씬을 감싸는 `OrthographicLH(22, 22, 0.1, 40)`. `Scene3D::lighting` 로 스냅샷에 값으로 전달.
- **Frame cbuffer(b0)** 확장: `lightViewProj` + `shadowParams`(x=texel, y=bias, z=enabled). `FrameConstantsGpu` + `common3d.hlsli` 함께.
- **`IRenderPass::RenderShadow`**: 기본 빈 구현. `MeshPass3D`·`ModelMeshPass3D` 가 override 해서 POSITION-only 레이아웃 + `"shadow"` VS + null PS 로 자기 VB/IB 를 다시 그린다. b1(world)은 기존 `m_objectConstants` 재사용.
- **수신 (`common3d.hlsli::SampleShadow`)**: `shadowClip` 을 VS 에서 계산해 PS 로 전달 → NDC → uv → 3×3 PCF `SampleCmpLevelZero`. 맵 밖·비활성이면 1.0(밝음). `ApplyLighting` / `ApplyCelLighting` 이 `shadow` 인자로 key 항을 곱. 그림자면은 헤미스피어 앰비언트만 남음.

## 켜고 끄기

`Scene3D::lighting.shadowsEnabled` (지금 `BuildLighting` 이 `true`). `false` 면 `RenderShadowMap` 스킵 + `shadowParams.z=0` 이라 셰이더가 그림자 계산 안 함 (분기).

## 한계 / 다음 (미구현)

- 싱글 캐스케이드 고정 프러스텀. 카메라가 씬 밖으로 나가면 그림자 잘림 → CSM 또는 카메라 추종 프러스텀.
- key 라이트 1개만. 포인트/스팟 그림자는 큐브맵/추가 아틀라스.
- PCF 3×3 고정. 소프트 섀도우(PCSS), 블러 프리패스는 없음.
- 셰도우맵 리사이즈 없음(고정 2048). 품질/성능은 `kShadowMapSize` 로.
- 알파 컷아웃(머리카락)은 depth 패스에서 clip 안 함 → 컷아웃 구멍이 그림자에 안 반영. 필요하면 `shadow.hlsl` 에 PS + 텍스처.
- `DepthBias` 값은 WARP 기준. peter-panning/acne 는 실기에서 재조정.

## 사용 방법 (How to use)

**그림자 범위/방향 조정**: `SnapshotBuilder::BuildLighting` 의 `OrthographicLH(22, 22, ...)` 크기, `center`, `eye = center - dir*16`. 씬이 커지면 ortho 크기·`kShadowMapSize` 를 키운다.

**바이어스 튜닝**: 표면에 줄무늬(acne) → `m_shadowRaster` 의 `DepthBias`/`SlopeScaledDepthBias` ↑ 또는 `common3d.hlsli` `SampleShadow` 의 `shadowParams.y`(`FrameConstants.h` 에서 세팅) ↑. 그림자가 물체에서 떠 보이면(peter-panning) ↓.

**새 3D 패스가 그림자를 드리우게**: `RenderShadow(const ShadowContext&)` override — b0 는 렌더러가 이미 `lightViewProj` 로 바인드했으니, POSITION-only 레이아웃 + `shaders.Get(device, "shadow", posLayout, 1)` 의 VS + `PSSetShader(nullptr)` 로 자기 지오메트리를 그린다. world 는 b1.

**그림자를 받기만**: `common3d.hlsli` 를 쓰는 패스면 자동. VS 에서 `output.shadowClip = mul(worldPos, lightViewProj)`, PS 에서 `SampleShadow(input.shadowClip)` 를 `ApplyLighting`/`ApplyCelLighting` 에 전달.

**끄기**: `BuildLighting` 에서 `lighting.shadowsEnabled = false`.

**하지 말 것**: `t1` 을 detach 안 하고 셰도우맵에 쓰기(입력·출력 동시 바인드), `Frame` cbuffer 레이아웃을 `FrameConstants.h` 와 어긋나게, 씬 프러스텀보다 훨씬 큰 ortho(해상도 낭비).
