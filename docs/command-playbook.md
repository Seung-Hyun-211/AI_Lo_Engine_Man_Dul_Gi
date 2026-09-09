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
| 3b | "렌더 패스/스테이지 추가해 (그림자·블룸·디버그 라인·포스트프로세스)" | `render::IRenderPass` 구현. 셰이더는 `assets/shaders/<name>.hlsl` + `Initialize(device, ShaderLibrary&)` 에서 `shaders.Get(...)`. `main.cpp` 에서 `AddRenderPass`. 데이터는 스냅샷 값 타입 | 패스 순서, 자체 RT 필요 여부, 스냅샷에 뭘 실을지 | 새 `src/render/r2d/` 또는 `r3d/`, `assets/shaders/*.hlsl`, `src/main.cpp`, 스냅샷 헤더, vcxproj (셰이더는 `<None>`) |
| 3f | "셰이더 시스템 개선 (디파인/순열, .cso 캐시, 컴퓨트)" | `ShaderLibrary::Get` 에 `D3D_SHADER_MACRO*` 인자, 바이트코드 디스크 캐시, `CSMain`/`cs_5_0` 지원. 설계는 `docs/shader-pipeline.md` "다음" | 순열 키 방식, 캐시 무효화, Release 빌드 리로드 정책 | `src/render/shader/ShaderLibrary.*` |
| 3g | "툰 룩 조정 (셀 밴드/아웃라인/크리즈)" | **있음**: 셀(`common3d.hlsli::ApplyCelLighting`), 인버티드 헐 아웃라인(`outline.hlsl` + `kOutlineWidth`), 크리즈 라인(`import/CreaseLines` + `CreaseOptions`). 세부·튜닝표는 `docs/toon-rendering.md` §7 | 아웃라인 방식(헐/포스트/림), 크리즈 임계각, 화면공간 vs 월드공간 폭 | `assets/shaders/*.hlsl`, `src/render/r3d/ModelMeshPass3D.*`, `src/import/CreaseLines.*` |
| 3i | "AA 개선 (셀 밴드/컷아웃/포스트)" | **MSAA 있음** (씬 타깃 최대 8x + resolve, `docs/msaa.md`). 다음: alpha-to-coverage(컷아웃 엣지), `ApplyCelLighting` 밴드에 `smoothstep`, 포스트 FXAA(오프스크린 RT 선행). 샘플 수는 `Dx11Renderer::CreateDeviceAndSwapChain` 후보 리스트 | 셀 단차를 셰이더 vs 포스트로, 8x 비용, 샘플 수를 FrameSettings 로 노출할지 | `src/render/Dx11Renderer.*`, `assets/shaders/common3d.hlsli`, 새 포스트 패스 |
| 3h | "조명 추가/개선 (포인트·스팟·그림자·rim)" | **key + ambient 있음** (`render/r3d/Lighting.h`, `Scene3D::lighting`). **그림자 있음**(단일 방향 셰도우맵 + PCF, `docs/shadows.md`). 다음: 라이트 배열(cbuffer/StructuredBuffer), CSM/카메라 추종, rim. 설계는 `docs/lighting.md` 로드맵 | 라이트 개수 상한·컬링, 그림자 해상도, `BuildLighting` 을 게임 상태 기반으로 | `src/render/r3d/Lighting.h`, `FrameConstants.h`, `assets/shaders/common3d.hlsli`, `src/game/SnapshotBuilder.cpp` |
| 3c | "FBX 모델 그려" | **됨(정적 + 텍스처)**: `ModelMeshPass3D` 가 시작 시 FBX + 디퓨즈 TGA 로드 → 바인드 포즈 렌더. 남은 일은 3e | | `src/render/r3d/ModelMeshPass3D.*` |
| 3d | "텍스처 개선 (노멀맵/스페큘러/밉맵/PNG·DDS)" | 현재: 디퓨즈 TGA(uncompressed 24/32) + alpha cutout. 추가: `TgaImage` 에 RLE, 또는 stb_image vendor(PNG/JPG), `_NRM`/`_SPEC` 슬롯 + 셰이더 확장, `D3D11_BIND_RENDER_TARGET|MISC_GENERATE_MIPS` 로 밉 생성 | 포맷 범위, 노멀맵 공간(탄젠트 필요 — importer 에 tangent 추가), 반투명 머티리얼 정렬 | `src/import/TgaImage.*` 또는 새 로더, `src/render/r3d/ModelMeshPass3D.*`, `src/import/Model.h` |
| 3e | "캐릭터 애니메이션 (스키닝)" | **됨 (CPU 스키닝)**: `ModelMeshPass3D` 가 `animClipIndex`/`animClipTime`(Scene3D::ModelDraw)를 보고 매 프레임 `AnimationSampler::Evaluate` + LBS 로 정점 재계산해 DYNAMIC VB 업로드(셰이더 무변경). 클립은 `import::LoadAnimationClipsFromFile` 로 별도 FBX에서 본 이름 매칭 리타깃. Unity-chan 26개 클립은 `render/r3d/CharacterAnimationClips.h`. 여러 인스턴스를 각자 다른 애니메이션으로 세우려면 여전히 §5.2 GPU `SkinnedMeshPass3D` 필요. 상세 `docs/model-animation-research.md` §5.2a, 전체 지도는 `docs/animation-design.md` §1 | (해결됨) 다음 후보는 GPU 스킨 전환·크로스페이드 | `src/render/r3d/ModelMeshPass3D.*`, `src/render/r3d/CharacterAnimationClips.h`, `src/import/ModelImporter.*`, `src/game/CharacterAnimationState.*`, `src/game/Simulation.*` |
| 3ea | "2D 스프라이트 애니메이션 (프레임 애니메이션)" | 설계됨(미구현): `anim::a2d::SpriteAnimationClip`/`SpriteAnimator` — uv rect 프레임 배열을 시간에 따라 스텝. `SnapshotBuilder`가 `CurrentFrame().uvRect`를 `SpriteDraw`에 복사. 전제: 텍스처 `SpriteDraw`(#3) 선행 필요. 설계 `docs/animation-design.md` §2 | 클립 데이터 포맷(코드 배열 vs 에셋 파일), 상태 머신(`anim::core`) 도입 시점 | 새 `src/anim/a2d/*`, `src/render/r2d/Sprite2D.h`, `src/game/SnapshotBuilder.*` |
| 3eb | "Live2D/Spine 같은 2D 리깅 애니메이션" | **연구 선행 필요** — 바로 설계·구현하지 않는다. `docs/animation-design.md` §4 의 조사 항목(라이선스, 포맷, vendor 가능 여부) 먼저 별도 조사 문서로 정리 | 상용 SDK 라이선스 수용 여부, 아트 파이프라인(실제 사용 툴) 확인이 먼저 | `docs/animation-design.md` (조사 진행 시 새 `docs/2d-mesh-animation-research.md`) |
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
- 렌더러 코어는 device/swapchain/RT/depth + `ShaderLibrary` 만 소유. 그리는 일은 전부 `IRenderPass` 목록.
- 패스는 셰이더를 직접 `D3DCompile` 하지 않는다. `assets/shaders/*.hlsl` + `ShaderLibrary::Get`. `.hlsl`/`.hlsli` 는 커밋되는 소스.
- **2D/3D 모듈은 서로 `#include` 금지.** `math/Math2D↔3D`, `render/r2d↔r3d`, `physics/p2d↔p3d`. 공유는 각 core만. 3D는 `ENGINE_WITH_3D`로 감싸 빌드 제외 가능하게 유지.
- 충돌은 탐지만. `CollisionWorld`는 시뮬(메인 스레드)만. 응답은 physics 밖.
- SOLID 우선: 새 타입은 `= delete` 복사 방지, 소유는 `unique_ptr`, raw는 비소유, 다형성은 인터페이스.
- 데드 코드 남기지 않기. 벤치마크 스텁은 코드에 명시.
- 서드파티는 `src/vendor/`에 소스 vendor, 벤더 타입은 그걸 쓰는 `.cpp` 안에만 (밖으로는 엔진 타입).
- **설계 문서를 만들면 "사용 방법(How to use)" 항목 필수.**
- 상세 계약: `docs/engine-overview.md`, `multithreaded_game_engine_architecture.md`, `ui-architecture.md`, `time-design.md`, `collider-design.md`, `model-animation-research.md`, `animation-design.md`, `shader-pipeline.md`, `toon-rendering.md`, `lighting.md`, `msaa.md`, `shadows.md`.
