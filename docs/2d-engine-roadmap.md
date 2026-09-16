# 2D 엔진 시스템 로드맵 (2D Engine Completion Roadmap)

목적: **특정 게임 장르가 아니라 2D 기반 자체의 완성도.** `docs/roadmap.md`(엔진 전체, 3D 포함)와
`docs/synopsis.md`(현재 확정 장르: 대규모 디펜스, 3D)와 별도로, **`ENGINE_WITH_2D` 한 축만 놓고
"게임을 뭘 만들든 2D로 하려면 엔진에 뭐가 비어 있는가"를 정리한다.**
장르 결정과 독립적 — 어떤 2D 게임(디펜스든 다른 장르든)을 얹어도 필요한 기반 시스템만 다룬다.

관련: `docs/texture-atlas-and-sprite-pass.md`(스프라이트+클리핑, 부분 구현), `docs/animation-design.md`
§2(2D 프레임 애니, 설계만), `docs/ui-architecture.md`(클리핑·레이아웃 미구현 항목), `docs/collider-design.md`
(2D 브로드페이즈 없음), `docs/atlas-build-pipeline.md`(BC7 압축 미구현), `docs/roadmap.md` §1(엔진 전체 표).

---

## 1. 현재 2D 인프라 — 있는 것

| 영역 | 있음 |
|---|---|
| 렌더 | `QuadPass2D`(단색 quad), `SpritePass2D`+`sprite2d.hlsl`(텍스처 스프라이트, atlas 1장 로드-원스), `render::TextureAtlas`/`AtlasIndex`(`.atlas` 파서) |
| 에셋 빌드 | `tools/atlas_pack` v1 — shelf 패킹 + gutter + 밉 + **무압축** `.dds` |
| 물리 | `physics/p2d`(Box/Circle, `CollisionWorld2D`) — 탐지 + layer/mask + `Contacts`, 레이캐스트 3질의(Closest/Any/All, `Ray2D`) |
| UI | `ui::Widget`/`UIWindow`/`Button`/`CheckBox`/`Slider`/`TextLine`/`ScrollList`(가상화+휠), 화면 전환(`SetScreen`/`SetOverlay`) |
| 코어 | 고정 스텝+time scale(`time-design.md`), `core::ObjectPool<T>`, `core::EntityId/EntityRegistry`(미연결) |
| 오디오 | XAudio2 믹서+스트리밍+voice 풀링(장르 무관, 이미 완성도 높음) |
| 이미지 디코드 | `import::LoadImageFromFile`(PNG/JPG/BMP/GIF/TGA, RGBA8) |

---

## 2. 갭 분석 — 없는 것 (리서치 결과)

### 2.1 렌더

| 항목 | 현재 상태 | 왜 필요 |
|---|---|---|
| **scissor 클리핑** (`ui::DrawList`+`PushClip`) | 없음 — `ScrollList`가 자기 Quad를 뷰포트로 clamp하는 임시 방편(옵션 A)만 | 패널/스크롤 영역 밖 그리기 방지. `texture-atlas-and-sprite-pass.md` §3에 설계 끝, 구현만 안 됨 |
| **2D 월드 카메라(팬/줌)** | 없음 — `sprite2d.hlsl`이 스크린 픽셀 좌표를 NDC로 직접 변환, 카메라 변환 자체가 없음 | 스크롤되는 2D 월드(뷰포트보다 큰 맵)를 만들 수 없음. 지금은 사실상 고정 카메라 UI 오버레이 수준 |
| **텍스처 압축(BC7/BC4)** | 없음 — `atlas_pack`이 무압축 `R8G8B8A8` `.dds`만 생성 | 메모리 4배. 아틀라스가 커지면(월드 타일셋 등) 바로 문제됨 |
| **GlyphAtlas(폰트)** | 없음 — `UI.cpp`의 5×7 절차 비트맵 폰트가 유일한 텍스트 렌더 | 로컬라이제이션·가독성 한계. 설계는 끝(`texture-atlas-and-sprite-pass.md` §1.3) |
| **2D 조명/그림자** | 없음 | 라이트맵 합성(가산 블렌딩 광원 + multiply 합성) + 가짜 그림자 스프라이트 방식이 적합(3D 캐스케이드 섀도우와 무관, 별도 절 필요) |
| **2D 파티클/VFX** | 없음 — `ParticlePass3D`는 3D 전용(빌보드가 3D 카메라 축 필요) | 2D는 스프라이트 시트 애니메이션(§2.3 프레임 애니로 대체 가능) 또는 화면공간 quad 파티클 별도 설계 필요 |
| **2D 포스트프로세싱**(비네트/컬러그레이딩/화면 흔들림) | 없음 — `PostProcessPass`는 `ENGINE_WITH_3D`로 감싸진 SSAO+안개 전용 | 낮/밤 톤, 피격 화면 효과 등에 필요 |
| 스왑체인 `FLIP_DISCARD` | 레거시 `DXGI_SWAP_EFFECT_DISCARD` | 장르 무관 기술부채, 2D 전환 시 같이 처리 적기 |

### 2.2 물리/충돌

| 항목 | 현재 상태 |
|---|---|
| **2D 브로드페이즈** | 없음 — `CollisionWorld2D::Step()`이 N² 브루트포스(3D는 이미 균일 그리드 구현됨, 대칭 안 맞음) |
| **트리거 enter/exit 이벤트** | 없음 — `Contacts()`가 "지금 겹침"만 보고, 이번 프레임에 새로 겹쳤는지/떨어졌는지 구분 안 함 |
| 스윕/CCD, 캡슐 콜라이더 | 없음 |
| 레이캐스트 가속 | 없음(선형 스캔) — 브로드페이즈 붙으면 자동 개선 여지 |

### 2.3 애니메이션

| 항목 | 현재 상태 |
|---|---|
| **2D 프레임 애니메이션**(`anim::a2d::SpriteAnimator`) | **설계만**(`animation-design.md` §2) — `SpriteDraw`가 있어야 그릴 대상이 생김(선행 조건은 이미 충족, §2.1 렌더 항목과 별개로 구현만 하면 됨) |
| `anim::core::AnimatorController` 상태머신 | 설계만, 실사용처 생기기 전까지 보류(YAGNI, 문서 자체 방침) |
| Live2D/Spine류 2D 스켈레탈 | 조사 전(`animation-design.md` §4) — 라이선스·툴체인 문제로 별도 결정 필요 |

### 2.4 UI

| 항목 | 현재 상태 |
|---|---|
| `VerticalStack`/`Measure`/`Arrange` | 미구현 — 좌표를 손으로 배치 |
| 진짜 scissor 클리핑 | 위 2.1과 동일 항목(UI가 최대 수혜자) |
| `Slider` 진짜 입력 캡처 | 없음 — 드래그 종료가 형제 위젯에 먼저 소비되면 그 프레임엔 안 풀릴 수 있음 |
| keyboard focus | 없음 |

### 2.5 에셋/코어

| 항목 | 현재 상태 |
|---|---|
| `AssetRegistry` + 비동기 로더 | 설계만(`loading-and-streaming.md`) — 지금 아틀라스도 `ModelMeshPass3D`처럼 로드-원스 |
| 핫리로드(아틀라스/타일셋) | 없음 |
| **그리드/타일맵 유틸리티** | 없음 — `Math2D.h`에 셀 좌표 헬퍼 자체가 없음(2D 게임 다수가 필요로 하는 범용 기반) |
| 세이브/직렬화 | 없음(엔진 전체 P2 항목, 2D에서도 동일하게 빔) |
| 게임패드(XInput) | 없음(장르 무관 공통 갭) |
| 프레임타임 프로파일러/인게임 콘솔 | 없음(FPS 카운터 텍스트만) |

---

## 3. 우선순위 (2D 엔진 완성 기준)

### P0 — 2D 렌더/UI 파이프라인의 구조적 공백 (다른 모든 2D 작업의 선행 조건)

| 항목 | 왜 먼저 | 문서 |
|---|---|---|
| **0. 3D 가드 누락 3개 TU 봉합** | `ENGINE_WITH_3D`를 꺼도 실제로는 2D 빌드가 안 됨(§4-A). 모든 2D 작업의 진짜 선행 조건 | §4-A |
| **1. `Scene2D` 분리 + 2D 월드 카메라(팬/줌)** | 지금 스크린 고정 좌표뿐 — 뷰포트보다 큰 2D 월드 자체가 불가능. 스냅샷에 월드 스프라이트 자리도 없음(§4-F) | 신규 `docs/camera-2d-design.md` |
| **2. 정렬·배칭 계약(y-sort + 아틀라스 레이어 규칙)** | 카메라가 생기는 즉시 y-sort가 필요하고, 현재 연속-런 배칭이 그 자리에서 무너짐(§4-D) | `texture-atlas-and-sprite-pass.md` §5에 절 추가 |
| **3. `ui::DrawList` + scissor 클리핑** | 설계 끝난 지 오래, 구현만 하면 UI 전체(ScrollList 포함)가 정상화 | `texture-atlas-and-sprite-pass.md` §3, §7 |
| **4. 2D 브로드페이즈(균일 그리드)** | 3D와 비대칭 상태 방치 중 — 엔티티 수 늘면 바로 병목. 결정성은 이미 안전(§4-G) | `collider-design.md` "브로드페이즈"에 2D 절 추가 |

**순서 의존**: 0 → 1 → 2 는 직렬(앞이 없으면 뒤가 성립 안 함). 3·4 는 1·2 와 파일이 겹치지 않아 병행 가능.

### P1 — 콘텐츠 제작에 바로 막히는 것

| 항목 | 문서 |
|---|---|
| 2D 프레임 애니메이션(`anim::a2d::SpriteAnimator`) 구현 | `animation-design.md` §2 (설계 끝) |
| `GlyphAtlas` + `AddText` 재작성 | `texture-atlas-and-sprite-pass.md` §1.3, §7-7 |
| 아틀라스 BC7/BC4 압축 | `atlas-build-pipeline.md` §5 |
| 트리거 enter/exit 이벤트 | `collider-design.md` 확장 |
| 2D 라이트맵 + 가짜 그림자 스프라이트 | `lighting.md`에 신규 "2D 라이트맵" 절 |
| `AssetRegistry` + 비동기 로더 | `loading-and-streaming.md`(설계 완료 → 구현) |
| 그리드/타일맵 유틸리티(`Math2D.h` 확장 또는 신규 `game/Grid2D.h`) | 신규 |

### P2 — 이후

| 항목 |
|---|
| 2D 파티클/VFX(화면공간 quad 파티클, 3D `ParticlePass3D`와 별개 설계) |
| 2D 포스트프로세싱(비네트/컬러그레이딩) |
| `VerticalStack`/`Measure`/`Arrange`, `Slider` 진짜 입력 캡처, keyboard focus |
| Live2D/Spine 조사(`animation-design.md` §4 — 아트 파이프라인 결정 이후) |
| 게임패드(XInput), 세이브/직렬화, 프로파일러/콘솔(엔진 공통 P1/P2와 중복 — `roadmap.md` 참고) |
| 스왑체인 `FLIP_DISCARD` |

---

## 4. 예측되는 설계 오류 (기존 시스템과 충돌하는 지점)

아래는 추상적 우려가 아니라 **현재 코드를 읽고 확인한 충돌 지점**이다. 각 항목은 "지금은 안 보이지만
2D 작업을 시작하는 순간 터지는" 것들이라, 해당 작업의 착수 조건으로 로드맵(§3)에 반영했다.

### A. `ENGINE_WITH_3D`를 꺼도 2D 빌드가 성립하지 않는다 ⚠ 가장 먼저

불변 규칙 7은 "3D는 매크로로 감싸 없으면 빌드에서 완전 제외"라고 하지만, 실제로는 **가드 없이
무조건 컴파일되는 3D 의존 TU가 3개** 남아 있다:

| 파일 | `ENGINE_WITH_3D` 가드 | 의존 |
|---|---|---|
| `src/anim/AnimationSampler.{h,cpp}` | 없음 | `math/Math3D.h` 직접 include |
| `src/import/ModelImporter.cpp` | 없음 | `Mat4`/`Vec3` + ufbx |
| `src/import/CreaseLines.cpp` | 없음 | `Mat4`/`Vec3` |

셋 다 `vcxproj`에 무조건 `ClCompile`로 등록돼 있다. 지금은 `Math3D.h`가 존재하니 조용히 컴파일되지만,
**3D 배제/삭제를 실제로 수행하는 순간 빌드가 깨진다.** 또 `animation-design.md`가 예고한
`anim/a2d` 신설 시, `anim/`이 사실상 3D 전용인데 이름은 중립이라 2D 코드가 무심코 딸려오기 쉽다.

- **조치**: 2D 작업 착수 전에 이 3개 TU를 `#if defined(ENGINE_WITH_3D)`로 감싸고(`.cpp` 본문 비우기 =
  기존 `physics/p3d` 방식과 동일), `ENGINE_WITH_3D` 없이 한 번 빌드해 경고/오류 0을 확인한다. 이게
  §3 P0-0.
- 동시에 `anim/AnimationSampler.*` → `anim/a3d/`로 이동(`animation-design.md` §0이 "2D가 생길 때
  별도 커밋으로"라고 미뤄둔 바로 그 시점이 여기).

### B. 2D 패스는 포스트프로세스 **뒤**에서 백버퍼에 직접 그린다 — 2D 라이트맵이 얹힐 자리가 없다

현재 패스 순서와 렌더 타깃:

```
MeshPass3D        → m_sceneColorRtv (MSAA 씬 타깃, MRT 컬러+노멀)
PostProcessPass   → SSAO/안개 합성 + 리졸브, 끝에 OMSetRenderTargets(백버퍼)
QuadPass2D        → (백버퍼, PostProcess가 남긴 바인드 상태)
SpritePass2D      → (백버퍼)
```

즉 **2D는 MSAA 씬 타깃도, 포스트프로세싱도 거치지 않는다.** 그래서 §2.1의 "2D 라이트맵을
월드에만 곱하고 UI는 제외"가 지금 구조에서는 표현 불가능하다 — 월드와 UI가 같은 백버퍼에
순서대로 쌓일 뿐 레이어 구분이 없다.

- **조치**: 2D 라이트맵(P1)을 설계하기 전에 **패스 순서·타깃 계약**부터 정한다. 필요한 형태는 3단
  분리 — ① 2D 월드 스프라이트를 씬 타깃에 → ② 라이트맵 곱 + 합성 → ③ UI 스프라이트를 백버퍼에.
  `RenderPass.h`의 `renderTarget`/`backBufferRenderTarget` 두 필드가 이미 그 구분을 갖고 있으므로
  새 필드보다 **패스가 어느 쪽에 그리는지 명시하는 규약**을 문서화하는 쪽이 맞다.
- **하지 말 것**: `PostProcessPass`에 2D 분기를 추가하는 것(그 클래스는 이미 `#if` 절반이 3D — 거기에
  2D 조명까지 넣으면 SRP가 완전히 무너진다). 2D 합성은 별도 패스로.

### C. `SpritePass2D`가 아틀라스 1장을 하드코딩하고 있다 (OCP 위반 예약)

```cpp
ID3D11ShaderResourceView* SpritePass2D::SrvFor(std::uint32_t atlasId) const
{
    if (atlasId == kUiAtlasId && m_atlasSrv != nullptr) return m_atlasSrv;   // 그리고 0 = 1x1 white
}
```

생성자가 `.dds` 경로 문자열 하나를 받아 `Initialize`에서 로드-원스 한다. **두 번째 아틀라스(월드
타일셋, 캐릭터)가 필요해지는 순간** 이 함수와 생성자에 경로/SRV가 하나씩 늘어나는 형태로 번진다 —
"렌더러가 특정 게임 개념을 하드코딩하지 않는다"(OCP)를 정면으로 어기는 방향.

- **조치**: 아틀라스가 2장째가 되는 시점 = `AssetRegistry`(P1)를 더 미룰 수 없는 시점으로 못박는다.
  `atlasId`는 레지스트리 핸들이어야 하고, 패스는 `id → SRV` 조회만 한다.
- **부수 위험**: 스냅샷은 값만 나르므로 `atlasId`는 핸들 숫자다. 렌더러는 최신 스냅샷 1개만 보관하고
  **오래된 프레임을 버리므로**, 레지스트리가 LRU 축출/핫리로드로 SRV를 해제하면 인플라이트 스냅샷의
  `atlasId`가 죽은 핸들을 가리킬 수 있다. 레지스트리 설계 시 "렌더 스레드가 마지막으로 소비한
  프레임 이후에만 해제" 규칙을 명시할 것.

### D. y-sort를 넣는 순간 배칭이 무너진다 (드로우 콜 폭증)

`SpritePass2D::Execute`는 `(atlasId, clip)`가 **연속으로 같은 구간**만 한 `Draw`로 묶는다(painter
순서 보존을 위해 의도된 설계). 그런데 2D 월드는 **y-sort(뒤→앞)가 기본**이고, y로 정렬하면
타일·캐릭터·오브젝트 아틀라스가 교차한다 → 런 길이가 1로 수렴 → **스프라이트 1개당 드로우 1회.**

이건 "규모가 커지면 나중에"가 아니라 **타일맵 + 캐릭터를 처음 같이 그리는 순간** 발생한다.

- **조치(§3 P0-2)**: 정렬 키를 `(레이어, y, atlasId)`로 두고, **한 레이어는 아틀라스 1장** 규칙을
  아틀라스 그룹 설계(`atlas-build-pipeline.md`의 그룹 개념)에 못박는다. 그러면 레이어 안에서 y로
  아무리 섞여도 `atlasId`가 불변이라 런이 유지된다.
- 대안(정렬을 못 고정할 때): 아틀라스를 텍스처 배열로 올리고 인스턴스마다 슬라이스 인덱스를 주는
  방식. 셰이더·패커 변경이 따르므로 P0에서는 채택하지 않는다.

### E. 카메라 줌 + 현재 샘플러/밉 설정 = 픽셀아트 뭉개짐 + gutter 초과 누출

`SpritePass2D`의 샘플러는 `D3D11_FILTER_MIN_MAG_MIP_LINEAR` 고정이고, `atlas_pack`은 박스 필터 밉을
굽는다. 카메라 줌(비정수 배율)이 들어가면 ① 픽셀아트 경계가 흐려지고 ② 축소 시 밉 레벨이
`atlas_pack`의 gutter 예산(`2^mips` 텍셀)을 넘어가면 이웃 스프라이트가 새어 나온다(fringe).

- **조치**: 카메라 설계(P0-1)에 **정수 배율 줌 스냅**(또는 픽셀 퍼펙트 스냅) 규칙을 포함하고,
  샘플러를 아틀라스 그룹 속성(POINT/LINEAR)으로 뺀다. `.atlas` 매니페스트에 필터 힌트 1필드 추가가
  가장 싸다.
- `atlas-build-pipeline.md`의 "셀 16px 밉 컷"(미구현으로 남아 있는 항목)이 이 문제의 다른 절반이다.

### F. 스냅샷에 월드 스프라이트/카메라가 들어갈 자리가 없다

`RenderSnapshot`은 `worldQuads`(레거시 데모용 단색) · `uiQuads` · `uiSprites` 셋뿐이고,
`sprite2d.hlsl`의 상수 버퍼는 `cbuffer Screen { float2 screen; }` — **카메라 개념이 아예 없다.**
여기에 카메라 행렬을 얹으면 같은 셰이더를 쓰는 **UI까지 같이 스크롤·줌된다.**

- **조치**: 카메라 작업의 첫 단계는 셰이더가 아니라 **스냅샷 분해** — `Scene2D { camera,
  worldSprites }`를 신설하고 `uiSprites`는 스크린 고정으로 남긴다(3D가 `Scene3D`를 갖는 것과 대칭,
  불변 규칙 7·3에 부합). 월드/UI는 같은 패스가 그리되 상수 버퍼를 두 번 갱신하거나 패스를 둘로
  나눈다 — B의 타깃 분리와 같이 결정한다.
- `worldQuads`(파티클 데모 잔재)를 이 시점에 정리할지도 같이 판단. 남겨두면 "월드 2D 경로가 둘"이
  되어 혼선.

### G. 안전 확인됨 — 과설계하지 말 것

- **2D 브로드페이즈는 결정성을 깨지 않는다.** `CollisionWorld2D::Step()`이 마지막에 contacts를
  `(a, b)` id 기준으로 `std::sort` 하므로, 순회 순서가 그리드로 바뀌어도 결과 배열은 동일하다.
  그리드 도입 시 **이 정렬을 지우지만 않으면** 된다(3D처럼 Debug 브루트포스 대조를 붙이면 충분).
- UI 입력 라우팅(`PointerXxx` → `bool` 소비)은 2D 월드가 생겨도 그대로 쓸 수 있다. 월드 클릭은
  "UI가 소비하지 않은 좌클릭"만 받으면 되고, 그 경로는 이미 `Application`에 있다.

---

## 5. 판단 필요

1. **2D 카메라 = `Scene2D` 신설로 확정할지** — §4-F 기준으로는 신설이 사실상 유일한 답(카메라를 공용 `Screen` cbuffer에 얹으면 UI까지 스크롤됨). 남은 선택은 월드/UI를 **한 패스가 cbuffer 두 번 갱신** vs **패스 둘로 분리** — §4-B의 타깃 분리와 같이 결정해야 한다.
2. **`worldQuads`(파티클 데모 잔재)를 카메라 작업 시 정리할지** — 남기면 월드 2D 경로가 둘이 된다(§4-F).
3. **2D 브로드페이즈를 3D와 같은 그리드로 맞출지** — 3D `CollisionWorld3D`의 적응형 셀 로직을 그대로 이식할지, 2D 전용(더 단순한 고정 셀)으로 갈지. 결정성은 어느 쪽이든 안전(§4-G).
4. **2D 라이트맵을 3D 조명과 별도 셰이더/패스로 완전히 분리할지** — 불변 규칙 7 그대로면 답은 "분리"이고, §4-B도 `PostProcessPass` 재사용을 반대한다. 남은 결정은 `common2d.hlsli` 공유 헤더를 둘지.
5. **아틀라스 필터(POINT/LINEAR)를 `.atlas` 매니페스트 필드로 뺄지** — 픽셀아트를 쓸 것인지에 달렸고, 그게 정수 배율 줌 스냅 규칙까지 결정한다(§4-E).

---

## 6. 사용 방법 (How to use)

- **다음 작업 고르기**: §3 P0부터. **P0-0(3D 가드 봉합)이 모든 것의 선행 조건**이고, 그 뒤 1→2는 직렬, 3·4는 병행 가능(§3 하단 "순서 의존").
- **착수 전 확인**: 해당 항목이 §4에 충돌 지점으로 올라와 있는지 먼저 본다. §4는 "이 작업을 그냥 시작하면 무엇이 깨지는가"를 코드 근거와 함께 적은 절이라, 설계 문서를 쓰기 전에 읽어야 의미가 있다.
- **항목 착수**: 표 항목을 해당 시스템 문서(`texture-atlas-and-sprite-pass.md`, `collider-design.md`, `animation-design.md` 등)에 상세 설계로 옮기고 "사용 방법"까지 채운 뒤, 여기 표 행은 링크로 축약. 카메라처럼 기존 문서가 없는 항목은 새 `docs/camera-2d-design.md` 등을 만든다.
- **`docs/roadmap.md`/`docs/synopsis.md`와의 관계**: 저 두 문서는 3D 디펜스 장르가 확정된 채로 있다. 이 문서는 그와 독립적으로 "2D 축만 완성하면 무엇이 남는가"를 추적한다 — 장르가 실제로 2D로 바뀌면 그때 `roadmap.md` §3을 이 문서 기준으로 재정렬한다.
- **하지 말 것**: 이 문서에 특정 게임(농사/디펜스 등)의 콘텐츠 항목(작물, 웨이브, 무기 등)을 올리지 않는다 — 그건 장르가 정해진 뒤 별도 `synopsis.md`/`*-design.md`의 몫이다. 여긴 순수 엔진 기반 시스템만.
