# 안티에일리어싱 (MSAA)

`src/render/Dx11Renderer.cpp`. 툰 아웃라인·실루엣 같은 **지오메트리 엣지**의 계단을 없애기 위한 하드웨어 멀티샘플 AA.

## 방식

패스들은 **백버퍼가 아니라 멀티샘플 씬 타깃**에 그린다. `PostProcessPass`
(`docs/post-process-gbuffer-research.md`)가 그 타깃을 백버퍼로 합성하며 리졸브를 겸한다 — 예전엔
프레임 끝의 단순 `ResolveSubresource` 호출 하나였지만, 지금은 그 책임이 패스 하나로 옮겨갔다.

```
Render():
  OMSetRenderTargets(m_sceneColorRtv, m_sceneDepthDsv)   ← MSAA (Count = m_sampleCount)
  Clear color + depth
  지오메트리 IRenderPass::Execute (MeshPass3D, ModelMeshPass3D, DebugDrawPass, ...)
  PostProcessPass::Execute:
      OMSetRenderTargets(m_backBufferRtv, none)           ← 백버퍼로 전환, depth 없음
      합성 셰이더(멀티샘플이면 composite_ms.hlsl)가 m_sceneColor 를 직접 읽어 백버퍼에 씀
                                                            (옛 ResolveSubresource 를 대체)
  2D 오버레이 IRenderPass::Execute (QuadPass2D, SpritePass2D)  ← 그대로 백버퍼에 그림
  Present
```

- **샘플 수 선택**: `CreateDeviceAndSwapChain` 에서 `CheckMultisampleQualityLevels` 로 8 → 4 → 2 순으로 지원되는 최고값. 아무것도 안 되면 `m_sampleCount = 1` (MSAA 없음 — 이때도 `m_sceneColor` 는 여전히 실제 텍스처이고, `PostProcessPass` 가 단일 샘플용 `composite.hlsl` 로 백버퍼에 합성한다). WARP·최신 GPU 는 8x 지원.
- **씬 타깃**:
  - 색: `m_sceneColor` (Texture2D, `SampleDesc.Count = m_sampleCount`, `BIND_RENDER_TARGET | BIND_SHADER_RESOURCE`) + `m_sceneColorRtv` + `m_sceneColorSrv`. **샘플수와 무관하게 항상 실제 텍스처** — 예전엔 1x 일 때 백버퍼 RTV 를 그대로 `AddRef` 해서 재사용했지만, 백버퍼는 셰이더 리소스로 바인드할 수 없어(스왑체인이 `DXGI_USAGE_RENDER_TARGET_OUTPUT` 로만 생성됨) `PostProcessPass` 가 읽으려면 항상 별도 텍스처가 있어야 한다(`docs/post-process-gbuffer-research.md` §3.1).
  - 깊이: `m_sceneDepth` (`R24G8_TYPELESS` + `BIND_DEPTH_STENCIL | BIND_SHADER_RESOURCE`, 같은 `SampleDesc`) + `m_sceneDepthDsv`(`D24_UNORM_S8_UINT` 뷰) + `m_sceneDepthSrv`(아직 아무도 안 읽음, G-버퍼용 선행 준비). 색·깊이의 Count/Quality 는 항상 같아야 `OMSetRenderTargets` 성공.
- **백버퍼**: `m_backBufferRtv` 는 `PostProcessPass` 의 합성 목적지이고, 그 뒤로 프레임 끝까지(2D 오버레이) 실제 렌더 대상이다 — 더 이상 "resolve 전용, pass가 안 그리는 대상"이 아니다.
- **리사이즈**: `ResizeBackBuffer` 가 씬 타깃 + 백버퍼 RTV 를 해제 → `ResizeBuffers` → 둘 다 재생성.
- **래스터라이저**: 3D 패스(`MeshPass3D`, `ModelMeshPass3D`)의 상태에 `MultisampleEnable = TRUE`. (삼각형 커버리지 MSAA 는 이 플래그와 무관하게 MSAA 타깃이면 동작하지만 명시.)

## 한계

- **셰이딩 불연속은 AA 안 됨**: 셀 셰이더의 4밴드 경계는 지오메트리 엣지가 아니라 픽셀 셰이더가 만든 색 단차라 MSAA 가 못 잡는다. 부드럽게 하려면 `common3d.hlsli` `ApplyCelLighting` 의 각 밴드 경계를 `smoothstep` 으로 (전이폭 파라미터), 또는 포스트 AA(FXAA/SMAA).
- **알파 컷아웃 엣지**: `clip()` 하는 머리카락·속눈썹 가장자리는 alpha-to-coverage 를 켜야 MSAA 가 부드럽게 잡는다 (`D3D11_BLEND_DESC::AlphaToCoverageEnable`, 미설정).
- **비용**: 8x 는 색·깊이 대역폭 8배 + resolve 1회/프레임. 부담되면 `CreateDeviceAndSwapChain` 의 후보 배열을 `{ 4u, 2u }` 등으로.
- 2D 패스(`QuadPass2D`)는 기본 래스터라이저(`MultisampleEnable = FALSE`)지만 스크린 정렬 사각형이라 실익 없음.

## 다음 (미구현)

- alpha-to-coverage (컷아웃 엣지)
- ~~resolve 를 커스텀 셰이더 다운샘플로 바꿔 톤매핑·샤픈 결합~~ — **오프스크린 RT 인프라 +
  커스텀 셰이더 리졸브 + SSAO·안개는 됨**(`PostProcessPass`, 위). 톤매핑·샤픈은 아직 —
  `docs/post-process-gbuffer-research.md` §9.
- 셀 밴드 단차용 포스트 AA(FXAA/SMAA) — 인프라는 있으니 `PostProcessPass` 에 패스 하나 추가하면 됨
- 샘플 수를 `FrameSettings` 로 노출 (런타임/옵션 메뉴)

## 사용 방법 (How to use)

**샘플 수 바꾸기**: `Dx11Renderer::CreateDeviceAndSwapChain` 의 후보 리스트 `{ 8u, 4u, 2u }` 수정. 1x 강제하려면 리스트를 비우거나 `m_sampleCount = 1` 고정.

**새 지오메트리 패스가 씬 타깃에 그리려면**: `PassContext::renderTarget` / `depthStencil` 이 이미 MSAA 뷰를 가리킨다(지오메트리 스테이지 동안). 자체 RT 를 쓰는 패스(그림자맵 등)는 같은 `m_sampleCount` 로 만들거나 1x 로 만들고 따로 관리. **새 포스트/오버레이 패스**(지오메트리 다음, `PostProcessPass`/`QuadPass2D` 근처)는 대신 `PassContext::backBufferRenderTarget`/`sceneColorSrv`/`sceneSampleCount` 를 쓴다 — `docs/post-process-gbuffer-research.md` §2.

**셀 밴드까지 부드럽게**: MSAA 로는 안 됨. `common3d.hlsli` 에서 각 `if` 경계를 `lerp(a, b, smoothstep(edge - w, edge + w, angleDeg))` 로 바꾸고 `w`(전이폭 도) 파라미터 추가. 저장 즉시 핫리로드.

**하지 말 것**: 색·깊이 씬 타깃의 `SampleDesc` 불일치, `PostProcessPass` 가 백버퍼로 전환하기 전에(즉 지오메트리 스테이지 도중) 백버퍼를 렌더 대상으로 바인드, `m_sampleCount == 1` 인데 `composite_ms.hlsl`(멀티샘플 전용) 을 고르기.
