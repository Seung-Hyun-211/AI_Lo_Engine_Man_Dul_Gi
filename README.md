# AI Lo Engine — 2D DX11 게임 엔진 뼈대

**C++20 / Win32 / DirectX 11** 기반 2D 게임 엔진의 프로젝트 루트. 특정 게임은 아직 없고, 게임을 얹을 수 있는 **구조와 프레임 흐름**만 구현되어 있다.

## 실행

Visual Studio 2022로 `CppWindowGame.vcxproj`를 열고 **Debug | x64** 선택 후 `F5`.
툴셋 v143, `LanguageStandard=stdcpp20`, `WarningLevel=Level4`. 링크: `d3d11.lib;dxgi.lib;d3dcompiler.lib`. 인클루드 루트 `src`.

## 모듈 구조

```text
src/
  main.cpp                 진입점. Dx11Renderer 를 만들어 IRenderer 로 Application 에 주입
  math/
    Math.h                 umbrella (2D 항상 + 3D는 ENGINE_WITH_3D 일 때)
    Math2D.h               Vec2 / Rect / Color            [2D 모듈]
    Math3D.h               Vec3 / Vec4 / Mat4 (row-major, LH)  [3D 모듈]
  core/
    JobSystem.*            워커 풀 + Job + Fence (예외 안전). ParallelFor
    Time.h                 FrameClock(clamp 된 delta), FixedTimestep(고정 스텝)  → docs/time-design.md
    AssetPaths.*           작업 디렉터리와 무관하게 assets/ 루트 탐색
    NonCopyable.h          소유 타입 공통 base
  platform/Win32Window.*   OS 창 + WndProc → IWindowEventSink 로 이벤트 전달
  input/InputState.h       이번 프레임 키/마우스 상태 + 에지 질의(Pressed/Released)
  render/                  core + shader + r2d/r3d 모듈
    IRenderer.h            렌더러 추상 (Start/SetFrameSettings/Submit/Resize/Stop)
    RenderPass.h           파이프라인 스테이지 추상 (Initialize/Execute/Release) — 확장 지점
    RenderSnapshot.h       값 기반 스냅샷: scene3d(3D) + worldQuads + uiQuads
    shader/ShaderLibrary.* assets/shaders/*.hlsl 컴파일·캐시·핫리로드  → docs/shader-pipeline.md
    Dx11Renderer.*         렌더 스레드. device/swapchain/depth + ShaderLibrary 소유. 패스 목록 실행
    r2d/Sprite2D.h, QuadPass2D.*   [2D] 스크린 공간 Quad, 깊이 off, straight-alpha
    r3d/Lighting.h, FrameConstants.h  [3D] key + ambient 조명, Frame cbuffer  → docs/lighting.md
    r3d/Scene3D.h, MeshPass3D.*    [3D] 깊이 테스트, 원근 카메라, 내장 큐브·평면
    r3d/ModelMeshPass3D.*          [3D] FBX 셀 셰이딩 + 아웃라인 + 크리즈 라인 + TGA  → docs/toon-rendering.md
  physics/                 core + p2d/p3d 모듈  → docs/collider-design.md
    Collision.h            ColliderId / CollisionLayer / Contact
    p2d/Collider2D.h, CollisionWorld2D.*   [2D] Box·Circle, N² 겹침 탐지
    p3d/Collider3D.h, CollisionWorld3D.*   [3D] Box·Sphere, N² 겹침 탐지
  import/                  FBX -> engine Model  → docs/model-animation-research.md
    Model.h                ModelMesh / ModelMaterial / Skeleton(Bone) / AnimationClip
    ModelImporter.*        ufbx 로 로드. 메시·머티리얼·스켈레톤·애니메이션(bake). ufbx 는 여기 갇힘
    TgaImage.*             작은 TGA 리더 (uncompressed 24/32bpp) -> RGBA8
    CreaseLines.*          법선각 급한 에지 -> AO 톤 크리즈 리본 (CPU, 로드 시 1회)
  anim/AnimationSampler.*  CPU 포즈 평가: (skeleton + clip + time) -> 본별 skin Mat4
  vendor/ufbx/             ufbx 0.23.0 (ufbx.h + ufbx.c), MIT/Public Domain
  ui/UI.*                  Widget / UIWindow / Button / TextLine / UIContext
  game/
    Simulation.*           가변 월드. 고정 timestep. 플레이어 + 장애물 + 20k 파티클 + 3D 데모. 충돌 구동
    SnapshotBuilder.*      Simulation + UIContext → RenderSnapshot
    Application.*           조립·프레임 지휘. IWindowEventSink 구현
assets/
  shaders/*.hlsl(i)        cel / model / mesh / outline / crease / quad2d + common3d.hlsli (핫리로드)
  models/                  FBX (+ .tga 는 .gitignore)
```

2D와 3D는 서로 `#include` 하지 않는 별도 모듈이다. `ENGINE_WITH_3D`를 빼면 3D 코드가 빌드에서 완전히 제외되고 2D 전용 exe가 경고 0으로 빌드된다.

자세한 지도·프레임 흐름·확장 지점은 [docs/engine-overview.md](docs/engine-overview.md). 명령 단위 작업 절차는 [docs/command-playbook.md](docs/command-playbook.md). 설계 문서: [time-design](docs/time-design.md) · [collider-design](docs/collider-design.md) · [model-animation-research](docs/model-animation-research.md) · [shader-pipeline](docs/shader-pipeline.md) · [toon-rendering](docs/toon-rendering.md) · [lighting](docs/lighting.md) · [msaa](docs/msaa.md).

## 스레드 계약

```text
Main Thread (Application::Run):
  Win32 message → InputState/UIContext → FixedTimestep → Simulation::Step → Job Fence
               → SnapshotBuilder → RenderSnapshot ──IRenderer::Submit──┐
                                                                        ▼
Render Thread (Dx11Renderer::RenderLoop): latest-frame mailbox → DX11 draw → Present
```

- 메인 스레드는 D3D11 API를 호출하지 않는다. 모든 DX11 호출은 `src/render/Dx11Renderer.cpp`(렌더 스레드)에만 있다.
- 스레드 경계는 값 기반 `RenderSnapshot`만 넘어간다. 렌더러는 최신 스냅샷 1개만 보관하고 오래된 미렌더 프레임은 버린다.
- 창 resize 요청은 메인에서 전달하되 `ResizeBuffers`는 렌더 스레드만 호출한다.
- Job 예외는 `WorkerLoop`가 잡아 `JobFence::Wait()` 지점에서 재전파한다(데드락 없음).

전체 계약과 로드맵은 [docs/multithreaded_game_engine_architecture.md](docs/multithreaded_game_engine_architecture.md).

## 설계 원칙

객체지향 설계와 SOLID를 최우선으로 한다. 각 모듈은 변경 이유가 하나(SRP)이고, `Application`은 조립만 한다. 상위 레이어는 구현이 아니라 추상(`IRenderer`, `IWindowEventSink`)에 의존한다(DIP). 위젯·렌더 프리미티브는 기존 타입 수정 없이 확장한다(OCP). 복사 방지(`NonCopyable`), 소유권은 `unique_ptr`, raw 포인터는 비소유 관찰용.

## 현재 데모 (게임 아님)

3D: 회전 큐브 + 궤도 반경이 진동하는 위성 큐브 2개(중심 큐브와 접촉하면 빨강 — `CollisionWorld3D` Box↔Sphere) + 바닥 평면, 궤도 카메라, directional light. 2D 오버레이: 방향키로 움직이는 사각형 + 장애물 박스 3개(겹치면 주황 — `CollisionWorld2D` AABB), 반투명 UI 패널 + `START` 버튼 + 상태 텍스트. 배경: 매 고정 스텝 `ParallelFor`로 도는 20,000개 파티클(앞 2,048개만 그림). 충돌은 탐지만 하고 위치 보정은 없다. 실제 게임 로직은 아직 없다.

## UI 설계

인게임 창·버튼·텍스트의 UI 계층은 [docs/ui-architecture.md](docs/ui-architecture.md)에 문서화되어 있다. 위젯은 `Quad` 방출에만 의존하므로 DX11 → DX12 교체 시에도 UI 코드는 유지된다.
