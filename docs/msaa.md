# 안티에일리어싱 (MSAA)

`src/render/Dx11Renderer.cpp`. 툰 아웃라인·실루엣 같은 **지오메트리 엣지**의 계단을 없애기 위한 하드웨어 멀티샘플 AA.

## 방식

패스들은 **백버퍼가 아니라 멀티샘플 씬 타깃**에 그린다. 프레임 끝에 백버퍼로 resolve 후 Present.

```
Render():
  OMSetRenderTargets(m_sceneColorRtv, m_sceneDepthDsv)   ← MSAA (Count = m_sampleCount)
  Clear color + depth
  각 IRenderPass::Execute
  if m_sampleCount > 1:
      OMSetRenderTargets(none)                            ← resolve 전 언바인드
      ResolveSubresource(backBuffer, 0, m_sceneColor, 0, R8G8B8A8_UNORM)
  Present
```

- **샘플 수 선택**: `CreateDeviceAndSwapChain` 에서 `CheckMultisampleQualityLevels` 로 8 → 4 → 2 순으로 지원되는 최고값. 아무것도 안 되면 `m_sampleCount = 1` (MSAA 없음, resolve 없음, `m_sceneColorRtv` 가 백버퍼 RTV 를 그대로 alias). WARP·최신 GPU 는 8x 지원.
- **씬 타깃**:
  - 색: `m_sceneColor` (Texture2D, `SampleDesc.Count = m_sampleCount`, `BIND_RENDER_TARGET`) + `m_sceneColorRtv`. 1x 면 안 만들고 백버퍼 RTV 를 `AddRef` 해서 재사용.
  - 깊이: `m_sceneDepth` (`D24_UNORM_S8_UINT`, 같은 `SampleDesc`) + `m_sceneDepthDsv`. 색·깊이의 Count/Quality 는 항상 같아야 `OMSetRenderTargets` 성공.
- **백버퍼**: `m_backBufferRtv` 는 이제 pass 렌더 대상이 **아니고** resolve 목적지일 뿐. 스왑체인은 여전히 single-sample (`DXGI_SWAP_EFFECT_DISCARD`).
- **리사이즈**: `ResizeBackBuffer` 가 씬 타깃 + 백버퍼 RTV 를 해제 → `ResizeBuffers` → 둘 다 재생성.
- **래스터라이저**: 3D 패스(`MeshPass3D`, `ModelMeshPass3D`)의 상태에 `MultisampleEnable = TRUE`. (삼각형 커버리지 MSAA 는 이 플래그와 무관하게 MSAA 타깃이면 동작하지만 명시.)

## 한계

- **셰이딩 불연속은 AA 안 됨**: 셀 셰이더의 4밴드 경계는 지오메트리 엣지가 아니라 픽셀 셰이더가 만든 색 단차라 MSAA 가 못 잡는다. 부드럽게 하려면 `common3d.hlsli` `ApplyCelLighting` 의 각 밴드 경계를 `smoothstep` 으로 (전이폭 파라미터), 또는 포스트 AA(FXAA/SMAA).
- **알파 컷아웃 엣지**: `clip()` 하는 머리카락·속눈썹 가장자리는 alpha-to-coverage 를 켜야 MSAA 가 부드럽게 잡는다 (`D3D11_BLEND_DESC::AlphaToCoverageEnable`, 미설정).
- **비용**: 8x 는 색·깊이 대역폭 8배 + resolve 1회/프레임. 부담되면 `CreateDeviceAndSwapChain` 의 후보 배열을 `{ 4u, 2u }` 등으로.
- 2D 패스(`QuadPass2D`)는 기본 래스터라이저(`MultisampleEnable = FALSE`)지만 스크린 정렬 사각형이라 실익 없음.

## 다음 (미구현)

- alpha-to-coverage (컷아웃 엣지)
- 포스트 AA 패스 (셰이딩 단차·후처리) — 오프스크린 RT 인프라와 함께
- 샘플 수를 `FrameSettings` 로 노출 (런타임/옵션 메뉴)
- resolve 를 커스텀 셰이더 다운샘플로 바꿔 톤매핑·샤픈 결합

## 사용 방법 (How to use)

**샘플 수 바꾸기**: `Dx11Renderer::CreateDeviceAndSwapChain` 의 후보 리스트 `{ 8u, 4u, 2u }` 수정. 1x 강제하려면 리스트를 비우거나 `m_sampleCount = 1` 고정.

**새 오프스크린 패스가 씬 타깃에 그리려면**: `PassContext::renderTarget` / `depthStencil` 이 이미 MSAA 뷰를 가리킨다. 자체 RT 를 쓰는 패스(그림자맵 등)는 같은 `m_sampleCount` 로 만들거나 1x 로 만들고 따로 관리.

**셀 밴드까지 부드럽게**: MSAA 로는 안 됨. `common3d.hlsli` 에서 각 `if` 경계를 `lerp(a, b, smoothstep(edge - w, edge + w, angleDeg))` 로 바꾸고 `w`(전이폭 도) 파라미터 추가. 저장 즉시 핫리로드.

**하지 말 것**: 색·깊이 씬 타깃의 `SampleDesc` 불일치, resolve 전에 RT 언바인드 누락, 백버퍼를 pass 렌더 대상으로 다시 바인드, `m_sampleCount == 1` 인데 resolve 호출.
