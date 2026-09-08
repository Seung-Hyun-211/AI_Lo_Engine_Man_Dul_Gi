# 커맨드 플레이북 (Command Playbook)

사용자는 명령으로만 작업한다. Claude는 명령이 오면 **먼저 이 표에서 해당 행을 찾아** 절차·판단지점·건드릴 파일을 확인하고 실행한다. 없는 명령이면 가장 가까운 행을 기준으로 처리하고, 새로 생긴 반복 명령은 이 파일에 행을 추가한다.

## 보고 규약 (매 명령 응답)

1. 바뀐 것 1~3줄
2. 판단 필요 시 옵션 A/B (권장안 먼저), 그 외엔 결정해서 진행하고 한 줄로 밝힘
3. 빌드 결과: `경고 N / 오류 N` (빌드법은 `docs/engine-overview.md` "빌드")
4. 커밋은 명시 요청 시에만

## 명령 → 처리 표

| # | 트리거 예시 | Claude가 하는 일 | 사용자가 판단할 것 | 건드리는 파일 |
|---|---|---|---|---|
| 1 | "위젯 하나 추가해 (체크박스/슬라이더/…)" | `ui::Widget` 상속 클래스. `Build`(로컬좌표→`Quad`), 필요 시 `PointerMove/Down/Up`(소비 시 `true`). `UIContext` 생성자에서 트리에 연결 | 위젯 종류·기본 크기·색. 상호작용 콜백 시그니처 | `src/ui/UI.h`, `src/ui/UI.cpp` |
| 2 | "게임 시스템 추가해 (물리/충돌/애니메이션/스폰)" | `src/game/`에 클래스. `Simulation`이 소유하거나 `Application` 스텝 루프에서 호출. 병렬 필요 시 `JobSystem::ParallelFor`(겹치지 않는 `[begin,end)`), `Fence`는 단계 경계에서만 | 고정 timestep에서 돌지 / 프레임당 1회인지. 데이터 레이아웃 AoS vs SoA | `src/game/Simulation.*` 또는 새 `src/game/<System>.*`, `src/game/Application.*` |
| 2c | "충돌 붙여 / 콜라이더 추가해" | **이미 있음**: `physics/p2d`·`physics/p3d`에 Box/Circle·Box/Sphere + `CollisionWorld*`(N² 탐지). `Simulation`이 소유·매 스텝 구동. 확장은 브로드페이즈·레이캐스트·트리거 enter/exit·응답. 사용법은 `docs/collider-design.md` "사용 방법" | 브로드페이즈(그리드/SAP), 트리거 이벤트 vs "지금 겹침", 응답을 physics에 넣을지 게임에 둘지 | `src/physics/*`, `src/game/Simulation.*` |
| 3 | "스프라이트(텍스처) 그릴 수 있게 해" | `render/r2d/Sprite2D.h`에 `SpriteDraw`(atlas id + uv rect) 값 타입 추가. 새 `TexturedSpritePass` 또는 `QuadPass2D` 확장. `SnapshotBuilder`가 방출 | 아틀라스 포맷(단일 PNG? 다중?). 좌표계·피벗. 로더를 IO 스레드로 뺄지 | `src/render/r2d/*`, `src/game/SnapshotBuilder.*` |
| 3b | "렌더 패스/스테이지 추가해 (그림자·블룸·디버그 라인·포스트프로세스)" | `render::IRenderPass` 구현(`Name`/`Initialize`/`Execute`/`Release`). `main.cpp`에서 `renderer.AddRenderPass(std::make_unique<...>())` (Start 전). 필요 데이터는 스냅샷에 값 타입으로 (2D면 `r2d`, 3D면 `r3d`) | 패스 순서(3D 뒤 / 2D 앞 어디). 자체 RT·리소스 필요 여부. 스냅샷에 뭘 실을지 | 새 `src/render/r2d/` 또는 `r3d/`, `src/main.cpp`, 스냅샷 헤더, vcxproj |
| 3c | "FBX 모델 그려" | **됨(정적 + 텍스처)**: `ModelMeshPass3D` 가 시작 시 FBX + 디퓨즈 TGA 로드 → 바인드 포즈 렌더. 남은 일은 3e | | `src/render/r3d/ModelMeshPass3D.*` |
| 3d | "텍스처 개선 (노멀맵/스페큘러/밉맵/PNG·DDS)" | 현재: 디퓨즈 TGA(uncompressed 24/32) + alpha cutout. 추가: `TgaImage` 에 RLE, 또는 stb_image vendor(PNG/JPG), `_NRM`/`_SPEC` 슬롯 + 셰이더 확장, `D3D11_BIND_RENDER_TARGET|MISC_GENERATE_MIPS` 로 밉 생성 | 포맷 범위, 노멀맵 공간(탄젠트 필요 — importer 에 tangent 추가), 반투명 머티리얼 정렬 | `src/import/TgaImage.*` 또는 새 로더, `src/render/r3d/ModelMeshPass3D.*`, `src/import/Model.h` |
| 3e | "캐릭터 애니메이션 (스키닝)" | `anim::AnimationSampler`(있음)로 본 팔레트 평가 → `ModelMeshPass3D` 를 스킨 셰이더로(정점에 boneIndices/Weights, cbuffer bones[≤64] 또는 StructuredBuffer). 애니메이션 있는 FBX 필요(Unity-chan 모델엔 클립 0). 설계 `docs/model-animation-research.md` §5 | cbuffer vs SRV, 상태 머신을 game 에, winding 검증 후 back-cull | `src/render/r3d/ModelMeshPass3D.*` 또는 새 `SkinnedMeshPass3D`, `src/render/r3d/Scene3D.h`, `src/game/*` |
| 4 | "실제 게임 시작하자 / 씬·엔티티 만들어" | `src/game/`에 엔티티 표현(초기엔 AoS `vector<Entity>`), 씬/스테이트 개념. `Simulation`의 데모 페이로드(플레이어+파티클+회전 큐브)를 실제 콘텐츠로 교체하거나 분리. `SnapshotBuilder`가 엔티티 → `MeshDraw`/`Quad` | 장르·핵심 루프(2D인지 3D인지). 엔티티 모델(컴포넌트? 단순 struct?). 데모 페이로드 유지 여부 | `src/game/Simulation.*`, `src/game/SnapshotBuilder.*`, 새 `src/game/*` |
| 4b | "3D 카메라 조작 붙여 (WASD·마우스 등)" | `Simulation`에 카메라 상태(위치·yaw·pitch). `Application::BuildPlayerIntent` 옆에 카메라 intent. `SnapshotBuilder::BuildCamera`가 궤도 대신 그 상태로 view 생성 | FPS / 오빗 / 고정. 감도·역전 옵션 위치 | `src/game/Simulation.*`, `src/game/Application.*`, `src/game/SnapshotBuilder.cpp` |
| 5 | "게임패드/다른 입력 지원해" | `input::InputState`에 상태+질의 추가. `platform`에 소스(XInput 등). `Application::BuildPlayerIntent`가 통합 | 지원 장치(XInput? RawInput?). 데드존·매핑 노출 위치 | `src/input/InputState.h`, `src/platform/*`, `src/game/Application.*` |
| 6 | "렌더 백엔드 바꿔/추가해 (DX12 등)" | 새 클래스가 `render::IRenderer` 구현. `main.cpp`에서 생성 타입만 교체. UI/게임 코드는 불변 | 교체 vs 런타임 선택. 스냅샷 포맷 변경 필요 여부 | 새 `src/render/<Backend>.*`, `src/main.cpp`, (필요 시) `src/render/RenderSnapshot.h` |
| 7 | "스왑체인 FLIP_DISCARD로" | `DXGI_SWAP_EFFECT_FLIP_DISCARD` + 프레임마다 RTV 재바인딩 확인. 필요 시 `IDXGISwapChain` 최소 버전 상향 | 없음 (권장 기본값). Win10 미만 지원 포기 여부 | `src/render/Dx11Renderer.cpp` |
| 8 | "시뮬/렌더 파이프라이닝 / 더블 버퍼링" | 스냅샷 더블버퍼 + 프레임 N 시뮬과 N-1 렌더 병렬. `Application` 루프 재구성 | 입력 지연 1프레임 허용치. 스냅샷 보간 여부 | `src/game/Application.*`, `src/render/Dx11Renderer.*` |
| 9 | "frame-time HUD / 프로파일러" | `core`에 스코프 타이머. `Application`이 프레임 시간·Fence 대기·worker 편차 수집 → UI `TextLine`으로 표시 | 표시 항목. 릴리스 빌드 포함 여부 | 새 `src/core/Profiler.*`, `src/game/Application.*`, `src/ui/UI.*` |
| 10 | "빌드해 / 실행해 / 스모크 테스트" | CLI 빌드(`docs/engine-overview.md` "빌드"), exe 실행 후 수 초 생존 확인, 결과 보고. 이 환경은 GPU 없어 WARP 폴백이 정상 | 없음 | — |
| 11 | "커밋해" | 변경 스테이징, 한국어 요약 메시지, `Co-Authored-By` 푸터. `main`이면 브랜치 먼저 | 커밋 범위·메시지 | — |

## 불변 규칙 (모든 행에 적용 — 깨지 말 것)

- D3D11 호출은 렌더 스레드에서만 (`Dx11Renderer.cpp` + `render/r2d/*` + `render/r3d/*`). 메인은 입력·시뮬·스냅샷만.
- 스레드 경계는 값 기반 `RenderSnapshot`만 (`Quad`, `Scene3D`/`MeshDraw`/`CameraView` 전부 값). 가변 게임 객체 포인터 금지.
- 렌더러 코어는 device/swapchain/RT/depth만 소유. 그리는 일은 전부 `IRenderPass` 목록 (기본: `MeshPass3D` → `QuadPass2D`).
- **2D/3D 모듈은 서로 `#include` 금지.** `math/Math2D↔3D`, `render/r2d↔r3d`, `physics/p2d↔p3d`. 공유는 각 core만. 3D는 `ENGINE_WITH_3D`로 감싸 빌드 제외 가능하게 유지.
- 충돌은 탐지만. `CollisionWorld`는 시뮬(메인 스레드)만. 응답은 physics 밖.
- SOLID 우선: 새 타입은 `= delete` 복사 방지, 소유는 `unique_ptr`, raw는 비소유, 다형성은 인터페이스.
- 데드 코드 남기지 않기. 벤치마크 스텁은 코드에 명시.
- 서드파티는 `src/vendor/`에 소스 vendor, 벤더 타입은 그걸 쓰는 `.cpp` 안에만 (밖으로는 엔진 타입).
- **설계 문서를 만들면 "사용 방법(How to use)" 항목 필수.**
- 상세 계약: `docs/engine-overview.md`, `multithreaded_game_engine_architecture.md`, `ui-architecture.md`, `time-design.md`, `collider-design.md`, `model-animation-research.md`.
