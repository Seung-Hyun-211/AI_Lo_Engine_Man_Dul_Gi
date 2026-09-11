# 포스트프로세싱 인프라 + G-버퍼(노멀/깊이/AO) 연구

**상태: 연구 + 설계만. 미구현.** 코드 변경 없음. "렌더 후 화면의 노멀·깊이·AO 정보를 들고 있으면
다양한 그래픽 연출이 가능하다"는 아이디어를 이 엔진 구조에 맞게 조사하고 설계한다 — SSAO,
스크린스페이스 아웃라인, 거리 안개, 소프트 파티클, 톤매핑 같은 화면 연출들이 공통으로 요구하는
**오프스크린 RT 인프라 + G-버퍼(노멀/깊이) 확보**가 핵심이다.

이 필요성은 이미 여러 문서에 흩어져 예고돼 있었다: `msaa.md` "포스트 AA 패스 — 오프스크린 RT
인프라와 함께", `lighting.md` "그림자: 오프스크린 RT 인프라 선행"(그림자는 이제 됐지만 셰도우맵
자체가 이 패턴의 선례), `toon-rendering.md` "대안(선택 안 함): 포스트프로세스 엣지 검출(오프스크린
RT 필요)", `command-playbook.md` 3i "포스트 FXAA(오프스크린 RT 선행)", 그리고 방금 쓴
[particle-system-research.md](particle-system-research.md) §11 "소프트 파티클 … 깊이 SRV 필요".
이 문서가 그 공통 전제를 한 번에 설계한다.

관련: [msaa.md](msaa.md)(씬 타깃), [shadows.md](shadows.md)(오프스크린 depth→SRV 선례),
[lighting.md](lighting.md)/[toon-rendering.md](toon-rendering.md)(조명·셀 셰이딩, 이 위에 얹힘),
[shader-pipeline.md](shader-pipeline.md), [particle-system-research.md](particle-system-research.md)
(소프트 파티클이 이 인프라의 첫 소비자 후보), [command-playbook.md](command-playbook.md)
3b(새 패스)·3h(조명)·3i(AA).

---

## 1. 왜 지금은 안 되나 — 현재 구조의 제약

| 필요한 것 | 현재 상태 |
|---|---|
| 포스트 패스가 이전 단계 결과를 텍스처로 **읽기** | `IRenderPass::Execute`는 `PassContext`로 RTV 1개 + DSV 1개만 받는다(`RenderPass.h`). 이전 패스 출력을 셰이더 입력(SRV)으로 되읽는 경로 자체가 없다 |
| 깊이값을 셰이더에서 샘플 | `m_sceneDepth`가 `D3D11_BIND_DEPTH_STENCIL`**만**으로 생성됨(`Dx11Renderer::CreateSceneTargets`) — `BIND_SHADER_RESOURCE`가 없어 지금 포맷으론 못 읽는다. (셰도우맵 `m_shadowDepth`는 둘 다 있음 — 선례는 이미 있다, §4.1) |
| 화면 공간 노멀 | 존재 자체가 없음. `MeshDraw`/`ModelDraw`는 컬러 하나만 그린다(`mesh.hlsl`/`cel.hlsl`의 `PSMain`이 `SV_TARGET` 1개만 반환) |
| MSAA 타깃을 셰이더가 읽기 | 씬 컬러·깊이가 멀티샘플(`m_sampleCount`, 최대 8x, `msaa.md`)이면 일반 `Texture2D::Sample`이 아니라 `Texture2DMS::Load(px, sampleIndex)`가 필요 — 지금 그런 셰이더가 없다 |
| 풀스크린 이펙트를 그리는 관용구 | 없음. `QuadPass2D`/`SpritePass2D`는 사각형 지오메트리를 쓰지 정점 없는 풀스크린 트라이앵글이 아니다 |

**핵심 결론**: 이 문서가 실제로 설계하는 건 "AO 하나"가 아니라 **오프스크린 RT + MRT(다중
렌더타깃) + 풀스크린 패스**라는 재사용 가능한 인프라다. AO·아웃라인·안개는 전부 그 위에 올라가는
개별 이펙트일 뿐이다(OCP — 인프라 한 번 만들면 이펙트는 패스 추가로 확장).

---

## 2. 업계 리서치 — G-버퍼로 뭘 하나

| 기법 | 필요한 버퍼 | 이 엔진에 맞는 정도 |
|---|---|---|
| **SSAO**(Screen-Space Ambient Occlusion) | 깊이 + 노멀(view space) | ○ — 요청한 "AO" 그 자체. §5 |
| **스크린스페이스 아웃라인**(노멀/깊이 엣지 검출) | 노멀 + 깊이 | ○ — `toon-rendering.md`가 이미 대안으로 언급. 인버티드 헐과 병행/대체 가능. §7.3 |
| **깊이 기반 안개** | 깊이만 | ○ — 가장 싸고 효과가 큼. 노멀 불필요 |
| **소프트 파티클**(빌보드가 지오메트리와 만나는 경계 완화) | 깊이만 | ○ — [particle-system-research.md](particle-system-research.md)가 이미 요청한 것 |
| **피사계심도(DOF)** | 깊이만 | △ — 효과는 좋지만 블러 커널 비용, 우선순위 낮음 |
| **톤매핑/컬러그레이딩** | 컬러만(G-버퍼 불필요) | ○ — 인프라(풀스크린 패스)만 있으면 공짜로 딸려옴, `lighting.md` "더 깊은 방법" 항목과 합류 |
| **스크린스페이스 반사(SSR)** | 깊이 + 노멀 + 컬러(이전 프레임) | ✗ — 구현 난도·비용 큼, 이 엔진 규모에 과함. 범위 밖 |
| **모션 블러** | 모션 벡터(별도 버퍼, 이전 프레임 트랜스폼 필요) | ✗ — 이 문서 범위 밖(다른 버퍼 종류) |

**AO 두 갈래**: "실시간 SSAO"(깊이+노멀에서 매 프레임 근사 계산, 동적 씬에 맞음)와 "베이크드
AO"(모델 임포트 시 정적으로 구워 텍스처/버텍스컬러에 저장, `crease.hlsl`의 크리즈 리본이 하는
"AO 톤 흉내"와 결이 비슷함)는 완전히 다른 파이프라인이다. **이 문서는 실시간 SSAO를 다룬다** —
"화면 정보로 연출"이라는 요청과 맞고, 크라우드처럼 동적으로 움직이는 개체에도 적용되기 때문.
베이크드 AO는 `model-animation-research.md`/임포트 파이프라인 쪽 별도 주제.

**셀 셰이딩과의 궁합(중요, 판단 필요)**: SSAO는 원래 사실적 렌더링 기법이라 회색조 그라데이션
음영을 만든다. 이 엔진의 룩은 4밴드 셀 셰이딩(`toon-rendering.md`)이라 SSAO를 그대로 곱하면
"사실적 그림자 위에 셀 셰이딩"이 되어 스타일이 깨질 수 있다 — 세기를 약하게 쓰거나, AO를
연속값이 아니라 셀처럼 계단화(quantize)해서 합성하는 절충이 필요(§8 열린 질문).

---

## 3. 아키텍처 개요

```text
[렌더 스레드 — Dx11Renderer::Render, 매 프레임]

1. RenderShadowMap(snapshot)                 (기존, 불변)

2. 씬 패스 (MeshPass3D / ModelMeshPass3D)
   OMSetRenderTargets({ sceneColorRtv, sceneNormalRtv }, sceneDepthDsv)   ← MRT, §4.2
   각 지오메트리 드로우: PS 가 SV_TARGET0(컬러) + SV_TARGET1(view-space 노멀) 둘 다 출력

3. (멀티샘플이면) 깊이·노멀 리졸브/다운샘플 → 논-MS 텍스처   §4.3
   sceneDepthDsv, sceneNormalRtv 는 그대로 두고 별도 리졸브 산출물 생성
   (컬러는 기존과 동일하게 프레임 끝에 리졸브 — 안 건드림)

4. SsaoPass (풀스크린 삼각형)                 §5
   입력: 리졸브된 깊이 + 노멀 (t0, t1)
   출력: aoRtv (R8_UNORM, 옵션 half-res)

5. CompositePass (풀스크린 삼각형)            §6
   sceneColor × ao (톤 조정) → 최종 컬러 (기존 sceneColorRtv 를 덮어쓰거나 새 타깃)
   (디버그 모드: normal/depth/ao 원본을 그대로 화면에 표시 — §7.4)

6. MSAA resolve(컬러, 기존 로직 그대로) → 백버퍼 → Present
```

- 새 스테이지(3~5)는 전부 **새 `IRenderPass` 구현**으로 추가(OCP) — `MeshPass3D`/
  `ModelMeshPass3D`의 기존 컬러 경로는 최대한 안 건드리고, "PS가 두 번째 타깃도 쓴다"는 것만
  최소 변경.
- **오프스크린 RT들은 `Dx11Renderer`가 소유**(셰도우맵과 같은 선례) — 여러 패스(지오메트리 패스,
  SSAO 패스, 컴포짓 패스)가 공유해야 하므로 개별 패스 소유가 아니다. `PassContext`를 확장해
  `normalRenderTarget`(옵션, nullptr 가능) 필드를 추가하고, G-버퍼 SRV들은 새
  `GBufferContext`(셰도우의 `ShadowContext`와 같은 패턴)로 SSAO/컴포짓 패스에 넘긴다.

---

## 4. G-버퍼 리소스 설계

### 4.1 깊이를 읽을 수 있게 — 셰도우맵과 같은 포맷 전환

```cpp
// 지금 (Dx11Renderer::CreateSceneTargets)
depthDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
depthDesc.BindFlags = D3D11_BIND_DEPTH_STENCIL;                       // SRV 불가

// 제안 — m_shadowDepth 와 동일 패턴 (이미 검증된 조합)
depthDesc.Format = DXGI_FORMAT_R24G8_TYPELESS;
depthDesc.BindFlags = D3D11_BIND_DEPTH_STENCIL | D3D11_BIND_SHADER_RESOURCE;
// DSV: DXGI_FORMAT_D24_UNORM_S8_UINT (그대로 depth-stencil 로 씀)
// SRV: DXGI_FORMAT_R24_UNORM_X8_TYPELESS (깊이만 읽기, 스텐실 채널 무시)
```

깊이가 멀티샘플이면 SRV 도 `Texture2DMS`가 된다 — §4.3.

### 4.2 노멀 버퍼 — 새 MRT 타깃

```cpp
// 새 텍스처 (씬 컬러와 같은 크기/샘플수)
D3D11_TEXTURE2D_DESC normalDesc = colorDesc;         // width/height/sampleCount 동일
normalDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;      // view-space 노멀, *0.5+0.5 인코딩
normalDesc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
// m_sceneNormal / m_sceneNormalRtv / (리졸브 후) m_sceneNormalSrv
```

- **뷰 공간 노멀**(월드 공간이 아니라) — SSAO 계산이 카메라 기준 반구 샘플링을 쓰므로 view space
  가 자연스럽다(`common3d.hlsli`에 월드→뷰 변환 한 줄 추가, `Frame` cbuffer 의 `view` 자체는
  지금 없고 `viewProj`만 있음 — **§4.2-a 참고**).
- **포맷**: `RGBA8_UNORM`(단순, `n*0.5+0.5`)로 시작. 옥타헤드럴 압축(RG8/RG16) 은 대역폭
  최적화 — 지금 규모에선 과최적화, 나중에.
- **§4.2-a**: `Frame` cbuffer에 `view` 행렬(또는 최소 view의 3x3 회전부)이 없다 — 지금
  `viewProj`만 저장(`FrameConstants.h`). 뷰 공간 노멀을 만들려면 `view`(또는 `normalMatrix`)를
  cbuffer에 추가해야 한다 — **CLAUDE.md 불변 규칙**: `FrameConstantsGpu`(C++)와
  `common3d.hlsli`의 `cbuffer Frame`은 항상 같이 고친다. (참고: 이전에 쓴
  [particle-system-research.md](particle-system-research.md) §3도 빌보드용 camRight/camUp을
  위해 같은 cbuffer 확장이 필요하다고 지적했다 — 두 문서가 같은 지점을 건드리므로, 실제 구현
  순서를 정할 때 둘을 합쳐 한 번에 `Frame` cbuffer를 확장하는 게 낫다, §9.)

### 4.3 MSAA와의 상호작용 — 가장 까다로운 지점

씬 컬러/노멀/깊이가 전부 멀티샘플(`msaa.md`, 최대 8x)이면:

- **컬러**는 이미 프레임 끝에 `ResolveSubresource`로 하드웨어 리졸브(단순 평균) — 안 건드림.
- **노멀은 하드웨어 리졸브 가능**(`ResolveSubresource`가 `RGBA8_UNORM`도 지원) — 다만 실루엣
  가장자리에서 픽셀 하나에 서로 다른 두 표면의 노멀이 평균돼 이상한 방향이 나올 수 있다. SSAO는
  근사 효과라 이 정도 오차는 대체로 허용된다.
- **깊이는 하드웨어 리졸브가 없다**(depth 포맷은 `ResolveSubresource` 미지원 — 평균 깊이가
  의미 없기도 함). 방법:
  - (a) **가장 단순**: 풀스크린 패스에서 `Texture2DMS<float>::Load(px, 0)`로 **샘플 0만** 사용
    (멀티샘플 앨리어싱 그대로 안고 감 — SSAO/안개 등 근사 효과엔 충분히 허용).
  - (b) 더 정확: 별도 다운샘플 셰이더로 8개 샘플의 min/max/average 중 하나를 선택해 논-MS
    텍스처에 씀(1 드로우 추가).
  - **권장: (a)로 시작.** 화질 이슈가 실측되면 (b)로.
- **실행 순서 주의**: 컬러의 하드웨어 리졸브는 지금 `Render()` 맨 끝, `Present` 직전이다
  (`msaa.md`). 노멀/깊이는 **SSAO 패스가 쓰기 전**, 즉 지오메트리 패스 직후·컴포짓 패스 이전에
  리졸브해야 한다 — 컬러 리졸브 시점과 다르다. `Dx11Renderer::Render`에 두 번째 리졸브 지점이
  생긴다는 뜻(기존 흐름에 삽입, §3의 3번).

---

## 5. SSAO 계산 (`render/r3d/SsaoPass`)

### 5.1 알고리즘 — 반구 커널(Crysis류, 가장 흔한 baseline)

```hlsl
// ssao.hlsl (풀스크린 삼각형, PSMain)
Texture2D depthTex : register(t0);      // 리졸브된 깊이, R24_UNORM_X8_TYPELESS 뷰
Texture2D normalTex : register(t1);     // 리졸브된 뷰공간 노멀
Texture2D noiseTex : register(t2);      // 4x4 랜덤 회전 벡터 (타일링)
cbuffer SsaoParams : register(b0) { float4x4 invProj; float radius, power, bias, _pad; float2 screenSize; };

float4 PSMain(VSOut i) : SV_TARGET
{
    float depth = depthTex.Sample(pointSamp, i.uv).r;
    if (depth >= 1.0) return float4(1,1,1,1);           // 배경(스카이) — AO 없음
    float3 viewPos = ReconstructViewPos(i.uv, depth, invProj);   // 깊이 → 뷰공간 위치
    float3 n = normalTex.Sample(pointSamp, i.uv).xyz * 2 - 1;

    float3 randomVec = noiseTex.Sample(wrapSamp, i.uv * screenSize / 4.0).xyz;   // 타일 4x4
    float3 tangent = normalize(randomVec - n * dot(randomVec, n));
    float3x3 tbn = float3x3(tangent, cross(n, tangent), n);

    float occlusion = 0;
    for (int k = 0; k < kKernelSize; ++k)               // kKernelSize ≈ 16, C++ 에서 생성해 cbuffer/텍스처로
    {
        float3 samplePos = viewPos + mul(kKernel[k], tbn) * radius;
        float4 clip = mul(float4(samplePos, 1), proj);   // 뷰→클립 (proj 만, view 는 이미 반영됨)
        float2 sampleUv = clip.xy / clip.w * float2(0.5,-0.5) + 0.5;
        float sampleDepth = ReconstructViewZ(depthTex.Sample(pointSamp, sampleUv).r, invProj);
        float rangeCheck = smoothstep(0, 1, radius / abs(viewPos.z - sampleDepth));
        occlusion += (sampleDepth >= samplePos.z + bias ? 1.0 : 0.0) * rangeCheck;
    }
    float ao = 1.0 - (occlusion / kKernelSize);
    return float4(pow(ao, power).xxx, 1);
}
```

- 커널 샘플(반구 내 랜덤 벡터, 원점에 가까울수록 밀도 높게)은 **C++에서 로드 시 1회 생성**해
  작은 `float4[16]` 배열로 cbuffer에 올린다(런타임 랜덤 불필요, 결정적).
- **노이즈 텍스처**(4x4 랜덤 회전) 로 커널을 픽셀마다 살짝 돌려 밴딩 아티팩트를 흐림으로 상쇄 —
  4x4 고정 패턴, 절차적 생성(로드 시 CPU에서 채워 `IMMUTABLE` 텍스처).
- **half-resolution 옵션**: `aoRtv`를 화면의 절반 크기로 만들면 비용 1/4 — 컴포짓 시
  bilinear 업샘플. 이 엔진 규모(WARP 개발 환경)에선 처음부터 half-res 권장.

### 5.2 사실적 SSAO를 셀 룩에 맞추기

- **세기 약하게(`power` 파라미터)**: 셀 셰이딩은 이미 강한 명암 대비를 갖고 있어 SSAO를 강하게
  곱하면 지저분해 보인다. `power > 1`로 AO를 극단값(0 또는 1에 가깝게) 몰아서 "완전히 안
  가려짐/구석진 곳만 살짝 어둡게"로 절제.
- **계단화(옵션)**: `ao`를 `round(ao * 3) / 3`처럼 3~4단으로 양자화하면 셀 셰이딩의 밴드 느낌과
  더 어울린다 — `cel.hlsl`의 4밴드와 통일감.
- **적용 범위 제한**: 캐릭터(스킨드 모델)엔 세게, 배경 큐브/평면엔 약하게 — 컴포짓 단계에서
  `Object` cbuffer(b1)에 `aoStrength` 필드 하나 추가하는 절충안도 가능(§8).

---

## 6. 합성 (`render/r3d/PostCompositePass`)

```hlsl
// composite.hlsl
Texture2D sceneColor : register(t0);
Texture2D aoTex : register(t1);          // half-res 면 bilinear 업샘플이 공짜로 됨(샘플러)
float4 PSMain(VSOut i) : SV_TARGET
{
    float3 color = sceneColor.Sample(pointSamp, i.uv).rgb;
    float ao = aoTex.Sample(linearSamp, i.uv).r;
    return float4(color * lerp(1.0, ao, kAoStrength), 1);
}
```

- **이 패스가 톤매핑/컬러그레이딩의 자연스러운 확장 지점**이기도 하다(`lighting.md` "더 깊은
  방법" 항목) — AO 곱 다음에 `ACESFilm()` 같은 톤매핑 함수 한 줄 추가하는 식으로 나중에 얹는다.
  **이 문서 범위 밖**이지만 인프라가 같으므로 언급.
- 디버그 뷰 모드(§7.4)도 이 패스나 그 옆에 별도 스위치로 구현.

---

## 7. 부가 활용 (인프라가 생기면 "공짜"에 가까운 것들)

### 7.1 거리 안개

```hlsl
float fog = saturate((viewZ - fogNear) / (fogFar - fogNear));
color = lerp(color, fogColor, fog);
```

깊이만 있으면 됨(노멀 불필요) — `CompositePass`에 한 줄 추가, 파라미터 2개(near/far) + 색.
`RenderSnapshot::clearColor`와 안개색을 맞추면 원거리 오브젝트가 하늘에 자연스럽게 묻힌다.

### 7.2 소프트 파티클

[particle-system-research.md](particle-system-research.md) §11이 열어둔 질문 — 이 인프라가
생기면 `particle.hlsl` PS 가 `depthTex`(§4.1의 SRV)를 읽어 `saturate((sceneDepth - particleDepth)
/ fadeDistance)`로 알파를 줄인다. **파티클 패스가 이 문서의 깊이 SRV의 두 번째 소비자.**

### 7.3 스크린스페이스 아웃라인 (인버티드 헐의 대안/보완)

```hlsl
// 이웃 픽셀과 노멀/깊이 차이가 크면 엣지
float3 n0 = normalTex.Sample(s, uv).xyz;
float d0 = depthTex.Sample(s, uv).r;
float edge = 0;
for each neighbor (4방향 또는 8방향):
    edge = max(edge, dot(n0, nNeighbor) < kNormalThreshold ? 1 : 0);
    edge = max(edge, abs(d0 - dNeighbor) > kDepthThreshold ? 1 : 0);
color = lerp(color, outlineColor, edge);
```

- **인버티드 헐과의 차이**: 헐 방식(`toon-rendering.md`)은 **오브젝트 실루엣**만 잡고 두께가
  일정하며 모델별로 계산(정점 셰이더 비용, 서브메시마다 추가 드로우). 스크린스페이스는
  **내부 디테일 엣지도 잡고**(노멀이 크게 꺾이는 곳 — 크리즈 라인이 CPU로 하는 일과 목적이
  겹친다) 드로우 콜 추가 없이 풀스크린 패스 하나로 전체 씬에 적용되지만, 두께가 화면 해상도·
  거리에 따라 달라지고 헐 방식만큼 깔끔한 검정 링이 안 나올 수 있다.
- **권장(1차)**: 지금 있는 인버티드 헐은 유지, 스크린스페이스는 **크리즈 라인의 대체재 후보**로
  검토(내부 디테일 엣지 목적이 겹치므로) — 헐(실루엣)과 병행. 완전 교체는 시각 비교 후 판단
  (이 환경은 GPU 없어 실측 불가, §9).

### 7.4 디버그 뷰

`FrameSettings`(또는 새 디버그 토글)에 `gbufferView` 열거형(`None/Normal/Depth/Ao`) 추가 →
`CompositePass`가 그 값이면 최종 컬러 대신 해당 버퍼를 그대로 화면에 표시. 개발 중 노멀/깊이가
제대로 채워지는지 눈으로 검증하는 가장 직접적인 방법 — **구현 1단계에서 가장 먼저 만들 것**
(§9 구현 순서 1번).

---

## 8. 스레드 / 불변 규칙 매핑

| 규칙 | 이 설계에서 |
|---|---|
| 1 (D3D11 은 렌더 스레드만) | 모든 G-버퍼 텍스처·RTV·SRV 생성과 리졸브는 `Dx11Renderer` 안(렌더 스레드). 새 패스들의 `Initialize`/`Execute`도 마찬가지 |
| 2 (렌더러 코어는 clear/bind/pass 순회 + `ShaderLibrary` 소유만) | G-버퍼 텍스처는 셰도우맵과 동급 취급 — **렌더러 코어가 소유**(여러 패스가 공유하므로), 그리기 자체는 `IRenderPass`(`SsaoPass`/`PostCompositePass`)가. 코어는 리졸브 호출 + `PassContext`/`GBufferContext` 조립만 |
| 3 (경계는 값 스냅샷만) | G-버퍼는 **렌더 스레드 전용 GPU 리소스** — `RenderSnapshot`에 안 실림(스냅샷은 그리기 *입력*만, 이건 그리기 *중간 산출물*). AO 세기 같은 튜닝값이 게임 상태를 반영해야 하면 `Scene3D`에 값 필드 추가(예: `postProcess.aoStrength`) — 텍스처 자체는 절대 스냅샷에 안 들어감 |
| 6 (`ParallelFor` 무관) | 이 설계는 전부 렌더 스레드 그리기 — 메인 스레드 시뮬레이션과 무관, 규칙 6 해당 없음 |
| 7 (2D/3D 분리) | 전부 `render/r3d`(월드 깊이/노멀은 3D 개념). 2D 패스(`QuadPass2D`/`SpritePass2D`)는 이 인프라를 안 쓰고 그대로 최종 컬러 위에 그림(§3 순서상 컴포짓 다음) |

---

## 9. 구현 순서 제안 (측정 게이트마다 멈춤 — 실제 구현 시)

1. **오프스크린 RT 인프라 + 풀스크린 삼각형 헬퍼 + 디버그 뷰 스위치(§7.4).** 아직 G-버퍼 내용은
   없이 "씬 컬러를 한 번 더 텍스처로 거쳐 백버퍼로" 만 검증(패스스루) — 인프라 배관이 맞는지
   확인. 이 단계에서 `PassContext`/새 `GBufferContext` 형태를 확정.
2. **깊이 SRV 바인드**(§4.1, `R24G8_TYPELESS`) + 디버그 뷰로 깊이 시각화(선형화해서 그레이스케일).
   파티클 소프트 파티클(§7.2)이 이 시점부터 이미 가능해짐.
3. **뷰공간 노멀 MRT**(§4.2) — `Frame` cbuffer에 `view`/`normalMatrix` 추가(파티클 문서의
   camRight/camUp 요구와 **한 번에 같이** 확장 — §4.2-a). `MeshPass3D`(큐브/평면, 가장 단순한
   셰이더)부터 SV_TARGET1 추가해 검증, 그다음 `ModelMeshPass3D`. 디버그 뷰로 노멀 시각화.
4. **MSAA 리졸브 확장**(§4.3) — 노멀 하드웨어 리졸브 + 깊이 샘플0 방식. 멀티샘플 켠 채로
   1~3단계 디버그 뷰가 안 깨지는지 확인.
5. **SSAO 패스**(§5) — half-res, 16-샘플 커널, 노이즈 텍스처. 디버그 뷰로 AO만 단독 확인.
6. **합성 패스**(§6) — AO 곱, 세기 파라미터 노출, 셀 룩에 맞게 톤 조정(§5.2). 이 시점에 안개(§7.1)
   도 같이 넣으면 싸게 딸려온다.
7. (선택) 스크린스페이스 아웃라인 프로토타입(§7.3) — 크리즈 라인과 시각 비교 후 채택 여부 결정.
8. (선택) 톤매핑 — `lighting.md`와 합류.

각 단계 독립 커밋 가능, 인프라만(1~2단계) 있어도 소프트 파티클·안개 같은 저비용 이펙트를 바로
못 박을 수 있어 초기 가치가 크다. **이 환경은 GPU 없음(WARP)** — 시각 결과는 실기에서 검증 필요,
CLI 빌드는 셰이더 컴파일·리소스 생성 성공 여부까지만 확인 가능.

---

## 10. 사용 방법 (How to use) — *구현 후 기준*

### 10.1 AO 세기 조정

`render/r3d/PostCompositePass`의 `kAoStrength`(0=AO 없음, 1=완전 반영) 또는 `Scene3D`에
필드로 승격해 `SnapshotBuilder`에서 세팅. 셀 룩이 지저분해 보이면 `ssao.hlsl`의 `power`를
올려 극단화(§5.2).

### 10.2 새 풀스크린 이펙트 추가 (예: 비네트)

```cpp
// 1. assets/shaders/vignette.hlsl — 풀스크린 삼각형 VS 공용 헬퍼 재사용, PS 만 새로
// 2. render/r3d/VignettePass : IRenderPass 구현, Initialize 에서 shaders.Get(...)
// 3. main.cpp: renderer.AddRenderPass(std::make_unique<VignettePass>())  ← 컴포짓 다음, 2D 이전
```

기존 SSAO/컴포짓 패스는 안 건드림(OCP) — 체인에 패스 하나 더 끼우는 것뿐.

### 10.3 디버그로 G-버퍼 들여다보기

`FrameSettings::gbufferDebugView = GBufferView::Normal` (또는 `Depth`/`Ao`) — 개발 중 F-키
토글 등으로 연결해두면 셰이더 수정 즉시(핫리로드) 결과 확인 가능.

### 10.4 소프트 파티클 연결

`ParticlePass3D::Initialize`에서 `PSSetShaderResources`로 이 문서의 §4.1 깊이 SRV를 t1(또는
빈 슬롯)에 바인드 — 렌더러가 `PassContext`(또는 `GBufferContext`)로 넘겨주는 값을 그대로 받아
쓰면 된다. 파티클 쪽 코드 변경은 PS 한 줄(`saturate(...)` 페이드) 추가로 끝.

### 10.5 하지 말 것

```text
✗ G-버퍼 텍스처를 RenderSnapshot 에 담기 — 렌더 스레드 전용 GPU 리소스다(규칙 3 위반)
✗ 깊이를 SRV 로 바인드된 채로 같은 프레임에 DSV 로도 쓰기 — 셰도우맵처럼 매번 detach 후 사용
✗ 멀티샘플 텍스처를 일반 Texture2D::Sample 로 읽기 — Texture2DMS::Load(px, sampleIndex)
✗ SSAO 강도를 실사 렌더링 값 그대로 (셀 룩엔 과함) — §5.2 절제 필요
✗ Frame cbuffer 를 SsaoPass 전용으로 새로 정의 — FrameConstantsGpu 공유(불변 규칙)
✗ 노멀/깊이 리졸브를 컬러 리졸브와 같은 타이밍(Present 직전)에 — SSAO 가 그 전에 읽어야 함
✗ 풀스크린 패스마다 자기 정점 버퍼 새로 만들기 — 공용 헬퍼(SV_VertexID 트라이앵글) 하나 공유
```

---

## 11. 판단 필요 / 열린 질문

- **MRT(결정 A, 이 문서 채택) vs 별도 노멀+깊이 프리패스(결정 B)**: A는 그리기 1회로 끝나지만
  기존 `MeshPass3D`/`ModelMeshPass3D`의 PS를 전부 손대야 한다(변경 폭 큼, 여러 셰이더 파일).
  B는 지오메트리를 두 번 그리지만(비용 ↑) 기존 컬러 패스를 완전히 안 건드리고 새 프리패스
  하나만 추가하면 되며, **AO를 조명 계산에 실제로 통합**(post-multiply가 아니라 `ApplyLighting`
  안에서 곱)하려면 컬러 패스보다 먼저 AO가 준비돼 있어야 하므로 B가 유리하다. **1차는 A(간단함
  우선)로, "AO를 라이팅에 진짜 섞고 싶다"는 요구가 생기면 B로 재검토.**
- **AO를 라이팅에 통합 vs 화면 전체 post-multiply**: 이 문서의 §6은 post-multiply(최종 컬러에
  곱)만 다룬다 — 구현 간단하지만 "물리적으로 정확한" 통합은 아니다(라이팅 계산 자체엔 AO가
  안 들어감). 셀 셰이딩 게임에서는 이 정도 근사로 충분한 경우가 많다.
- **`view`/`normalMatrix`를 `Frame` cbuffer에 추가하는 시점**: 이 문서(뷰공간 노멀)와
  [particle-system-research.md](particle-system-research.md)(카메라 right/up, §3) 둘 다
  `Frame` cbuffer 확장을 요구한다 — 실제 구현 착수 시 **어느 쪽이든 먼저 시작하는 기능이 cbuffer
  확장을 하고, 나머지는 이미 있는 필드를 재사용**하도록 조율(중복 필드 방지, DRY). → 구체적
  해법은 §12.4(`view` 행렬 하나만 추가하면 두 요구 모두 커버됨).
- **half-res AO의 업샘플 아티팩트**: 얇은 물체(캐릭터 팔다리) 경계에서 bilinear 업샘플이 번져
  보일 수 있음 — 실기 확인 후 필요하면 depth-aware 업샘플(bilateral)로 교체.
- **디버그 뷰 스위치를 배포 빌드에 남길지**: 다른 디버그 기능(`DebugDrawPass`)과 동일하게
  "개발 편의, 배포엔 굳이" — `#if _DEBUG` 로 감쌀지는 실제 구현 시 결정.
- **컴퓨트 셰이더로 SSAO 이전 시점**: 지금은 픽셀 셰이더 풀스크린 패스로 충분(`command-playbook`
  3f 컴퓨트 지원 자체가 아직 없음). 성능 병목이 실측되면 그때.

---

## 12. 구현 착수 체크리스트 — 수정할 파일 vs 새로 만들 파일

이 섹션은 "지금 셰이더가 어떻게 생겼는지"는 무시하고(§1~§11의 설계를 그대로 목표 상태로 두고),
**그 목표 상태를 만들려면 무엇을 고치고 무엇을 새로 만들어야 하는지**만 파일 단위로 정리한다.
셰이더 파일들의 **PS 출력 시그니처가 바뀐다는 사실**(SV_TARGET 1개 → 2개)만 확정이고, 각 셰이더의
셰이딩 내용 자체(셀 밴드 각도, 크리즈 색 등)는 그대로 두거나 나중에 따로 바꿔도 된다 — 이 문서가
결정할 일이 아니다.

### 12.1 렌더 파이프라인 구조 변경 — 가장 큰 변경

지금 `Dx11Renderer::Render`는 **한 번 바인드(`OMSetRenderTargets(1, &sceneColorRtv, sceneDepthDsv)`)
하고 `m_passes`를 순서대로 전부 돌리는 단일 루프**다. G-버퍼를 넣으려면 프레임 안에서 렌더타깃
구성이 **세 번 바뀐다** — ① MRT(컬러+노멀, depth 포함) ② 포스트(SSAO+합성, 단일 컬러 타깃, depth
는 SRV로만) ③ 오버레이(UI, 단일 컬러 타깃, depth 없음). 지금처럼 "한 번 바인드하고 전부 순회"로는
표현이 안 된다.

**최소 변경안(권장)**: `IRenderPass` 인터페이스나 `m_passes` 자료구조 자체는 안 건드리고,
`Dx11Renderer`가 이미 갖고 있는 **"마지막 N개는 항상 특정 역할"** 관용구(지금은 `QuadPass2D`
하나)를 **"마지막 2개"로 확장**한다:

```cpp
// 생성자 (지금: { MeshPass3D, QuadPass2D })
m_passes = { std::make_unique<MeshPass3D>(),
             std::make_unique<PostProcessPass>(),   // 신규 — 내부에서 리타깃 여러 번
             std::make_unique<QuadPass2D>() };
// AddRenderPass 의 기본 삽입 지점을 end()-1 → end()-m_trailingPassCount(=2) 로.
// atEnd=true(SpritePass2D 용도)는 지금처럼 진짜 맨 끝 push_back — 안 바뀜.
```

`PostProcessPass::Execute` 하나가 내부에서 **여러 번 `OMSetRenderTargets`를 호출**해 위 ①→②→③
전환을 스스로 수행한다 — 즉 이 패스가 실행되고 나면 파이프라인은 "오버레이가 그리기 좋은 상태"로
남아있고, 그 뒤에 도는 `QuadPass2D`/`SpritePass2D`는 지금 코드 그대로 아무것도 모른 채 잘 그려진다
(OCP — 2D 패스 코드 변경 0).

**대안(더 명시적이지만 변경 폭 큼)**: `IRenderPass`에 `Stage Stage() const`(`Geometry`/
`PostProcess`/`Overlay`) 게터를 추가하고 `Dx11Renderer::Render`가 스테이지 경계마다 리타깃 — 인터페이스
변경이라 기존 패스 전부(`MeshPass3D`, `ModelMeshPass3D`, `DebugDrawPass`, `QuadPass2D`,
`SpritePass2D`)가 그 게터를 구현해야 한다. **1차는 위 최소 변경안으로, 포스트 스테이지가 여러
사용자 확장 패스를 필요로 하게 되면(SSAO 말고 다른 포스트 이펙트가 여럿 생기면) 이 대안으로 전환.**

### 12.2 `main.cpp` — `AddRenderPass` 호출부

```cpp
// 지금
renderer.AddRenderPass(std::make_unique<ModelMeshPass3D>(...));           // 기본 삽입(=지오메트리 구간)
renderer.AddRenderPass(std::make_unique<DebugDrawPass>());                // 기본 삽입
renderer.AddRenderPass(std::make_unique<SpritePass2D>(...), /*atEnd=*/true);
```

`bool atEnd` 파라미터·호출 형태는 **안 바뀐다**(§12.1 최소 변경안 채택 시) — `PostProcessPass`가
내부적으로 생성자에서 끼워지므로 `main.cpp`는 손댈 필요가 없다. **대안(§12.1의 Stage 게터 방식)을
택하면 이 파일의 호출부 3곳 모두 시그니처가 바뀐다** — 최소 변경안을 권장하는 이유 중 하나.

### 12.3 `Dx11Renderer.h` / `.cpp` — 씬 타깃 리소스

| 리소스 | 변경 |
|---|---|
| `m_sceneDepth`/`m_sceneDepthDsv` | 포맷 `D24_UNORM_S8_UINT` → `R24G8_TYPELESS`, `BindFlags`에 `D3D11_BIND_SHADER_RESOURCE` 추가. DSV 뷰는 `D24_UNORM_S8_UINT`로 그대로, **신규** SRV 뷰(`R24_UNORM_X8_TYPELESS`) 추가 → `m_sceneDepthSrv` |
| `m_sceneNormal`/`m_sceneNormalRtv` | **신규.** `RGBA8_UNORM`, `RENDER_TARGET|SHADER_RESOURCE`, 씬 컬러와 같은 크기/샘플수 |
| `m_sceneNormalResolved`/`m_sceneNormalResolvedSrv` | **신규, 멀티샘플일 때만.** 논-MS `RGBA8_UNORM`, `ResolveSubresource`로 채움(하드웨어 리졸브 가능 — 컬러와 동일 방식) |
| `m_sceneDepthResolved`/`m_sceneDepthResolvedSrv` | **신규, 멀티샘플일 때만.** 논-MS `R32_FLOAT`. 깊이는 하드웨어 리졸브가 없으므로 `PostProcessPass`(또는 별도 작은 풀스크린 패스)가 `Texture2DMS<float>::Load(px, 0)`로 채움(§4.3의 "샘플 0" 방식) |
| `CreateSceneTargets`/`ReleaseSceneTargets`/`ResizeBackBuffer` | 위 5개 리소스 생성/해제/리사이즈 반영 |
| 프레임 끝 `if (m_sampleCount > 1) ResolveSubresource(backBuffer, ..., m_sceneColor, ...)` | **`PostProcessPass`가 활성화되면 이 호출이 그 패스 내부로 흡수된다** — 합성 셰이더가 `Texture2DMS` 컬러를 직접 읽어 AO를 곱하며 그 자체가 "커스텀 리졸브"를 겸함(별도 리졸브 불필요). 포스트 파이프라인이 없으면(1x MSAA 또는 아직 안 만든 상태) 기존 단순 리졸브 경로를 그대로 유지 — 두 경로 병존 |

### 12.4 `FrameConstants.h` + `common3d.hlsli` — `Frame` cbuffer 확장

```cpp
// FrameConstantsGpu 에 추가
float view[16];   // camera.view 그대로 (row-major) — 아래 세 가지를 전부 이걸로 커버
```

```hlsl
// common3d.hlsli 의 cbuffer Frame 에 동일 필드 추가 (항상 같이 고침 — 불변 규칙)
row_major float4x4 view;
```

- 이 필드 하나가 **§4.2(뷰공간 노멀, `mul(worldNormal, (float3x3)view)`)** 와
  [particle-system-research.md](particle-system-research.md) §3(**카메라 right/up** —
  `view`의 0행/1행이 곧 월드공간 카메라 축)를 **동시에** 커버한다 — §11에서 지적한 "두 문서가
  같은 cbuffer를 요구" 문제의 해법. 어느 기능을 먼저 구현하든 `view` 필드를 추가하고, 나중
  기능은 필드 추가 없이 그걸 재사용.
- `common3d.hlsli`에 헬퍼 추가 권장: `float3 WorldToViewNormal(float3 worldNormal)` (곱셈
  1줄) — 모든 지오메트리 셰이더가 재사용, 중복 방지.

### 12.5 지오메트리 셰이더 — PS 출력 시그니처 변경 (내용은 무시, 구조만)

`float4 PSMain(...) : SV_TARGET` → `PSOut PSMain(...)` 형태로 바뀌는 파일들. **셀 밴드 각도·
크리즈 색 같은 셰이딩 로직 자체는 그대로 두고 반환 방식만 바꾼다.**

| 파일 | 변경 | 비고 |
|---|---|---|
| `mesh.hlsl` | SV_TARGET1 추가(노멀) | |
| `model.hlsl` | SV_TARGET1 추가 | |
| `cel.hlsl` | SV_TARGET1 추가 | |
| `mesh_instanced.hlsl` | SV_TARGET1 추가 | 크라우드 인스턴스 |
| `mesh_instanced_toon.hlsl` | SV_TARGET1 추가 | |
| `outline.hlsl` | **안 바꿔도 됨** | 검정 링만 그리고 노멀에 의미 있는 값이 없음 — SV_TARGET1 미출력 시 그 픽셀의 노멀 버퍼는 클리어값 유지(§4.2 참고, D3D11은 MRT 중 일부 슬롯만 쓰는 PS를 허용) |
| `crease.hlsl` | **판단 필요** | 크리즈 리본이 노멀에 기여해야 SSAO/엣지검출이 리본 자리를 반영 — 안 넣으면 그 자리는 밑면 노멀로 남아 약간 부정확(허용 가능한 근사인지는 실측 필요) |
| `shadow.hlsl`/`shadow_instanced.hlsl` | **안 바꿈** | 셰도우 depth-only 패스는 컬러/노멀 타깃 자체가 안 바인딩됨(무관) |
| `debugline.hlsl` | **판단 필요** | 디버그 라인이 G-버퍼에 노멀을 남기면 SSAO 계산에 (원치 않게) 영향 — 보통 노멀 미출력이 맞음 |

**공용 헬퍼 사용 예** (`common3d.hlsli::WorldToViewNormal`, §12.4):

```hlsl
struct PSOut { float4 color : SV_TARGET0; float4 normal : SV_TARGET1; };

PSOut PSMain(VSOut input)
{
    PSOut o;
    float shadow = SampleShadow(input.shadowClip);
    o.color  = float4(ApplyLighting(objColor.rgb, input.nrm, shadow), objColor.a);
    o.normal = float4(WorldToViewNormal(input.nrm) * 0.5f + 0.5f, 1.0f);
    return o;
}
```

### 12.6 지오메트리 패스 C++ 코드 (`MeshPass3D.cpp`, `ModelMeshPass3D.cpp`)

**변경 없음, 또는 최소.** MRT 바인딩(`OMSetRenderTargets`에 RTV 2개)은 `Dx11Renderer::Render`
(§12.1의 지오메트리 스테이지 시작부)가 하지 개별 패스가 하지 않는다 — 지금도 `MeshPass3D::Execute`
는 렌더타깃을 직접 바인드하지 않고 이미 바인드된 상태에서 그리기만 한다(`PassContext`가 정보만
전달). 인풋 레이아웃(정점 입력)도 안 바뀐다 — 오직 **PS가 참조하는 `.hlsl` 파일의 출력 슬롯 수**만
컴파일 시점에 바뀔 뿐, `ShaderLibrary::Get` 호출 시그니처(레이아웃 배열)는 VS 입력 기준이라 그대로.

### 12.7 신규 렌더 패스 (완전히 새로 작성)

| 파일 | 내용 |
|---|---|
| `src/render/r3d/PostProcessPass.h`/`.cpp` | **신규.** §12.1의 `PostProcessPass` — Initialize에서 SSAO 커널/노이즈 텍스처 생성 + 필요한 D3D 리소스(뷰포트별 RTV는 `Dx11Renderer`가 소유해 넘겨받음, §12.9) 준비. Execute에서: (a) 깊이 다운샘플(멀티샘플이면), (b) SSAO 계산, (c) 합성(+커스텀 리졸브), (d) 오버레이용 타깃으로 `OMSetRenderTargets` 재설정 |
| `assets/shaders/fullscreen.hlsli` | **신규.** 공용 VS — `SV_VertexID`(0,1,2)로 클립공간 커버 삼각형 정점 3개 생성, 모든 풀스크린 PS 가 include |
| `assets/shaders/depth_resolve.hlsl` | **신규.** `Texture2DMS<float>` 깊이 → 논-MS `R32_FLOAT` (샘플 0). 1x MSAA면 이 패스 자체를 스킵(그냥 원본 깊이 SRV 사용) |
| `assets/shaders/ssao.hlsl` | **신규.** §5의 반구 커널 PS |
| `assets/shaders/composite.hlsl` | **신규.** §6의 AO 곱 + (멀티샘플이면) 컬러 리졸브 겸임 + §7.1 안개 |

### 12.8 `PassContext`/`RenderPass.h` — 포스트 패스가 G-버퍼를 읽는 방법

`PostProcessPass`는 자기 스테이지 안에서 스스로 리소스를 관리하지만(§12.7), **깊이·노멀 원본은
`Dx11Renderer`가 소유**하고 있으므로(§12.3) 그 SRV를 건네받을 통로가 필요하다. 두 가지 선택지
(둘 다 이 코드베이스에 이미 선례가 있다):

- **A) `PassContext`에 필드 추가**(`sceneDepthSrv`, `sceneNormalSrv` 등, 기본 `nullptr`) — 지금
  `renderTarget`/`depthStencil`이 이미 이 방식(프레임마다 렌더러가 채워 넘김). 다른 패스는 그냥
  무시하면 됨(ISP상 문제 없음, 옵션 필드).
- **B) 전역 슬롯 바인딩** — 셰도우 SRV가 이미 이 방식이다(`Dx11Renderer::Render`가
  `PSSetShaderResources(1, 1, &m_shadowSrv)`를 패스 루프 진입 전에 한 번 호출, 개별 패스는
  `t1`을 셰이더에서 그냥 선언). `PostProcessPass`도 자기 차례가 되기 전에 렌더러가
  `t2`(깊이)/`t3`(노멀)에 바인드.
- **권장: A.** `PostProcessPass`가 이 SRV들을 실제로 쓰는 유일한 패스라 전역 슬롯보다
  `PassContext` 필드가 "이 패스가 이번 프레임에 필요한 입력"이라는 의도를 더 분명히 드러낸다.
  셰도우(B)는 **여러 패스**(모든 지오메트리 패스)가 공통으로 읽어야 해서 전역 슬롯이 맞았던
  것과 대비.

### 12.9 `RenderSnapshot`/`Scene3D` — 튜닝 값 (선택, 초기엔 상수로도 충분)

```cpp
// render/r3d/Scene3D.h 에 추가 (값 타입, 규칙 3)
struct PostProcessSettings
{
    float aoStrength{ 0.6f };
    float aoRadius{ 0.5f };
    float aoPower{ 1.5f };
    float fogNear{ 20.0f }, fogFar{ 80.0f };
    math::Color fogColor{ 0.44f, 0.49f, 0.57f, 1.0f };   // clearColor 와 맞추면 무난
};
struct Scene3D { /* ...기존... */ PostProcessSettings postProcess{}; };
```

`FrameSettings`(vsync/fps처럼 "렌더러가 어떻게 동작할지")가 아니라 **`Scene3D`가 맞다** — "이번
프레임에 무엇을 얼마나 그릴지"는 규칙 3(스냅샷은 값)의 대상. 디버그 뷰 스위치(§7.4)도 여기 열거형
필드로.

### 12.10 변경/신규 파일 한눈에

| 파일 | 종류 |
|---|---|
| `src/render/Dx11Renderer.h`/`.cpp` | 수정 — 씬 타깃 리소스 확장(§12.3), 파이프라인 3스테이지화(§12.1), `AddRenderPass` 기본 삽입 지점(§12.1) |
| `src/render/RenderPass.h` | 수정 — `PassContext`에 G-버퍼 SRV 필드 추가(§12.8, 옵션 A 채택 시) |
| `src/render/r3d/FrameConstants.h` | 수정 — `view` 필드 추가(§12.4) |
| `assets/shaders/common3d.hlsli` | 수정 — `cbuffer Frame`에 `view` 추가 + `WorldToViewNormal` 헬퍼(§12.4/§12.5) |
| `assets/shaders/{mesh,model,cel,mesh_instanced,mesh_instanced_toon}.hlsl` | 수정 — PS 출력 2개로(§12.5) |
| `assets/shaders/{outline,shadow,shadow_instanced,debugline}.hlsl` | 안 바꿈(§12.5) |
| `assets/shaders/crease.hlsl` | 판단 필요(§12.5) |
| `src/render/r3d/MeshPass3D.cpp`/`ModelMeshPass3D.cpp` | 변경 없음 또는 최소(§12.6) |
| `src/render/r3d/PostProcessPass.h`/`.cpp` | **신규**(§12.7) |
| `assets/shaders/fullscreen.hlsli` | **신규**(§12.7) |
| `assets/shaders/depth_resolve.hlsl` | **신규**(§12.7) |
| `assets/shaders/ssao.hlsl` | **신규**(§12.7) |
| `assets/shaders/composite.hlsl` | **신규**(§12.7) |
| `src/render/r3d/Scene3D.h` | 수정 — `PostProcessSettings` 추가(§12.9) |
| `src/game/SnapshotBuilder.cpp` | 수정 — `PostProcessSettings` 채우기 |
| `src/main.cpp` | **안 바뀜**(§12.1 최소 변경안 채택 시, §12.2) |

### 12.11 구현 순서 — 위험도/의존성 순 (§9를 이 체크리스트 기준으로 구체화)

1. `Frame` cbuffer에 `view` 추가(§12.4) — 다른 모든 단계의 선행 조건, 위험 없음(필드 추가만).
2. 씬 깊이 SRV 바인드 가능하게(§12.3 깊이 부분) + 디버그 뷰로 확인. 이 시점부터 소프트 파티클도 가능.
3. `PostProcessPass` 골격 + `AddRenderPass` 삽입 지점 변경(§12.1) — 아직 아무 이펙트 없이 "패스스루"만(입력 그대로 출력). 파이프라인 배관이 맞는지 검증.
4. 노멀 MRT(§12.3 노멀 부분 + §12.5 셰이더들, `MeshPass3D`의 `mesh.hlsl`부터) — 디버그 뷰로 노멀 시각화.
5. 나머지 지오메트리 셰이더(`model.hlsl`/`cel.hlsl`/인스턴스드 2종)도 SV_TARGET1 추가.
6. 깊이 다운샘플(§12.7 `depth_resolve.hlsl`) + 노멀 리졸브.
7. SSAO(§12.7 `ssao.hlsl`) — 디버그 뷰로 AO 단독 확인.
8. 합성(§12.7 `composite.hlsl`, MSAA 리졸브 흡수) — AO 곱 + 안개, 셀 룩에 맞게 세기 조정.
9. (선택) `crease.hlsl` 노멀 기여 여부 결정, 스크린스페이스 아웃라인 프로토타입.

각 단계 독립 검증 가능. 1~3단계만으로도 다른 이펙트(소프트 파티클, 향후 톤매핑)를 위한 배관이
갖춰진다.

---

## 13. 관련 문서

- [msaa.md](msaa.md) — 씬 타깃 구조, 이 문서가 확장하는 지점(§4.3)
- [shadows.md](shadows.md) — 오프스크린 depth→SRV의 기존 선례, `GBufferContext` 설계가 본뜬 `ShadowContext`
- [lighting.md](lighting.md) / [toon-rendering.md](toon-rendering.md) — 이 인프라가 얹히는 조명·셀 셰이딩, 스크린스페이스 아웃라인이 비교 대상으로 삼는 인버티드 헐·크리즈 라인
- [shader-pipeline.md](shader-pipeline.md) — 새 `.hlsl` 로딩 규약
- [particle-system-research.md](particle-system-research.md) — 소프트 파티클(§11)이 이 문서의 깊이 SRV를 재사용, `Frame` cbuffer 확장 요구가 겹침(§11)
- [command-playbook.md](command-playbook.md) — 3b(새 패스), 3h(조명), 3i(AA/포스트), 3f(컴퓨트, 향후)
