# 프레넬(림 라이트) 리서치 — 아웃라인·셀·크리즈와의 통합

**구현 완료** — §8의 계획대로 `common3d.hlsli::RimLight` + `cel.hlsl` + `ModelMeshPass3D::MaterialRimStrength`로 들어갔다. 사용법·파라미터표는 [toon-rendering.md](toon-rendering.md) "프레넬(림 라이트)" 절 참조. 이 문서는 왜 이 설계(뷰공간 계산으로 cbuffer 확장 회피, 연속 대신 밴드형, 재질별 억제)를 골랐는지의 리서치 기록으로 남긴다.

목표(당시): 지금 구현된 3패스 툰 파이프라인(`docs/toon-rendering.md`: 아웃라인 → 셀 셰이딩 → 크리즈)에 프레넬 기반 림 라이트를 **어디에, 어떤 수식으로, 무엇과 충돌 않게** 추가할지.

## 1. 지금 상태

`ModelMeshPass3D`가 3패스로 그린다 (`docs/toon-rendering.md`):

```
1. outline.hlsl   인버티드 헐, front-cull, 깊이만 앞서 기록, 실루엣 링(순검정)
2. cel.hlsl       텍스처 + ApplyCelLighting(4밴드) + 그림자, GeometryPSOut(color+normal)
3. crease.hlsl    CPU 생성 리본, 정점색 통과, depth LESS_EQUAL·기록 off
```

프레넬/림은 **없음** (`fresnel|rim` 전체 검색 결과 셰이더·렌더 코드에 0건). `common3d.hlsli`엔 `HemisphereAmbient` / `ApplyLighting` / `ApplyCelLighting` / `SampleShadow`만 있다.

## 2. 프레넬이 왜 툰 룩에 쓰이나

실사 프레넬(그레이징 각에서 반사율↑)을 애니메이션풍으로 과장한 것 = **실루엣 근처 면을 밝게 감싸는 림 라이트**. 셀 셰이딩은 밴드가 하드해서 광원 반대쪽(50°+, 검정 밴드)이 뭉텅 죽는데, 림이 그 실루엣 경계를 살짝 밝혀 입체감·"가장자리가 빛을 받는" 애니메이션 특유의 팝을 만든다. 지금 엔진은 그 자리를 앰비언트(`HemisphereAmbient`)와 아웃라인 링(순검정)이 채우고 있어, 이 둘 사이 — 실루엣 바로 안쪽 — 가 림이 들어갈 자리다.

## 3. 어느 공간에서 계산하나 — Frame cbuffer 확장 불필요

프레넬은 `N·V`가 필요하다(`V` = 표면→카메라 방향). 순진하게 하면 카메라의 월드 위치가 있어야 하는데, **뷰 공간에서 계산하면 카메라 위치가 항상 원점**이라 이미 있는 `view` 행렬만으로 끝난다:

```hlsl
float3 viewPos = mul(worldPos, view).xyz;   // Frame.view, 이미 §3.3에서 SSAO용으로 추가돼 있음
float3 V = normalize(-viewPos);              // 뷰공간에서 카메라는 원점
float3 N = normalize(WorldToViewNormal(worldNormal));   // 이미 있는 헬퍼 (common3d.hlsli)
float ndv = saturate(dot(N, V));
```

→ **`Frame` cbuffer도 `FrameConstants.h`도 안 건드린다.** `WorldToViewNormal`이 이미 §3.3/§3.4(post-process-gbuffer-research.md)에서 SSAO 노멀 G-버퍼용으로 추가된 걸 그대로 재사용 — 두 번째 소비처가 생기는 것뿐. 이게 "카메라 월드 위치를 cbuffer에 새로 추가" 안보다 나은 이유: CLAUDE.md 불변 규칙(`Frame`/`FrameConstantsGpu` 동시 수정)의 리스크 표면을 하나도 늘리지 않는다.

## 4. 연속 프레넬 vs 밴드형 림 — 밴드형을 권장

두 갈래:

**A. 연속 프레넬 (물리 근사)**
```hlsl
float fresnel = pow(1.0f - ndv, rimPower);   // rimPower ≈ 2~4
```
부드러운 그라데이션. 문제: 셀 셰이딩은 4단 하드 밴드인데 이것만 매끈하면 룩이 어긋난다 — 셀 표면은 계단인데 가장자리만 아날로그로 빛나는 이질감.

**B. 밴드형 림 (권장)** — `ApplyCelLighting`이 이미 하는 것(각도 임계값 + `smoothstep`으로 부드럽힌 하드 컷)과 같은 어휘를 씀:
```hlsl
float rim = smoothstep(rimThreshold - rimSoftness, rimThreshold + rimSoftness, 1.0f - ndv);
```
`rimThreshold`(예 0.6~0.75) 이상 그레이징에서만 켜지는 좁은 밴드 하나. 셀의 "각도를 끊어 단을 만든다"는 철학과 통일되고, 폭(`rimThreshold`)·경계 부드러움(`rimSoftness`)이 `CEL_BAND_SOFTNESS`와 같은 성격의 손잡이라 튜닝 어휘가 늘지 않는다.

권장: **B**. 이유는 위 일관성 + 아래 §6 아웃라인과의 시각적 충돌 제어가 A보다 쉬움(경계가 명확해 아웃라인 링과의 간격을 수치로 조절 가능).

## 5. 림을 광원 방향에 묶을지

두 스타일:
- **전방위 림**(`rim` 그대로): 어느 각도든 그레이징이면 켜짐. 구현 단순, 셀프 라이트처럼 보일 위험(어디서 봐도 가장자리가 빛남 — 물리적으로 이상).
- **키라이트 반대쪽 림**(림 = 백라이트/키커 느낌, 애니메이션에서 더 흔함): `rim *= saturate(dot(-keyDirection.xyz, V_world또는 근사))`처럼 광원 반대 방향일 때만 강해지게. 즉 카메라가 광원 쪽을 보고 있을 때(피사체가 역광)만 림이 뚜렷.

권장: **전방위로 시작, 세기(`rimStrength`)를 낮게(0.15~0.3) 잡아 과하지 않게** — 광원-의존 버전은 `-keyDirection`을 뷰 공간으로 한 번 더 돌려야 해서 셰이더가 복잡해지는데, 지금 3점 조명이 아니라 key 하나뿐이라 이득이 크지 않다. 나중에 여러 라이트가 생기면 재검토.

## 6. 아웃라인과의 충돌 — 진짜 문제는 겹쳐 그려지는 게 아니라 "이중 테두리"로 보이는 것

아웃라인(순검정 링)과 셀 표면(+림)은 **서로 다른 드로우콜, 서로 다른 픽셀**이라 실제 렌더 충돌(z-fight, 오버드로우)은 없다 — 아웃라인은 부풀린 헐의 뒷면만, 셀은 원래 표면만 그린다. 문제는 순수하게 시각적이다: 검정 링 바로 안쪽에 밝은 림까지 있으면 "검정 테두리 + 밝은 테두리"가 이중으로 읽혀 지저분해진다.

대응:
1. `rimThreshold`를 아웃라인 두께(`kOutlineWidth = 0.002`, 화면공간)보다 명백히 안쪽에서 시작하게 크게 잡는다(예 0.65 이상 — 진짜 실루엣 끝부분만).
2. `rimStrength`를 낮게(연출 확인 전까지 0.2 이하) — 강한 흰 테두리가 아니라 은은한 광택 정도로.
3. 페이스/피부처럼 `shadowBias`가 이미 예외 처리된 재질(§`docs/toon-rendering.md` "얼굴 그림자 제어")엔 림도 약화 — 눈·눈썹 라인에 림이 끼면 지저분해짐. `MaterialShadowBias` 테이블을 재사용해 같은 재질 판정으로 `rimStrength`도 0에 가깝게.

## 7. 크리즈 라인과의 관계

크리즈는 `crease.hlsl`에서 조명과 무관한 **고정 정점색**(두 면 평균색 × 채도/명도 스케일, AO 느낌)이라 림의 영향을 받지 않는다 — 손댈 필요 없음. 다만 크리즈가 실루엣 가장자리 근처(예: 팔 윤곽 안쪽 옷 주름)에 있으면 림 밴드와 겹쳐 보일 수 있는데, 크리즈는 `surfaceOffset`만큼 셀 표면 위에 얹히는 별도 지오메트리라 밑에 깔린 셀 표면의 림이 그대로 비쳐 보인다 — 실제 문제 시 크리즈 알파를 살짝 낮추거나 무시(발생 시 재검토, 지금은 이론적 우려).

## 8. 구현 계획 (착수 시)

**셰이더 (`common3d.hlsli`)** — 새 헬퍼 하나, 기존 함수 시그니처는 안 건드림:
```hlsl
// Banded rim light from the grazing angle (view-space N·V). No new Frame field -
// reuses `view` (already added for SSAO, §3.3) since the camera sits at the
// view-space origin. threshold/softness are the same vocabulary as
// ApplyCelLighting's band edges.
float RimLight(float3 worldNormal, float3 worldPos, float threshold, float softness)
{
    float3 viewPos = mul(worldPos, view).xyz;
    float3 v = normalize(-viewPos);
    float3 n = normalize(WorldToViewNormal(worldNormal));
    float grazing = 1.0f - saturate(dot(n, v));
    float s = max(softness, 0.001f);
    return smoothstep(threshold - s, threshold + s, grazing);
}
```
`cel.hlsl`의 `PSMain`에서 `ApplyCelLighting` 결과에 더한다:
```hlsl
float rim = RimLight(input.nrm, worldPos.xyz, uRimThreshold, uRimSoftness);
output.color.rgb += keyColor.rgb * rim * uRimStrength * uRimMask;   // uRimMask: 재질별 억제, §6.3
```
`worldPos`를 PS로 넘기려면 `VSOut`에 필드 추가 필요(지금 `cel.hlsl` VSOut엔 없음 — `shadowClip`처럼 하나 더).

**C++ (`ModelMeshPass3D.cpp`)** — `CelParamsGpu`의 남는 `pad[3]`을 씀(레이아웃 크기 안 늘어남, b3는 `FrameConstants.h`의 전역 불변 규칙 대상이 아니라 이 파일 로컬):
```cpp
struct CelParamsGpu { float shadowBias; float rimThreshold; float rimSoftness; float rimStrength; };
```
`SubMesh`에 `rimStrength` 필드 추가, `MaterialShadowBias`와 같은 방식으로 재질명 판정(§6.3) — 얼굴류는 낮게, 그 외 기본값(예 0.2).

**끄기**: `rimStrength = 0`이면 `RimLight` 호출 자체가 무의미(더하는 값이 0) — 별도 `#if`/토글 불필요, 셀 셰이딩과 동일한 패턴(`aoStrength` 등 값으로 끄기).

## 9. 튜닝 파라미터 요약 (제안값)

| 파라미터 | 위치(예정) | 제안 기본값 | 의미 |
|---|---|---:|---|
| `uRimThreshold` | `CelParams` b3 | 0.65 | 이 grazing 값 이상에서만 림 시작 (1에 가까울수록 실루엣에 더 붙음) |
| `uRimSoftness` | 〃 | 0.15 | 경계 전이 폭 — `CEL_BAND_SOFTNESS`와 같은 성격 |
| `uRimStrength` | 〃 | 0.2 (재질별, 얼굴류는 ~0) | 최종 곱 세기 |
| `rimColor` | 재사용 | `keyColor.rgb` | 별도 필드 없이 키 라이트 색 재사용 — 광원과 무관한 흰 림을 원하면 나중에 필드 추가 |

## 10. 파이프라인 순서 — 변경 없음, 이유만 재확인

```
1. outline.hlsl   (변경 없음 — 프레넬은 지오메트리가 아니라 셀 표면의 셰이딩 항이라 순서 무관)
2. cel.hlsl       (+ RimLight 항 추가 — 여기서만 손댐)
3. crease.hlsl    (변경 없음 — §7)
```
아웃라인이 먼저 그려져 깊이를 선점해야 링이 실루엣에 살아남는 기존 원리(§`docs/toon-rendering.md` "2. 실루엣 아웃라인")는 그대로. 프레넬 추가로 패스 순서나 깊이/래스터라이저 상태를 바꿀 이유는 없다.

## 11. 검증 계획

이 컨테이너는 GPU 없음 — 빌드·핫리로드 문법만 확인 가능, 실제 룩은 F5(Windows/VS)에서. 확인할 것: (1) 림이 아웃라인과 이중 테두리로 안 보이는지 — §6 튜닝, (2) 얼굴 재질에서 림이 눈/눈썹 라인을 지저분하게 안 만드는지, (3) `uRimStrength=0`일 때 기존 룩과 완전히 동일한지(회귀 없음).

## 12. 관련 문서

[toon-rendering.md](toon-rendering.md)(구현된 아웃라인/셀/크리즈), [lighting.md](lighting.md)(`Frame` cbuffer·`ApplyCelLighting`), [post-process-gbuffer-research.md](post-process-gbuffer-research.md) §3.3(`view` 필드·`WorldToViewNormal`의 최초 도입 이유 — 이번에 두 번째 소비처가 됨), [shadows.md](shadows.md)(같은 `common3d.hlsli`를 공유하는 다른 셰이딩 항 `shadow`의 전달 방식 — 참고용 선례).
