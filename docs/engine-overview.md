# 엔진 개요 (Engine Overview)

이 문서는 프로젝트 루트의 지도다. 특정 게임을 담고 있지 않은 **2D 엔진 뼈대**의 모듈 경계, 소유권, 프레임 흐름, 의존성 방향, 확장 지점을 정리한다. 스레드 계약의 세부는 [multithreaded_game_engine_architecture.md](multithreaded_game_engine_architecture.md), UI 계층의 세부는 [ui-architecture.md](ui-architecture.md)에 있다. 명령 단위 작업 절차는 [command-playbook.md](command-playbook.md)에 있다.

## 한눈에 보기

```text
wWinMain (src/main.cpp)
  └─ Dx11Renderer 를 만들어 IRenderer 로서 Application 에 주입
       Application (game/Application)           프레임 지휘자 · 조립만
        ├─ Win32Window       (platform/)        OS 창 + 메시지  → IWindowEventSink
        ├─ InputState        (input/)           이번 프레임 키/마우스
        ├─ UIContext         (ui/)              화면 오버레이 (월드 위)
        ├─ Simulation        (game/)            가변 월드, 고정 timestep
        │    ├─ JobSystem    (core/)            연속 범위 병렬 update
        │    ├─ CollisionWorld2D (physics/p2d)  2D 겹침 탐지 (탐지만)
        │    └─ CollisionWorld3D (physics/p3d)  3D 겹침 탐지  [ENGINE_WITH_3D]
        ├─ SnapshotBuilder   (game/)            월드+UI → 값 기반 RenderSnapshot
        └─ IRenderer         (render/)          추상 렌더러 (구현: Dx11Renderer)
                                                코어는 device/swapchain/RT/depth만 소유
                                                그리기 = IRenderPass 목록
                                                  MeshPass3D  (render/r3d)  [ENGINE_WITH_3D]
                                                  QuadPass2D  (render/r2d)
```

## 2D / 3D 모듈 분리 · 빌드 토글

같은 빌드 안이지만 2D와 3D는 **서로 include하지 않는 별도 모듈**이다. 공유는 각 계층의 core로만.

| 계층 | core (공유) | 2D 모듈 | 3D 모듈 |
|---|---|---|---|
| math | — | `math/Math2D.h` (Vec2/Rect/Color) | `math/Math3D.h` (Vec3/Vec4/Mat4) |
| render | `IRenderer.h`, `RenderPass.h`, `Dx11Renderer.*`, `RenderSnapshot.h` | `render/r2d/` (Sprite2D, QuadPass2D) | `render/r3d/` (Scene3D, MeshPass3D) |
| physics | `physics/Collision.h` | `physics/p2d/` (Collider2D, CollisionWorld2D) | `physics/p3d/` (Collider3D, CollisionWorld3D) |

- `math/Math.h`는 umbrella: 2D는 항상, 3D는 `ENGINE_WITH_3D`일 때만 pull.
- **`ENGINE_WITH_3D` 미정의 시**: 3D `.cpp` 본문이 `#if`로 비워짐 → `MeshPass3D`·`CollisionWorld3D` 심볼 없음, `RenderSnapshot`에 `scene3d` 없음, `Dx11Renderer`가 3D 패스 미등록, `SnapshotBuilder`/`Simulation`이 3D 스킵. 2D 전용 exe가 경고 0으로 빌드된다(검증됨).
- `ENGINE_WITH_2D`는 baseline (UI가 의존). 프로젝트 정의는 `CppWindowGame.vcxproj`의 `PreprocessorDefinitions`.
- 새 차원 모듈(예: 사운드 2D/3D)도 같은 패턴: `<layer>/core` + `<layer>/x2d` + `<layer>/x3d` + 토글.

## 모듈과 책임 (SRP)

| 모듈 | 파일 | 유일한 변경 이유 |
|---|---|---|
| `engine::math` | `math/Math.h`(umbrella), `Math2D.h`, `Math3D.h` | 공용 기하 타입이 바뀔 때 |
| `engine::core` | `core/JobSystem.*`, `core/Time.h`, `core/NonCopyable.h`, `core/AssetPaths.*` | 작업 스케줄링·시간·자산 경로 규칙이 바뀔 때 ([time-design.md](time-design.md)) |
| `engine::platform` | `platform/Win32Window.*` | OS 창/메시지 처리 방식이 바뀔 때 |
| `engine::input` | `input/InputState.h` | 입력 상태 표현·에지 판정이 바뀔 때 |
| `engine::render` | `render/IRenderer.h`, `RenderPass.h`, `RenderSnapshot.h`, `Dx11Renderer.*` + `render/shader/*` + `render/r2d/*` + `render/r3d/*` | 렌더 백엔드/스냅샷 포맷/파이프라인 스테이지/셰이더 로딩이 바뀔 때 ([shader-pipeline.md](shader-pipeline.md)) |
| `engine::physics` | `physics/Collision.h` + `physics/p2d/*` + `physics/p3d/*` | 충돌 탐지 규칙이 바뀔 때 ([collider-design.md](collider-design.md)) |
| `engine::import` | `import/Model.h`, `import/ModelImporter.*`, `import/TgaImage.*`, `import/CreaseLines.*` (+ `vendor/ufbx`) | FBX/TGA → 엔진 데이터 매핑이 바뀔 때 ([model-animation-research.md](model-animation-research.md)) |
| `engine::anim` | `anim/AnimationSampler.*` | 포즈 평가·블렌딩 규칙이 바뀔 때 |
| `engine::ui` | `ui/UI.*` | 위젯 트리·오버레이 규칙이 바뀔 때 |
| `engine::game` | `game/Simulation.*`, `game/SnapshotBuilder.*`, `game/Application.*` | 프레임 흐름·월드 규칙이 바뀔 때 |

`Application`은 이들을 **소유·연결·순서 지정**만 한다. 실제 일은 각 모듈이 한다. 예전 `Game` god class(창 생성·메시지·입력·시뮬·스냅샷·resize를 모두 보유)를 이 표대로 분해한 결과다.

## 의존성 방향 (DIP)

```text
game  ─▶ render(IRenderer, RenderSnapshot)   ← Dx11Renderer 는 구현일 뿐
game  ─▶ ui  ─▶ render(Quad)                 UI 는 Quad 방출에만 의존, D3D 모름
game  ─▶ input, platform, core, math
render(Dx11) ─▶ core(NonCopyable), D3D11     상위 레이어를 도로 참조하지 않음
```

- `main.cpp`만 `Dx11Renderer` 구상 타입을 안다. 나머지는 `render::IRenderer`(`Start/SetFrameSettings/Submit/Resize/Stop`)에만 의존한다. DX12 렌더러는 `IRenderer`를 구현해 `main.cpp` 한 줄만 바꾸면 교체된다.
- `platform::Win32Window`는 게임/UI 타입을 모른다. `IWindowEventSink` 추상으로 이벤트를 되돌려준다. `Application`이 그 sink를 구현한다.
- `Simulation`은 `InputState`를 모른다. `Application`이 입력 + UI 소비 여부를 `PlayerIntent` 값으로 번역해 넘긴다.

## 프레임 흐름

메인 스레드 한 프레임(`Application::Run` 루프):

```text
1. InputState::BeginFrame()          이전 프레임의 에지 플래그 클리어
2. Win32Window::PumpMessages()       메시지 → IWindowEventSink → InputState / UIContext
                                     · 좌클릭을 UI가 소비하면 InputState 로 전달 안 함 (게임 입력 스킵)
3. 대기 중 resize 반영               Application → IRenderer::Resize, Simulation::SetWorldSize
                                     (실제 ResizeBuffers 는 렌더 스레드가 호출)
4. FrameClock::Tick()                clamp 된 deltaTime
5. BuildPlayerIntent()               InputState → PlayerIntent (정규화 전 축값)
6. FixedTimestep::Advance(dt)        누적 → 이번 프레임 실행할 고정 스텝 수 (상한 5)
7. for each step: Simulation::Step() 시간 누적 → 플레이어 이동 → JobSystem 파티클 advect+Fence
                                     → CollisionWorld2D(+3D) Clear/Add/Step → 접촉 읽어 상태 갱신
8. SnapshotBuilder::Build()          Simulation 상태 → 카메라 + MeshDraw(3D) + 월드 Quad + UI Quad
                                     (접촉 여부에 따라 색만 바꿈; 위치 보정 없음)
9. IRenderer::Submit(snapshot)       1슬롯 메일박스에 최신 프레임만 적재
```

렌더 스레드(`Dx11Renderer::RenderLoop`)는 독립적으로 돈다: 최신 스냅샷을 꺼내 **멀티샘플 씬 타깃**(MSAA, 최대 8x — [msaa.md](msaa.md))을 clear·bind하고, `IRenderPass` 목록을 등록 순서대로 실행하고, 백버퍼로 resolve한 뒤 `Present`한다. 기본 파이프라인은 `MeshPass3D`(깊이 테스트 on, 원근, 단일 directional light) → `QuadPass2D`(스크린 공간, 깊이 off, straight-alpha 블렌드). device·context·swap chain·depth·`ResizeBuffers`·`Present`의 유일 소유자다. 스레드 경계는 값 기반 `RenderSnapshot`만 넘어간다.

## 확장 지점

- **새 시스템(물리/애니메이션/컬링)** — `game/`에 클래스를 추가하고 `Application::Run`의 스텝 루프에서 호출한다(고정 `dt`). 병렬화가 필요하면 `JobSystem::ParallelFor`로 겹치지 않는 `[begin,end)` 범위만 쓰고 `Fence`는 단계 경계에서만 기다린다.
- **콜라이더 붙이기** — `physics::CollisionWorld2D`(또는 `#if ENGINE_WITH_3D` `CollisionWorld3D`)를 `Simulation` 멤버로 두고 `Step()`에서 `Clear`→`Add`→`Step`→`Contacts()`. 탐지만; 응답은 게임 코드. 사용법·레이어·불변 규칙은 [collider-design.md](collider-design.md).
- **FBX 모델 로드** — `import::LoadModelFromFile(path)` → `import::Model`(메시·머티리얼·스켈레톤·애니메이션). ufbx 는 `import` 안에 갇혀 있다. 포즈 평가는 `anim::AnimationSampler::Evaluate` (고정 스텝). **캐릭터 애니메이션은 지금 CPU 스키닝이다** — `ModelMeshPass3D`가 매 프레임 본 팔레트로 LBS를 CPU에서 계산해 DYNAMIC 정점 버퍼에 `Map/Unmap` 업로드(셰이더는 스킨 관련 코드 없음, `assets/shaders/cel.hlsl` 그대로). **GPU 스키닝(정점 셰이더에서 본 팔레트로 스킨)은 미구현, 설계만** — 새 `SkinnedMeshPass3D` + `StructuredBuffer` 본 팔레트 형태로 [model-animation-research.md](model-animation-research.md) §5.2(원안)·§5.2a(실제 CPU 구현)에 상세.
- **서드파티 추가** — `src/vendor/<lib>/`에 소스 vendor, 벤더 헤더는 그 라이브러리를 쓰는 `.cpp` 안에서만 include, 밖으로는 엔진 타입만. vcxproj 에 소스 추가(필요 시 `CompileAs`/`WarningLevel` per-file).
- **새 차원 모듈** — `<layer>/core` + `<layer>/x2d` + `<layer>/x3d` 디렉터리, 서로 include 금지, `ENGINE_WITH_3D`로 3D 빌드 제외 가능하게. `render`·`physics`가 예시.
- **새 위젯** — `ui::Widget`을 상속한다. 기존 위젯 수정 없이(OCP) `Build`(로컬 좌표 → `Quad`), `PointerXxx`(소비 시 `true`)만 구현한다. LSP: 기반 계약(로컬 좌표·`parentOrigin` 기준 배치·소비 반환)을 지킨다. `CheckBox`/`Slider`가 예시.
- **새 화면/씬 상태** — `game/<Screen>.h/.cpp`에 위젯 트리를 만드는 자유 함수(`TitleScreen`/`InGameHud`/`SettingsScreen`이 예시), `Application`이 `UIContext::SetScreen`(전체 교체) 또는 `SetOverlay`(모달)로 꽂는다. `UIContext`는 게임 개념을 모른다(OCP). `GameState` enum에 값 추가 + `Application::Run`의 스텝 게이팅 조건 갱신. 세부 `docs/scene-flow-design.md`.
- **새 렌더 패스/스테이지** — `render::IRenderPass`(`Name`/`Initialize(device, ShaderLibrary&)`/`Execute`/`Release`)를 구현하고 `main.cpp`에서 `renderer.AddRenderPass(...)`로 등록한다(Start 전). 셰이더는 `assets/shaders/<name>.hlsl` + `shaders.Get(device, "<name>", layout, count)`. 렌더러 코어·기존 패스는 안 건드린다(OCP). 세부는 [shader-pipeline.md](shader-pipeline.md).
- **셰이더 수정** — `assets/shaders/*.hlsl` 편집·저장 → 실행 중이면 다음 프레임에 핫리로드. 공통 코드는 `common3d.hlsli`.
- **새 렌더 프리미티브** — `render/RenderSnapshot.h`에 값 타입을 추가하고(예: 텍스처용 `SpriteDraw`) 그것을 소비하는 패스를 만든다. 렌더러 코어에 게임 개념(`playerX` 등)을 하드코딩하지 않는다.
- **렌더 백엔드 교체** — `IRenderer`를 구현하는 새 클래스를 만들고 `main.cpp`에서 그것을 생성한다. `IRenderPass`는 D3D11 전용 계약이라 새 백엔드는 자체 패스 계약을 갖는다.
- **입력 소스 추가(게임패드 등)** — `InputState`에 상태·질의를 추가하고 `Win32Window`(또는 새 platform 소스)가 채운다. gameplay는 여전히 `PlayerIntent` 번역을 거친다.

## 현재 데모 페이로드 (게임 아님)

뼈대가 살아있음을 보이기 위한 최소 콘텐츠만 있다. 실제 게임 로직은 없다.

- **3D 씬:** 화면 중앙에 FBX 모델(셀 셰이딩 + 인버티드 헐 아웃라인 + 크리즈 라인 + TGA, 천천히 회전) + 바닥 + 크기별 박스 7개. key 라이트가 좌우로 스윕하며 **캐스트 그림자**(2048² 셰도우맵, PCF)를 드리운다. 세부: [toon-rendering.md](toon-rendering.md) · [lighting.md](lighting.md) · [shadows.md](shadows.md).
- **비활성:** 기존 3D 큐브/충돌 데모, 2D 오버레이 데모(플레이어·장애물·파티클). `SnapshotBuilder` 의 `kBoxes` / `kDrawLegacy2D` 로 되돌릴 수 있음. UI(패널·버튼)는 유지.
- **2D 오버레이 (QuadPass2D):** 방향키로 움직이는 사각형 + 고정 장애물 박스 3개. `CollisionWorld2D`(AABB)로 겹침 감지 → 겹치면 플레이어가 주황색(위치 보정은 없음).
- 20,000개 파티클을 매 고정 스텝 `ParallelFor`로 advect — **JobSystem 처리량 스텁**. 앞 2,048개만 2D 점으로 그린다.
- `UIContext`: 반투명 패널 + `START` 버튼 + 상태 텍스트 2줄.

## 빌드

`CppWindowGame.vcxproj` (VS 2022, 툴셋 v143, `stdcpp20`, Level4). 인클루드 루트 `src`. 링크 `d3d11.lib;dxgi.lib;d3dcompiler.lib`. `Debug | x64` → `F5`. 3D 제외 빌드는 `PreprocessorDefinitions`에서 `ENGINE_WITH_3D` 제거 (2D 전용, 경고 0 검증됨).
