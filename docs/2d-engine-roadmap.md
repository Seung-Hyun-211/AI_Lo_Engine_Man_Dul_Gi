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
| **2D 월드 카메라(팬/줌, world→screen 변환)** | 지금 스크린 고정 좌표뿐 — 뷰포트보다 큰 2D 월드 자체가 불가능. 가장 근본적인 공백 | 신규 설계 필요(`render/r2d` + `Scene2D` 값 타입) |
| **`ui::DrawList` + scissor 클리핑** | 설계 끝난 지 오래, 구현만 하면 UI 전체(ScrollList 포함)가 정상화 | `texture-atlas-and-sprite-pass.md` §3, §7 |
| **2D 브로드페이즈(균일 그리드)** | 3D와 비대칭 상태 방치 중 — 엔티티 수 늘면 바로 병목 | `collider-design.md` "브로드페이즈"에 2D 절 추가 |

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

## 4. 판단 필요

1. **2D 카메라 설계 방향** — `Scene3D`처럼 값 타입 `Scene2D{ camera, spriteDraws, ... }`를 스냅샷에 새로 둘지, 기존 `uiSprites`/`worldQuads`를 그대로 두고 카메라만 별도 변환 행렬로 추가할지. UI(스크린 고정)와 월드(카메라 종속) 스프라이트를 같은 `SpritePass2D`가 그리는 이상 이 둘을 구분하는 플래그가 필요.
2. **2D 브로드페이즈를 3D와 같은 그리드로 맞출지** — 3D `CollisionWorld3D`의 적응형 셀 로직을 그대로 이식할지, 2D 전용(더 단순한 고정 셀)으로 갈지.
3. **2D 라이트맵을 3D 조명과 별도 셰이더/패스로 완전히 분리할지** — 불변 규칙 7(2D/3D 모듈 분리) 그대로 적용하면 답은 "분리"이지만, `common3d.hlsli`처럼 `common2d.hlsli` 공유 셰이더 헤더를 새로 둘지 결정 필요.

---

## 5. 사용 방법 (How to use)

- **다음 작업 고르기**: §3 P0부터. P0 3개는 서로 독립적이라 순서 무관하게 병행 가능(카메라는 렌더, 클리핑은 UI, 브로드페이즈는 물리 — 파일이 겹치지 않음).
- **항목 착수**: 표 항목을 해당 시스템 문서(`texture-atlas-and-sprite-pass.md`, `collider-design.md`, `animation-design.md` 등)에 상세 설계로 옮기고 "사용 방법"까지 채운 뒤, 여기 표 행은 링크로 축약. 카메라처럼 기존 문서가 없는 항목은 새 `docs/camera-2d-design.md` 등을 만든다.
- **`docs/roadmap.md`/`docs/synopsis.md`와의 관계**: 저 두 문서는 3D 디펜스 장르가 확정된 채로 있다. 이 문서는 그와 독립적으로 "2D 축만 완성하면 무엇이 남는가"를 추적한다 — 장르가 실제로 2D로 바뀌면 그때 `roadmap.md` §3을 이 문서 기준으로 재정렬한다.
- **하지 말 것**: 이 문서에 특정 게임(농사/디펜스 등)의 콘텐츠 항목(작물, 웨이브, 무기 등)을 올리지 않는다 — 그건 장르가 정해진 뒤 별도 `synopsis.md`/`*-design.md`의 몫이다. 여긴 순수 엔진 기반 시스템만.
