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
  확장을 하고, 나머지는 이미 있는 필드를 재사용**하도록 조율(중복 필드 방지, DRY).
- **half-res AO의 업샘플 아티팩트**: 얇은 물체(캐릭터 팔다리) 경계에서 bilinear 업샘플이 번져
  보일 수 있음 — 실기 확인 후 필요하면 depth-aware 업샘플(bilateral)로 교체.
- **디버그 뷰 스위치를 배포 빌드에 남길지**: 다른 디버그 기능(`DebugDrawPass`)과 동일하게
  "개발 편의, 배포엔 굳이" — `#if _DEBUG` 로 감쌀지는 실제 구현 시 결정.
- **컴퓨트 셰이더로 SSAO 이전 시점**: 지금은 픽셀 셰이더 풀스크린 패스로 충분(`command-playbook`
  3f 컴퓨트 지원 자체가 아직 없음). 성능 병목이 실측되면 그때.

---

## 12. 관련 문서

- [msaa.md](msaa.md) — 씬 타깃 구조, 이 문서가 확장하는 지점(§4.3)
- [shadows.md](shadows.md) — 오프스크린 depth→SRV의 기존 선례, `GBufferContext` 설계가 본뜬 `ShadowContext`
- [lighting.md](lighting.md) / [toon-rendering.md](toon-rendering.md) — 이 인프라가 얹히는 조명·셀 셰이딩, 스크린스페이스 아웃라인이 비교 대상으로 삼는 인버티드 헐·크리즈 라인
- [shader-pipeline.md](shader-pipeline.md) — 새 `.hlsl` 로딩 규약
- [particle-system-research.md](particle-system-research.md) — 소프트 파티클(§11)이 이 문서의 깊이 SRV를 재사용, `Frame` cbuffer 확장 요구가 겹침(§11)
- [command-playbook.md](command-playbook.md) — 3b(새 패스), 3h(조명), 3i(AA/포스트), 3f(컴퓨트, 향후)
