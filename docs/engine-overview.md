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
        │    └─ JobSystem    (core/)            연속 범위 병렬 update
        ├─ SnapshotBuilder   (game/)            월드+UI → 값 기반 RenderSnapshot
        └─ IRenderer         (render/)          추상 렌더러 (구현: Dx11Renderer)
                                                코어는 device/swapchain/RT/depth만 소유
                                                그리기 = IRenderPass 목록
                                                  MeshPass3D  (3D, 깊이 테스트, 원근)
                                                  QuadPass2D  (2D, 스크린 공간, 깊이 off)
```

## 모듈과 책임 (SRP)

| 모듈 | 파일 | 유일한 변경 이유 |
|---|---|---|
| `engine::math` | `math/Math.h` | 공용 기하 타입(`Vec2`/`Vec3`/`Mat4`/`Rect`/`Color`)이 바뀔 때 |
| `engine::core` | `core/JobSystem.*`, `core/Time.h`, `core/NonCopyable.h` | 작업 스케줄링·시간 누적 규칙이 바뀔 때 |
| `engine::platform` | `platform/Win32Window.*` | OS 창/메시지 처리 방식이 바뀔 때 |
| `engine::input` | `input/InputState.h` | 입력 상태 표현·에지 판정이 바뀔 때 |
| `engine::render` | `render/IRenderer.h`, `render/RenderPass.h`, `render/RenderSnapshot.h`, `render/Dx11Renderer.*`, `render/passes/*` | 렌더 백엔드/스냅샷 포맷/파이프라인 스테이지가 바뀔 때 |
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
7. for each step: Simulation::Step() 플레이어 이동 + JobSystem 으로 파티클 병렬 advect + Fence + 시간 누적
8. SnapshotBuilder::Build()          카메라 + MeshDraw(3D) + 월드 Quad + UI Quad → 값 기반 RenderSnapshot
9. IRenderer::Submit(snapshot)       1슬롯 메일박스에 최신 프레임만 적재
```

렌더 스레드(`Dx11Renderer::RenderLoop`)는 독립적으로 돈다: 최신 스냅샷을 꺼내 RT·depth를 clear·bind하고, `IRenderPass` 목록을 등록 순서대로 실행한 뒤 `Present`한다. 기본 파이프라인은 `MeshPass3D`(깊이 테스트 on, 원근, 단일 directional light) → `QuadPass2D`(스크린 공간, 깊이 off, straight-alpha 블렌드). device·context·swap chain·depth·`ResizeBuffers`·`Present`의 유일 소유자다. 스레드 경계는 값 기반 `RenderSnapshot`만 넘어간다.

## 확장 지점

- **새 시스템(물리/애니메이션/컬링)** — `game/`에 클래스를 추가하고 `Application::Run`의 스텝 루프에서 호출한다. 병렬화가 필요하면 `JobSystem::ParallelFor`로 겹치지 않는 `[begin,end)` 범위만 쓰고 `Fence`는 단계 경계에서만 기다린다.
- **새 위젯** — `ui::Widget`을 상속한다. 기존 위젯 수정 없이(OCP) `Build`(로컬 좌표 → `Quad`), `PointerXxx`(소비 시 `true`)만 구현한다. LSP: 기반 계약(로컬 좌표·`parentOrigin` 기준 배치·소비 반환)을 지킨다.
- **새 렌더 패스/스테이지** — `render::IRenderPass`(`Name`/`Initialize`/`Execute`/`Release`)를 구현하고 `main.cpp`에서 `renderer.AddRenderPass(...)`로 등록한다(Start 전). 렌더러 코어·기존 패스는 건드리지 않는다(OCP). 그림자·블룸·디버그 라인·포스트프로세스가 여기 해당한다.
- **새 렌더 프리미티브** — `render/RenderSnapshot.h`에 값 타입을 추가하고(예: 텍스처용 `SpriteDraw`) 그것을 소비하는 패스를 만든다. 렌더러 코어에 게임 개념(`playerX` 등)을 하드코딩하지 않는다.
- **렌더 백엔드 교체** — `IRenderer`를 구현하는 새 클래스를 만들고 `main.cpp`에서 그것을 생성한다. `IRenderPass`는 D3D11 전용 계약이라 새 백엔드는 자체 패스 계약을 갖는다.
- **입력 소스 추가(게임패드 등)** — `InputState`에 상태·질의를 추가하고 `Win32Window`(또는 새 platform 소스)가 채운다. gameplay는 여전히 `PlayerIntent` 번역을 거친다.

## 현재 데모 페이로드 (게임 아님)

뼈대가 살아있음을 보이기 위한 최소 콘텐츠만 있다. 실제 게임 로직은 없다.

- **3D (MeshPass3D):** 바닥 평면 + 두 축으로 회전하는 큐브 + 공전하는 작은 큐브 2개. 카메라는 원점을 천천히 궤도. 단일 directional light Lambert.
- **2D 오버레이 (QuadPass2D):** 방향키로 움직이는 시안색 64×64 사각형(경계 clamp, 대각선 정규화).
- 20,000개 파티클을 매 고정 스텝 `ParallelFor`로 advect — **JobSystem 처리량 스텁**. `SnapshotBuilder`는 앞 2,048개만 2D 점으로 그린다. 나머지는 계산만 하는 벤치마크 부하다.
- `UIContext`: 반투명 패널 1개 + `START` 버튼 + 상태 텍스트 2줄. 버튼 클릭 시 상태 텍스트가 바뀐다.

## 빌드

`CppWindowGame.vcxproj` (VS 2022, 툴셋 v143, `stdcpp20`, Level4). 인클루드 루트 `src`. 링크 `d3d11.lib;dxgi.lib;d3dcompiler.lib`. `Debug | x64` → `F5`.
