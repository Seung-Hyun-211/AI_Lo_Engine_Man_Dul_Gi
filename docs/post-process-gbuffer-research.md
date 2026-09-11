# 포스트프로세싱 인프라 + G-버퍼(노멀/깊이/AO)

**상태: 구현 완료.** 오프스크린 RT + MRT(컬러+뷰공간 노멀) + 풀스크린 패스 인프라 위에 반구 커널
SSAO(블러 포함) + 거리 안개가 얹혀 있다. VS 빌드·실행·그라데이션 확인됨. 남은 건 전부 선택
사항(§9: half-res 최적화, 스크린스페이스 아웃라인, `crease.hlsl` 노멀 기여).

## 1. 개요

"렌더 후 화면의 노멀·깊이·AO 정보를 들고 있으면 다양한 그래픽 연출이 가능하다"는 아이디어에서
출발했다. SSAO·스크린스페이스 아웃라인·거리 안개·소프트 파티클·톤매핑 같은 화면 연출들은 전부
같은 전제를 공유한다 — **오프스크린 RT + G-버퍼(노멀/깊이) + 풀스크린 패스 인프라**. 이 문서가
그 인프라와, 첫 소비자인 SSAO+안개를 함께 다룬다. 이 필요성은 이미 여러 문서에 흩어져
예고돼 있었다: `msaa.md`(포스트 AA용 오프스크린 RT), `lighting.md`/`shadows.md`(셰도우맵이
같은 "오프스크린 depth→SRV" 패턴의 선례), `toon-rendering.md`(스크린스페이스 엣지 검출 대안).

관련: [msaa.md](msaa.md)(씬 타깃 구조), [shadows.md](shadows.md)(오프스크린 depth→SRV 선례),
[lighting.md](lighting.md)/[toon-rendering.md](toon-rendering.md)(조명·셀 셰이딩, 이 위에 얹힘),
[shader-pipeline.md](shader-pipeline.md), [particle-system-research.md](particle-system-research.md)
(소프트 파티클이 깊이 SRV·`Frame.view`를 재사용, 설계만·미구현), [command-playbook.md](command-playbook.md)
3b(새 패스)·3h(조명)·3i(AA).

## 2. 아키텍처

```text
[렌더 스레드 — Dx11Renderer::Render, 매 프레임]

1. RenderShadowMap(snapshot)                              (기존, 불변)

2. 지오메트리 스테이지 (MeshPass3D / ModelMeshPass3D / DebugDrawPass)
   OMSetRenderTargets({ sceneColorRtv, sceneNormalRtv }, sceneDepthDsv)   ← MRT
   각 지오메트리 드로우: PS 가 SV_TARGET0(컬러) + SV_TARGET1(view-space 노멀) 출력

3. PostProcessPass (render/r2d/PostProcessPass) — 풀스크린 삼각형, 한 클래스가 순서대로:
   a. SSAO 계산 → 내부 ao 타깃 (ENGINE_WITH_3D 일 때만; 없으면 1x1 흰색 폴백)
   b. 4x4 박스 블러 → 블러된 ao 타깃
   c. 합성: sceneColor(멀티샘플이면 Texture2DMS 로 직접 읽음) × ao + 거리 안개
      → 백버퍼 RTV 에 직접 씀 (이 단계가 옛 "프레임 끝 컬러 리졸브"를 대체)

4. 오버레이 스테이지 (QuadPass2D / SpritePass2D) — 백버퍼 RTV 에 그대로

5. Present
```

**핵심 설계 결정 세 가지**:

- **MRT로 지오메트리 패스를 확장**(별도 노멀+깊이 프리패스 대신) — 그리기 1회로 끝나고, 기존
  `MeshPass3D`/`ModelMeshPass3D`의 컬러 경로를 거의 안 건드린다(PS가 두 번째 타깃도 쓰는 것만
  변경). 대가는 AO를 라이팅 계산 자체에 통합하지 못하고 최종 컬러에 곱하는 post-multiply로
  그친다는 것(§9 열린 질문).
- **`PostProcessPass`는 `render/r2d/`에 있다** — 컬러 합성 자체는 3D 전용 개념이 없어
  `QuadPass2D`처럼 `ENGINE_WITH_3D` 무관 baseline이지만, SSAO(뷰공간 재구성)는 본질적으로 3D
  개념이라 그 부분만 `#if defined(ENGINE_WITH_3D)`로 감쌌다. 꺼져 있거나 깊이·노멀 SRV가 아직
  없으면 1x1 흰색 텍스처("차폐 없음")로 조용히 폴백 — 합성 셰이더는 어느 쪽인지 몰라도 된다.
- **G-버퍼 텍스처는 `Dx11Renderer`가 소유**(셰도우맵과 같은 선례) — 지오메트리·포스트 두
  스테이지가 공유해야 하므로. `PostProcessPass`는 `PassContext`에 추가된 필드
  (`sceneColorSrv`/`sceneNormalSrv`/`sceneDepthSrv`/`backBufferRenderTarget`/`sceneSampleCount`)
  로 그 SRV들을 받는다 — 셰도우 SRV의 "전역 슬롯 바인딩" 방식 대신 옵션 필드를 골랐다, SSAO를
  쓰는 패스가 하나뿐이라 의도가 더 분명하기 때문.

**파이프라인 배선**: `Dx11Renderer`의 기본 패스 목록은 `{ MeshPass3D, PostProcessPass,
QuadPass2D }`이고, `AddRenderPass`의 기본 삽입 지점이 "마지막 2개(`PostProcessPass`+
`QuadPass2D`) 앞"으로 고정돼 있어 새 지오메트리 패스(`ModelMeshPass3D`, `DebugDrawPass`)는
자동으로 MRT 스테이지에 들어간다. `main.cpp`의 `AddRenderPass` 호출부는 이 때문에 전혀 안
바뀐다. AO 자체는 `PassContext`로 안 나간다 — 계산과 소비가 같은 `PostProcessPass::Execute`
안에서 끝나기 때문.

## 3. G-버퍼 리소스

### 3.1 씬 타깃 (컬러/깊이/노멀)

| 텍스처 | 포맷 | 비고 |
|---|---|---|
| `m_sceneColor` | `R8G8B8A8_UNORM`, `RENDER_TARGET|SHADER_RESOURCE` | 샘플수 무관 **항상 실제 텍스처** — 백버퍼는 `DXGI_USAGE_RENDER_TARGET_OUTPUT`으로만 생성돼 SRV 바인드가 안 되므로, 합성 단계가 읽으려면 씬 컬러가 1x MSAA에서도 백버퍼를 alias할 수 없다(이전엔 alias했었음) |
| `m_sceneDepth` | `R24G8_TYPELESS` → DSV `D24_UNORM_S8_UINT` / SRV `R24_UNORM_X8_TYPELESS` | 셰도우맵(`m_shadowDepth`)과 같은 typeless+두 뷰 패턴 |
| `m_sceneNormal` | `R8G8B8A8_UNORM`, `RENDER_TARGET|SHADER_RESOURCE` | view-space 노멀, `*0.5+0.5` 인코딩. 지오메트리 스테이지의 2번째 렌더타깃 |

셋 다 `m_sampleCount`(최대 8x, `msaa.md`)에 맞춰 멀티샘플일 수 있다.

### 3.2 MSAA — 별도 리졸브 텍스처 없음

컬러는 합성 셰이더(`composite_ms.hlsl`)가 `Texture2DMS`로 직접 읽어 샘플을 평균하며 그 자체가
"커스텀 리졸브"를 겸한다 — 옛 프레임 끝 `ResolveSubresource` 호출이 사라졌다. 깊이·노멀도
같은 논리로, SSAO 셰이더(`ssao_ms.hlsl`)가 `Texture2DMS`로 직접 읽어 **샘플 0**만 쓴다(멀티샘플
앨리어싱을 안고 감 — SSAO·안개 같은 근사 효과엔 충분히 허용되는 수준). 그래서 `depth_resolve.hlsl`
같은 별도 다운샘플 패스나 리졸브 텍스처가 아예 없다. 단일 샘플(`ssao.hlsl`/`composite.hlsl`)과
멀티샘플(`ssao_ms.hlsl`/`composite_ms.hlsl`) 두 변형이 필요한 이유는 HLSL에 `Texture2D`/
`Texture2DMS` 폴리모피즘이 없고 이 엔진에 셰이더 순열 시스템이 아직 없기 때문
([shader-pipeline.md](shader-pipeline.md) "다음").

### 3.3 `Frame` cbuffer 확장 — `view`

뷰공간 노멀을 만들려면 카메라의 `view` 행렬이 셰이더에 있어야 하는데, 기존 `Frame` cbuffer엔
합성된 `viewProj`만 있었다. `FrameConstantsGpu`(C++)와 `common3d.hlsli`의 `cbuffer Frame`에
`view`(4x4, row-major, `camera.view` 그대로) 필드를 추가했다 — **불변 규칙: 이 둘은 항상 같이
고친다.**

이 필드 하나가 두 가지를 동시에 커버한다: 뷰공간 노멀 변환(`common3d.hlsli::WorldToViewNormal`,
아래)과 [particle-system-research.md](particle-system-research.md)가 필요로 하는 카메라
world-space right/up(뷰 행렬이 정규직교라 `view`의 0행/1행이 곧 그 축) — 파티클 시스템을
실제로 구현할 때 `Frame` cbuffer를 또 확장할 필요가 없다.

```hlsl
// common3d.hlsli
float3 WorldToViewNormal(float3 worldNormal)
{
    return normalize(mul(float4(worldNormal, 0.0f), view).xyz);
}
```

### 3.4 `GeometryPSOut` — 지오메트리 셰이더의 두 번째 출력

```hlsl
// common3d.hlsli — 5개 지오메트리 셰이더가 전부 이 타입을 반환 (개별 정의 아님)
struct GeometryPSOut { float4 color : SV_TARGET0; float4 normal : SV_TARGET1; };
```

`mesh.hlsl`/`model.hlsl`/`cel.hlsl`/`mesh_instanced.hlsl`/`mesh_instanced_toon.hlsl`의 `PSMain`이
`float4 : SV_TARGET` 대신 이 구조체를 반환하도록 바뀌었다 — 셰이딩 로직 자체(셀 밴드 각도 등)는
그대로, 반환 방식만 바뀌었다:

```hlsl
GeometryPSOut PSMain(VSOut input)
{
    float shadow = SampleShadow(input.shadowClip);
    GeometryPSOut output;
    output.color  = float4(ApplyLighting(objColor.rgb, input.nrm, shadow), objColor.a);
    output.normal = float4(WorldToViewNormal(input.nrm) * 0.5f + 0.5f, 1.0f);
    return output;
}
```

`outline.hlsl`/`shadow.hlsl`/`shadow_instanced.hlsl`/`debugline.hlsl`은 안 바뀌었다 — D3D11은
바인드된 MRT 중 일부 슬롯만 쓰는 PS를 허용하므로(그 픽셀의 노멀은 클리어값 유지), 실루엣 링·
디버그 라인처럼 의미 있는 노멀이 없는 지오메트리는 `SV_TARGET1`을 안 써도 된다.
`crease.hlsl`(내부 크리즈 리본)만 아직 판단 보류 상태다(§9).

## 4. SSAO (`render/r2d/PostProcessPass::ComputeAo`)

16-샘플 반구 커널(가장 흔한 baseline). 커널 샘플과 4x4 노이즈 텍스처(픽셀마다 커널을 살짝
회전시켜 밴딩을 흐림으로 상쇄)는 `Initialize`에서 결정적 시드로 한 번만 생성한다.

**뷰공간 위치 재구성**: 일반적인 SSAO 튜토리얼은 역투영행렬(`invProj`)을 통째로 넘기지만, HLSL엔
내장 행렬 역함수가 없다. 이 엔진의 `math::PerspectiveFovLH`가 굽는 투영행렬은 특정 모양이라
`proj.m[10]`(A)·`proj.m[14]`(B) 두 스칼라만으로 `viewZ = B / (depth - A)`가 풀리고, 거기서
`x`/`y`는 화면 UV·`proj.m[0]`/`proj.m[5]`로 바로 나온다 — 전체 4x4 역행렬이 필요 없다.

**블러가 필수인 이유**: 픽셀마다 커널을 회전시키는 노이즈 텍스처 기법은 밴딩을 없애는 대신
고주파 노이즈를 만든다. `ssao_blur.hlsl`이 노이즈 타일과 같은 반경(4x4)의 박스 블러로 그 노이즈를
지운다 — 반구 커널 SSAO는 원래 "노이즈+블러"가 한 세트다(옵션 아님). 블러 없이 실측했을 때
AO가 자연스러운 그라데이션이 아니라 "점 밀도" 스티플로 보이는 게 그 증거였다.

**셀 셰이딩과의 궁합**: SSAO는 원래 사실적 렌더링 기법이라 회색조 그라데이션을 만드는데, 이
엔진의 4밴드 셀 셰이딩(`toon-rendering.md`)에 그대로 곱하면 스타일이 깨질 수 있다 —
`aoPower`(>1이면 AO가 0/1 극단값으로 몰림)로 절제한다(§6 `Scene3D::PostProcessSettings`).

AO는 씬 MSAA와 무관하게 항상 단일 샘플·전체 해상도로 계산된다(half-res 최적화는 미구현, §9).
`IRenderPass`에 리사이즈 훅이 없어서, AO 타깃(원본 + 블러본 둘 다)은 `Execute` 안에서
`context.viewportWidth/Height` 변화를 감지해 지연 재생성한다(`EnsureAoTarget`).

## 5. 합성 (`render/r2d/PostProcessPass::Composite`)

```hlsl
// composite.hlsl (단일 샘플 — composite_ms.hlsl 은 sceneColor 를 Texture2DMS 평균으로 읽는 것만 다름)
float4 PSMain(VSOut input) : SV_TARGET
{
    float3 color = sceneColor.Load(int3(input.pos.xy, 0)).rgb;
    color *= lerp(1.0f, aoTex.Sample(aoSampler, input.uv).r, aoParams.x);   // aoParams.x = aoStrength

    if (fogParams.z > 0.5f)   // fogEnabled
    {
        float depth = depthTex.Load(int3(input.pos.xy, 0));
        if (depth < 1.0f)   // 배경/far plane 은 스킵
        {
            float fog = saturate((ViewZFromDepth(depth) - fogParams.x) / (fogParams.y - fogParams.x));
            color = lerp(color, fogColor.rgb, fog);
        }
    }
    return float4(color, 1.0f);
}
```

거리 안개는 깊이만 있으면 되고(노멀 불필요), SSAO와 같은 A/B 스칼라 트릭으로 뷰공간 Z를
재구성한다. `SnapshotBuilder::BuildPostProcess()`가 안개를 카메라 원거리 클립(100)·크라우드
최대거리 컬(`instanced-rendering.md`, 100)에 맞춰 `fogNear=40`/`fogFar=100`으로 채운다 —
개체가 뿅 사라지는 대신 안개에 묻히는 것처럼 보이게 하는 게 의도.

## 6. 파일 목록

| 파일 | 내용 |
|---|---|
| `src/render/Dx11Renderer.h`/`.cpp` | 씬 컬러/깊이/노멀 텍스처+SRV 소유, 지오메트리 스테이지 MRT 바인드, `PostProcessPass`를 트레일링 패스로 상시 등록 |
| `src/render/RenderPass.h` | `PassContext`에 `backBufferRenderTarget`/`sceneColorSrv`/`sceneNormalSrv`/`sceneDepthSrv`/`sceneSampleCount` |
| `src/render/r3d/FrameConstants.h` + `assets/shaders/common3d.hlsli` | `Frame` cbuffer에 `view`, `WorldToViewNormal` 헬퍼, `GeometryPSOut` |
| `assets/shaders/{mesh,model,cel,mesh_instanced,mesh_instanced_toon}.hlsl` | `GeometryPSOut` 반환 — 컬러 + view-space 노멀 |
| `assets/shaders/{outline,shadow,shadow_instanced,debugline}.hlsl` | 변경 없음(§3.4) |
| `src/render/r2d/PostProcessPass.h`/`.cpp` | SSAO 계산 + 블러 + 합성(AO/안개), SSAO 관련 부분만 `ENGINE_WITH_3D`로 감쌈 |
| `assets/shaders/fullscreen.hlsli` | 정점/인덱스 버퍼 없는 풀스크린 삼각형 공용 VS |
| `assets/shaders/ssao.hlsl` / `ssao_ms.hlsl` | 반구 커널 SSAO(단일/멀티샘플) |
| `assets/shaders/ssao_blur.hlsl` | 4x4 박스 블러 |
| `assets/shaders/composite.hlsl` / `composite_ms.hlsl` | AO 곱 + 안개 (+멀티샘플이면 컬러 리졸브) |
| `src/render/r3d/Scene3D.h` | `PostProcessSettings`(`aoStrength`/`aoRadius`/`aoPower`/`fogEnabled`/`fogNear`/`fogFar`/`fogColor`) |
| `src/game/SnapshotBuilder.cpp` | `BuildPostProcess()`가 값을 채움 |
| `src/main.cpp` | 변경 없음 |
| `CppWindowGame.vcxproj` | 위 신규 파일 전부 등록 (글롭 빌드 아님 — 빠뜨리면 MSBuild가 컴파일 자체를 안 함) |

**미구현(전부 선택, §9)**: `assets/shaders/crease.hlsl`의 노멀 기여, 스크린스페이스 아웃라인,
half-res AO.

## 7. 스레드 / 불변 규칙 매핑

| 규칙 | 이 설계에서 |
|---|---|
| 1 (D3D11 은 렌더 스레드만) | 모든 G-버퍼 텍스처·RTV·SRV 생성과 리졸브는 `Dx11Renderer` 안(렌더 스레드). `PostProcessPass`의 `Initialize`/`Execute`도 마찬가지 |
| 2 (렌더러 코어는 clear/bind/pass 순회 + `ShaderLibrary` 소유만) | G-버퍼 텍스처는 셰도우맵과 동급 — 렌더러 코어가 소유(지오메트리·포스트 두 스테이지가 공유), 그리기는 `IRenderPass` 하나(`PostProcessPass`)가. 코어는 스테이지 경계의 `OMSetRenderTargets` 호출 + `PassContext` 조립만 |
| 3 (경계는 값 스냅샷만) | G-버퍼는 렌더 스레드 전용 GPU 리소스 — `RenderSnapshot`에 안 실림. 튜닝값(`aoStrength` 등)만 `Scene3D::postProcess`로 스냅샷을 건넌다 |
| 6 (`ParallelFor` 무관) | 전부 렌더 스레드 그리기 — 메인 스레드 시뮬레이션과 무관, 규칙 6 해당 없음 |
| 7 (2D/3D 분리) | `PostProcessPass`는 `render/r2d/`(baseline)에 있지만 SSAO 관련 코드만 `ENGINE_WITH_3D`로 감쌌다(§2). 2D 오버레이 패스는 이 인프라를 안 쓰고 최종 컬러 위에 그대로 그림 |

## 8. 사용 방법 (How to use)

### 8.1 AO/안개 세기 조정

`game/SnapshotBuilder.cpp`의 `BuildPostProcess()`에서 `render::PostProcessSettings`
(`render/r3d/Scene3D.h`) 필드를 바꾼다:

```cpp
render::PostProcessSettings BuildPostProcess()
{
    render::PostProcessSettings settings{};
    settings.aoStrength = 0.5f;   // 0 = AO 없음, 1 = 완전 반영 (기본 1.0)
    settings.aoRadius = 0.3f;     // 반구 커널 반경, 뷰공간 단위(m)
    settings.aoPower = 2.0f;      // 클수록 AO가 극단값(0/1)으로 몰림 — 셀 룩에 유리
    settings.fogEnabled = true;
    settings.fogNear = 40.0f; settings.fogFar = 100.0f;
    settings.fogColor = { 0.44f, 0.49f, 0.57f, 1.0f };
    return settings;
}
```

게임 상태(시간대, 실내/실외 등)에 따라 다르게 채우고 싶으면 이 함수에 조건 분기만 추가하면
된다. `aoBias`만은 `PostProcessPass.cpp`의 상수로 남아 있다(조정 필요성이 낮다고 판단).

### 8.2 새 풀스크린 이펙트 추가 (예: 비네트)

```cpp
// 1. assets/shaders/vignette.hlsl — fullscreen.hlsli 재사용, PS 만 새로
// 2. render/r2d/VignettePass : IRenderPass 구현, Initialize 에서 shaders.Get(...)
// 3. main.cpp: renderer.AddRenderPass(std::make_unique<VignettePass>())  ← 기본 삽입 지점 = 지오메트리 구간
//    합성 다음에 그리고 싶으면 PostProcessPass 내부에 단계를 추가하거나(OCP 위반 소지),
//    별도 패스를 atEnd=false 로 추가해 QuadPass2D 앞에 두는 대안을 검토
```

기존 `PostProcessPass` 내부 로직은 안 건드림(OCP) — 체인에 패스 하나 더 끼우는 것뿐.

### 8.3 소프트 파티클 연결

`ParticlePass3D::Initialize`(설계만, [particle-system-research.md](particle-system-research.md))가
`PSSetShaderResources`로 `context.sceneDepthSrv`를 바인드하면 된다 — 렌더러가 `PassContext`로
넘겨주는 값을 그대로 받아 쓰면 된다. 파티클 쪽 코드 변경은 PS 한 줄(`saturate(...)` 페이드)
추가로 끝난다.

### 8.4 하지 말 것

```text
✗ G-버퍼 텍스처를 RenderSnapshot 에 담기 — 렌더 스레드 전용 GPU 리소스다(규칙 3 위반)
✗ 깊이를 SRV 로 바인드된 채로 같은 프레임에 DSV 로도 쓰기 — 셰도우맵처럼 매번 detach 후 사용
✗ 멀티샘플 텍스처를 일반 Texture2D::Sample 로 읽기 — Texture2DMS::Load(px, sampleIndex)
✗ SSAO 강도를 실사 렌더링 값 그대로 — aoPower/aoStrength 로 절제(§4)
✗ Frame cbuffer 를 PostProcessPass 전용으로 새로 정의 — FrameConstantsGpu 공유(불변 규칙)
✗ 반구 커널 SSAO 를 블러 없이 — 노이즈가 그대로 스티플로 보인다(§4)
✗ 풀스크린 패스마다 자기 정점 버퍼 새로 만들기 — fullscreen.hlsli 공용 VS 하나로 충분
```

## 9. 남은 일 / 열린 질문

- **half-res AO**: 지금은 전체 해상도로 계산한다. 화면의 절반 크기로 낮추면 비용 1/4인데,
  bilinear 업샘플이 얇은 물체(캐릭터 팔다리) 경계에서 번질 수 있어 필요하면 depth-aware
  업샘플(bilateral)까지 같이 넣어야 한다. 성능 문제가 실측되면 착수.
- **`crease.hlsl`(내부 크리즈 리본)이 노멀에 기여해야 하는지**: 안 넣으면 그 자리는 밑면 노멀로
  남아 SSAO/엣지검출이 살짝 부정확 — 실측 후 판단.
- **스크린스페이스 아웃라인**: 노멀/깊이 엣지 검출로 인버티드 헐(`toon-rendering.md`)을 보완/대체.
  헐 방식은 실루엣만·두께 일정, 스크린스페이스는 내부 디테일 엣지도 잡지만(크리즈 라인과 목적
  겹침) 드로우 콜 추가 없이 풀스크린 패스 하나로 끝난다. 1차 권장은 헐 유지 + 스크린스페이스를
  크리즈 라인의 대체재 후보로 검토 — 완전 교체는 시각 비교 후.
- **AO를 라이팅에 통합 vs 화면 전체 post-multiply**: 지금은 post-multiply(최종 컬러에 곱)뿐이라
  "물리적으로 정확한" 통합은 아니다(라이팅 계산 자체엔 AO가 안 들어감). 셀 셰이딩 게임에서는
  이 정도 근사로 충분한 경우가 많다 — 라이팅에 직접 섞고 싶어지면 MRT 대신 별도 노멀+깊이
  프리패스(지오메트리를 두 번 그림)로 재설계해야 한다.
- **컴퓨트 셰이더로 SSAO 이전 시점**: 지금은 픽셀 셰이더 풀스크린 패스로 충분(`command-playbook.md`
  3f 컴퓨트 지원 자체가 아직 없음). 성능 병목이 실측되면 그때 재검토.
- **디버그 뷰(노멀/깊이/AO를 화면에 그대로 표시)**: 설계 단계에서 유용해 보였지만 실제로는
  안 만들었다 — 필요해지면 `Scene3D::PostProcessSettings`에 열거형 필드를 추가하고
  `composite*.hlsl`이 그 값이면 최종 컬러 대신 해당 버퍼를 그대로 출력하면 된다.

## 10. 관련 문서

- [msaa.md](msaa.md) — 씬 타깃 구조, 이 문서가 확장한 지점
- [shadows.md](shadows.md) — 오프스크린 depth→SRV의 기존 선례, `PassContext` 확장이 참고한 패턴
- [lighting.md](lighting.md) / [toon-rendering.md](toon-rendering.md) — 이 인프라가 얹히는 조명·셀 셰이딩, 스크린스페이스 아웃라인이 비교 대상으로 삼는 인버티드 헐·크리즈 라인
- [shader-pipeline.md](shader-pipeline.md) — `.hlsl` 로딩 규약
- [particle-system-research.md](particle-system-research.md) — 소프트 파티클이 이 문서의 깊이 SRV·`Frame.view`를 재사용(설계만)
- [command-playbook.md](command-playbook.md) — 3b(새 패스), 3h(조명), 3i(AA/포스트), 3f(컴퓨트, 향후)
