# 엔진 기본 요소 (Engine Conventions)

엔진 전체가 전제하는 **축·단위·포맷·구조의 한 곳**. 각 시스템의 상세 계약은 개별 문서가
가지고, 이 문서는 그 문서들이 공유하는 불변값을 모아 못박는다. 새 코드는 여기 적힌 것을
어기지 않는다.

---

## 1. 좌표계 · 핸디드니스

### 3D (`math/Math3D.h`, `render/r3d`, `physics/p3d`)

| | 값 |
|---|---|
| 핸디드니스 | **왼손 (left-handed)** |
| 축 | **+X 오른쪽, +Y 위, +Z 화면 안쪽(forward)** |
| 행렬 저장 | **row-major** (`Mat4::m[16]`, `m[12..14]` = translation) |
| 벡터 관례 | **row-vector**: `v' = v * M`. 합성은 **자식 먼저** — `world = local * parent` |
| 회전 표현 | 쿼터니언(`math::Quat`), `QuatAxisAngle(axis, radians)`, `a * b` = "a 다음 b" |
| 투영/뷰 | `LookAtLH` / `PerspectiveFovLH` / `OrthographicLH`. FOV `π/3` (60°) |
| 모델 정면 | 로컬 **+Z** (Unity-chan). 뒤로 걸으면 `SnapshotBuilder::kModelYawOffset` 를 `π` 로 |
| FBX 임포트 축 | `ufbx_axes_left_handed_y_up` (`ModelImporter`) — 임포터가 엔진 축으로 변환 |
| 면 winding | **아직 미확정** — `MeshPass3D`/`ModelMeshPass3D` 는 `D3D11_CULL_NONE`. back-cull 은 winding 검증 후 (열린 항목 §10) |

### 2D (`math/Math2D.h`, `render/r2d`, `ui`)

| | 값 |
|---|---|
| 공간 | **픽셀**, 원점 **좌상단**, **+Y 아래** (Win32 클라이언트 + `quad2d.hlsl`/`sprite2d.hlsl`) |
| 색 | `math::Color` = **linear RGBA 0..1, straight(비-premultiplied) alpha** |

---

## 2. 단위

| 양 | 3D | 2D | 비고 |
|---|---|---|---|
| 길이 | **미터 (m)** | **픽셀 (px)** | 3D 임포트 스케일 `ImportOptions::scale` 로 파일 단위→m (Unity-chan cm → `0.01`) |
| 시간 | **초 (s)** | 초 | `std::chrono` 직접 사용 금지 — `core::FrameClock`/`FixedTimestep` 만 (§3) |
| 각도 | **라디안 (rad)** | rad | 모든 yaw/pitch/turn-rate. `kCharTurnRate = 12 rad/s`, `kMouseSensitivity = 0.0022 rad/px` |
| 속도 | m/s | px/s | `kCharWalkSpeed 2.2`, `kCharRunSpeed 5.2` (m/s) · `kPlayerSpeed 300` (px/s, 레거시 2D) |
| 가속도 | m/s² | — | **중력 `kCharGravity = 14` — 실제 9.8 이 아니라 게임 튜닝값.** `kCharJumpSpeed 4.6 m/s` |
| 질량 · 힘 · 임펄스 | **없음** | — | 강체 동역학 미구현. 캐릭터는 kinematic (속도 직접 적분) |
| 지면 | `y = 0` 평면 | — | 캐릭터 발이 `pos.y`. 데모 이동 범위 `±kCharHalfRange 7.5 m` 클램프 |

게임플레이 튜닝 상수는 `game::Simulation` 의 `static constexpr k*` 에 모여 있다 — 숫자만 바꾼다.

---

## 3. 시간 · 고정 스텝 (`docs/time-design.md`)

| | 값 |
|---|---|
| 시뮬 스텝 | **고정 1/60 s (60 Hz)**. 런타임에 스텝 크기 변경 금지(결정성) |
| 프레임 델타 clamp | `FrameClock::kMaxDelta = 0.1 s` (브레이크포인트·스톨 방지) |
| 프레임당 최대 스텝 | `FixedTimestep::kMaxStepsPerFrame = 5` (초과분 폐기, spiral-of-death 방지) |
| 전역 배율 | `Application::m_globalTimeScale` → `Advance(delta * scale)`. `0` = 일시정지, `0.5` 슬로모, `2` 배속 |
| 개체별 배율 | `Simulation::Actor::timeScale` (로컬), `>1` 은 서브스텝(`kMaxActorSubSteps = 8`). `ignoreGlobalPause` |
| 경과 시간 | 시스템이 `m_elapsed += fixedDelta` 누적 + getter (예: `Simulation::ElapsedTime()`) |
| 규칙 | 시뮬·물리·애니메이션은 전달받은 `fixedDelta` 만 쓴다. 카메라 마우스룩만 프레임당 1회(스텝 밖) 예외 |

---

## 4. 텍스처 방식 (`docs/image-assets.md`, `docs/atlas-build-pipeline.md`, `docs/texture-atlas-and-sprite-pass.md`)

| | 값 |
|---|---|
| 백엔드 | **DirectX 11, Windows 데스크톱 전용.** 모바일·타 백엔드 미고려 → 크로스플랫폼 텍스처 포맷 분기 없음 |
| 디코드 진입점 | **`import::LoadImageFromFile(path)` → `import::ImageData`** (RGBA8, top-down, straight alpha). `.tga` 자체 디코더 / `.png .jpg .bmp .gif` stb_image. 유일 seam |
| 색공간 | 파일 바이트 그대로 보관. **SRV 포맷** 으로 감마 결정: albedo/UI = `..._UNORM_SRGB` (BC7 = `BC7_UNORM_SRGB`), normal/mask/packed = `..._UNORM` (linear) |
| 알파 | **straight(비-premultiplied).** 블렌드 = `SrcAlpha / InvSrcAlpha`. 컷아웃은 셰이더 `clip(a - 0.35)` (헤어/속눈썹) |
| 전역 sRGB 파이프라인 | **미구현** (sRGB 백버퍼 없음). 그전까지 SRV 포맷이 유일한 sRGB 레버 |
| 아틀라스 파일 | **`.dds` (DX10 헤더) 고정.** 페이지 크기 고정 집합 `{1024, 2048, 4096}` (기본 4096) |
| 아틀라스 GPU 포맷 | **BC7 `*_SRGB`(컬러) / BC4 linear(단일채널) 로 고정** — 현재 `atlas_pack` v1 은 무압축 `_SRGB` 디버그본, BC7 압축은 다음 |
| 아틀라스 번들 | 그룹(`ui` / `char/<name>` / `obj/<cat>`)별 증분 빌드 — 매번 전부 안 만듦. gutter = `2^mips` 텍셀 + edge-extend |
| 샘플러 | UI 스프라이트: `MIN_MAG_MIP_LINEAR`, **CLAMP**. 모델 디퓨즈: LINEAR, WRAP |
| dev / ship | dev 는 loose 이미지·`.tga` 로드 편의, **배포 런타임은 `.dds`(+`.atlas`) 만** |

---

## 5. LOD

| 종류 | 현재 | 로드맵 |
|---|---|---|
| 텍스처 밉 | 모델 디퓨즈: 밉 없음(`MipLevels = 1`). 아틀라스: `atlas_pack` 가 box 필터 밉 생성(그룹별 레벨, 셀 16px 에서 컷) | `MISC_GENERATE_MIPS` 로 모델 텍스처 밉 |
| 텍스처 LOD 조정 | 샘플러 `MaxLOD` / mip LOD bias, 아틀라스 품질 티어(1× / 0.5× / 0.25× 스케일 페이지, `game-settings.md`) | 런타임 부분 밉 스트리밍 |
| 메시 LOD | **없음** — 디스턴스 기반 메시 스왑/임포스터 미구현 | 로드맵 |
| 그림자 | 방향광 **단일 캐스케이드** 셰도우맵 2048² + PCF (`docs/shadows.md`) | CSM / 카메라 추종 |
| 파티clesde | 앞 2,048개만 그림(`SnapshotBuilder::kVisibleParticleSample`), 나머지는 JobSystem 벤치 스텁 | — |

---

## 6. 애니메이션 구조 (`docs/model-animation-research.md`, `docs/animation-design.md`)

### 3D 스켈레탈

| 요소 | 형태 |
|---|---|
| 스켈레톤 | `import::Skeleton` — 본이 **부모 인덱스 < 자식 인덱스** 로 위상 정렬. 본마다 `inverseBind`(Mat4) + 로컬 bind TRS |
| 클립 | `import::AnimationClip` — 임포터가 **고정 샘플레이트(기본 30 fps)** 로 구운 키프레임. 본별 `BoneTrack` |
| 리타깃 | `import::LoadAnimationClipsFromFile` — 별도 bones-only FBX 의 트랙을 **본 이름 매칭** 으로 대상 스켈레톤에 |
| 포즈 평가 | `anim::AnimationSampler::Evaluate(skeleton, clip, t)` → **모델 공간 스킨 행렬 팔레트** (`inverseBind * boneModel`) |
| 스키닝 | **CPU LBS** — `ModelMeshPass3D` 가 매 프레임 정점 재계산 → DYNAMIC VB `Map/Unmap`. 셰이더엔 스킨 코드 없음. 최대 4본/정점(`kMaxBoneInfluences`) |
| 강체 부착 | 스킨 웨이트 없이 한 본에 매달린 서브메시(Unity-chan 얼굴/눈/입) = `ModelMesh::attachBone` → `SubMesh::rigidBone`, 그 본 하나로 전체 변환 |
| 재생 상태 | `game::CharacterAnimationState` — `enum Locomotion{Wait,Walk,Run,Jump}` → 클립 인덱스 스냅. `Simulation::StepOneActor` 가 게임플레이 신호(접지·속도·Shift)로 구동 |
| 스레드 경계 | 스냅샷엔 **`(clipIndex, clipTime)` 만** 건너감. 팔레트·정점 계산은 렌더 스레드 |
| 시간 | 애니메이션 전진은 **고정 스텝 안** (`CharacterAnimationState::Update(dt, loco)`). 개체별 `timeScale` 적용됨 |

**아직 없음**: 크로스페이드/블렌딩(전환 즉시 스냅), 루트 모션 분리(클립에 이동이 구워져 미끄러짐), GPU 스키닝(여러 인스턴스를 각자 다른 애니로 세우려면 필요 — 설계만).

### 2D 프레임 애니메이션

`anim::a2d::SpriteAnimationClip` / `SpriteAnimator` — uv rect 배열을 시간에 따라 스텝. **설계만**(`animation-design.md` §2), 텍스처 `SpriteDraw`(#3, 부분 구현) 위에 얹힐 예정.

---

## 7. 렌더/스레드 불변식 (`CLAUDE.md` 규칙 1~8, `docs/multithreaded_game_engine_architecture.md`)

- **D3D11 호출은 렌더 스레드에서만.** 메인 스레드 = 입력·시뮬·스냅샷 생성.
- 스레드 경계는 **값 기반 `RenderSnapshot` 하나** (`Quad`·`SpriteDraw`·`MeshDraw`·`ModelDraw`·`CameraView`·`Mat4` 전부 값). 가변 게임 객체 포인터 금지.
- 렌더러는 **최신 스냅샷 1개** (1슬롯 메일박스). 오래된 미렌더 프레임 폐기.
- 그리는 일은 전부 `IRenderPass` 목록. 렌더러 코어는 clear·bind·pass 순회 + `ShaderLibrary` 소유만.
- 셰이더는 `assets/shaders/<name>.hlsl` + `ShaderLibrary::Get`. 인라인 `D3DCompile` 금지. `.hlsl`/`.hlsli` = 커밋 소스, 실행 중 편집 시 핫리로드.
- 패스는 **멀티샘플 씬 타깃** (최대 8x) 에 그리고 프레임 끝에 백버퍼로 resolve (`docs/msaa.md`).
- `JobSystem::ParallelFor` 는 겹치지 않는 연속 `[begin,end)` 범위만. 워커 안 `vector` 재할당·엔티티 생성/파괴·공유 카운터 증가 금지.

---

## 8. 충돌 (`docs/collider-design.md`)

- **탐지만.** `CollisionWorld*::Step()`/`OverlapsAny()` 는 콜라이더를 안 움직인다. 응답(밀어내기·블로킹)은 게임 코드.
- 2D: Box/Circle. 3D: Box/Sphere. `layer` / `mask` 비트. `LayersInteract(a,b) = (maskA & layerB) && (maskB & layerA)`.
- `Collider3D::isTrigger` — solid 아님, `Contact` 로는 보고됨.
- `CollisionWorld` 는 **메인/시뮬 스레드만**.

---

## 9. 모듈 분리 (`CLAUDE.md` 규칙 7)

- **2D/3D 는 별도 모듈.** `math/Math2D↔Math3D`, `render/r2d↔r3d`, `physics/p2d↔p3d` 서로 `#include` 금지. 공유는 각 계층 core.
- 3D 는 `ENGINE_WITH_3D` 로 감싸 없으면 빌드 완전 제외(`.cpp` 본문 `#if`). 2D 는 baseline(`ENGINE_WITH_2D`, UI 의존).
- 서드파티는 `src/vendor/` 소스 vendor, 벤더 타입은 그걸 쓰는 `.cpp` 안에만 (`ufbx`, `stb`).

---

## 10. 열린 항목 (아직 미확정 — 정하기 전엔 보수적으로)

| 항목 | 현재 | 정할 때 |
|---|---|---|
| 면 winding / back-face cull | `CULL_NONE` (winding 미검증) | 실제 모델로 검증 → `CULL_BACK` + winding 규칙 명문화 |
| 전역 sRGB 렌더 파이프라인 | 없음 (SRV 포맷만) | sRGB 백버퍼 + linear 중간 타깃 (`docs/lighting.md`) |
| 메시 LOD | 없음 | 디스턴스 스왑 / 임포스터 정책 |
| GPU 스키닝 | CPU LBS 만 | `SkinnedMeshPass3D` + `StructuredBuffer` 본 팔레트 (`model-animation-research.md` §5.2) |
| 강체 동역학 (질량·힘) | 없음 (kinematic) | 필요한 게임 장르 확정 후(`docs/synopsis.md`) |
| 애니메이션 크로스페이드·루트 모션 | 없음 | `docs/animation-design.md` §5 |

---

## 11. 지키는 법 (How to apply)

- **새 3D 시스템**: 위치·거리는 미터, 각도는 라디안, 좌표는 LH Y-up +Z-forward, 행렬은 row-vector(`v*M`, 자식 먼저). 시간은 `Step(float fixedDelta, ...)` 시그니처, 내부에서 `chrono` 금지.
- **새 튜닝 상수**: 하드코딩 말고 `game::Simulation` 의 `static constexpr k*` 옆에 (또는 그 시스템의 상수 블록).
- **새 텍스처를 로드**: `import::LoadImageFromFile` 만. SRV 포맷은 §4 색공간 규칙대로. 배포 대상이면 아틀라스 그룹에 넣어 `.dds` 로.
- **새 애니메이션 소비자**: `(clipIndex, clipTime)` 만 스냅샷으로 넘기고, 팔레트/정점 계산은 렌더 쪽에서. 전진은 고정 스텝 안에서.
- **새 렌더 프리미티브**: `RenderSnapshot` 에 값 타입 추가 + 그걸 소비하는 `IRenderPass`. 렌더러 코어에 게임 개념 하드코딩 금지.

### 하지 말 것

- 중력에 `9.8` 을 넣지 않는다 — 게임 튜닝값(`kCharGravity`)이 기준. 사실적 중력이 필요하면 별도 상수로 명시.
- 오른손 좌표계 · 열벡터(`M*v`) · column-major 로 새 3D 수학을 쓰지 않는다.
- premultiplied alpha 를 도입하지 않는다 (`stbi_set_unpremultiply_on_load` 포함).
- 아틀라스를 런타임에 패킹하거나 `.png`/`.dds` 를 섞어 쓰지 않는다 — 배포는 `.dds` 만.
- 크로스플랫폼(모바일/ES/Metal) 텍스처 포맷 분기를 다시 들이지 않는다 — DX11 데스크톱 전용.
- `Step()` 밖에서 시뮬/물리/애니메이션 시간을 전진시키지 않는다 (카메라 마우스룩만 예외).
- winding 을 가정하고 `CULL_BACK` 을 켜지 않는다 (열린 항목 §10 해결 전까지).
