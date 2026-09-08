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
| 3 | "스프라이트(텍스처) 그릴 수 있게 해" | `render/RenderSnapshot.h`에 `SpriteDraw`(atlas id + uv rect) 값 타입 추가. `Dx11Renderer`에 텍스처 SRV·샘플러·아틀라스 로딩·배치. `SnapshotBuilder`가 `SpriteDraw` 방출 | 아틀라스 포맷(단일 PNG? 다중?). 좌표계·피벗. 로더를 IO 스레드로 뺄지 (로드맵 4) | `src/render/RenderSnapshot.h`, `src/render/Dx11Renderer.*`, `src/game/SnapshotBuilder.*` |
| 4 | "실제 게임 시작하자 / 씬·엔티티 만들어" | `src/game/`에 엔티티 표현(초기엔 AoS `vector<Entity>`), 씬/스테이트 개념. `Simulation`의 데모 페이로드(플레이어+파티클)를 실제 콘텐츠로 교체하거나 분리 | 장르·핵심 루프. 엔티티 모델(컴포넌트? 단순 struct?). 파티클 스텁 유지 여부 | `src/game/Simulation.*`, 새 `src/game/*`, `docs/engine-overview.md` |
| 5 | "게임패드/다른 입력 지원해" | `input::InputState`에 상태+질의 추가. `platform`에 소스(XInput 등). `Application::BuildPlayerIntent`가 통합 | 지원 장치(XInput? RawInput?). 데드존·매핑 노출 위치 | `src/input/InputState.h`, `src/platform/*`, `src/game/Application.*` |
| 6 | "렌더 백엔드 바꿔/추가해 (DX12 등)" | 새 클래스가 `render::IRenderer` 구현. `main.cpp`에서 생성 타입만 교체. UI/게임 코드는 불변 | 교체 vs 런타임 선택. 스냅샷 포맷 변경 필요 여부 | 새 `src/render/<Backend>.*`, `src/main.cpp`, (필요 시) `src/render/RenderSnapshot.h` |
| 7 | "스왑체인 FLIP_DISCARD로" | `DXGI_SWAP_EFFECT_FLIP_DISCARD` + 프레임마다 RTV 재바인딩 확인. 필요 시 `IDXGISwapChain` 최소 버전 상향 | 없음 (권장 기본값). Win10 미만 지원 포기 여부 | `src/render/Dx11Renderer.cpp` |
| 8 | "시뮬/렌더 파이프라이닝 / 더블 버퍼링" | 스냅샷 더블버퍼 + 프레임 N 시뮬과 N-1 렌더 병렬. `Application` 루프 재구성 | 입력 지연 1프레임 허용치. 스냅샷 보간 여부 | `src/game/Application.*`, `src/render/Dx11Renderer.*` |
| 9 | "frame-time HUD / 프로파일러" | `core`에 스코프 타이머. `Application`이 프레임 시간·Fence 대기·worker 편차 수집 → UI `TextLine`으로 표시 | 표시 항목. 릴리스 빌드 포함 여부 | 새 `src/core/Profiler.*`, `src/game/Application.*`, `src/ui/UI.*` |
| 10 | "빌드해 / 실행해 / 스모크 테스트" | CLI 빌드(`docs/engine-overview.md` "빌드"), exe 실행 후 수 초 생존 확인, 결과 보고. 이 환경은 GPU 없어 WARP 폴백이 정상 | 없음 | — |
| 11 | "커밋해" | 변경 스테이징, 한국어 요약 메시지, `Co-Authored-By` 푸터. `main`이면 브랜치 먼저 | 커밋 범위·메시지 | — |

## 불변 규칙 (모든 행에 적용 — 깨지 말 것)

- D3D11 호출은 렌더 스레드(`Dx11Renderer.cpp`)에서만. 메인은 입력·시뮬·스냅샷만.
- 스레드 경계는 값 기반 `RenderSnapshot`만. 가변 게임 객체 포인터 금지.
- SOLID 우선: 새 타입은 `= delete` 복사 방지, 소유는 `unique_ptr`, raw는 비소유, 다형성은 인터페이스(`Widget`/`IRenderer`/`IWindowEventSink`).
- 데드 코드 남기지 않기. 벤치마크 스텁은 코드에 명시.
- 상세 계약: `docs/engine-overview.md`, `docs/multithreaded_game_engine_architecture.md`, `docs/ui-architecture.md`.
