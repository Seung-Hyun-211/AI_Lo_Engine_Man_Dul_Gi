# CLAUDE.md

C++20 / Win32 / DirectX 11 기반 2D 게임 엔진 뼈대. 이 파일은 세션마다 자동 로드되는 상시 지침이다.

## 작업 방식 (사용자는 명령으로만 운전한다)

사용자는 모든 변경을 명령으로 지시하고, 판단할 수 있을 만큼만 보길 원한다. 코드 설명·장문 금지.

- 명령이 오면 **먼저 `docs/command-playbook.md`에서 해당 행**(트리거 → 절차 → 판단지점 → 파일)을 찾아 실행한다.
- 응답은: ① 바뀐 것 1~3줄 ② 판단 필요 시 옵션 A/B(권장안 먼저), 아니면 결정하고 한 줄로 밝힘 ③ 빌드 결과 `경고 N / 오류 N` ④ 커밋은 명시 요청 시에만.
- "사용법/확장법" 지식은 채팅이 아니라 `CLAUDE.md` · `docs/*` · memory에 남긴다.
- CLI 빌드·스모크테스트 절차는 memory `build-and-run` 참조 (이 환경은 GPU 없어 WARP 폴백이 정상).

## 빌드 / 실행

- Visual Studio 2022로 `CppWindowGame.vcxproj`를 연다. `Debug | x64` 선택 후 `F5`.
- 툴셋 v143, `LanguageStandard=stdcpp20`, `WarningLevel=Level4`.
- 링크: `d3d11.lib;dxgi.lib;d3dcompiler.lib`. 인클루드 루트는 `src`.
- 별도 테스트 프로젝트·CI 없음.

## 아키텍처 불변 규칙 (절대 깨지 말 것)

1. **D3D11 API 호출은 렌더 스레드에서만** 한다. 메인 스레드(`src/game/Application.cpp`의 프레임 루프)는 입력·시뮬레이션·스냅샷 생성만 한다.
2. 렌더 스레드(`src/render/Dx11Renderer.cpp`)가 device, immediate context, swap chain, back buffer, `ResizeBuffers`, `Present`의 **유일한 소유자**다.
3. 스레드 경계는 값 기반 `RenderSnapshot`만 넘어간다. 가변 게임 객체 포인터를 넣지 않는다.
4. 렌더러는 최신 스냅샷 1개만 보관한다(1슬롯 메일박스). 오래된 미렌더 프레임은 버린다.
5. 창 resize 요청은 메인에서 전달하되 `ResizeBuffers`는 렌더 스레드만 호출한다.
6. `JobSystem::ParallelFor`의 각 잡은 겹치지 않는 연속 `[begin, end)` 범위만 쓴다. 워커 안에서 공유 카운터 증가·`vector` 재할당·엔티티 생성/파괴 금지. `JobFence::Wait()`는 프레임 단계 경계에서만 쓴다.

모듈 지도·프레임 흐름·확장 지점은 `docs/engine-overview.md`, 전체 스레드 계약은 `docs/multithreaded_game_engine_architecture.md`, UI 계층은 `docs/ui-architecture.md`.

## 설계 원칙 — 최우선 (모든 신규/수정 코드에 적용)

객체지향 설계와 SOLID를 다른 모든 작업보다 우선한다. 아래 "최우선 작업"도 이 원칙을 지키는 방식으로 구현한다.

- **SRP (단일 책임)** — 클래스 하나는 변경 이유가 하나여야 한다. 예전 `main.cpp`의 `Game` god class는 `platform::Win32Window` · `input::InputState` · `game::Simulation` · `game::SnapshotBuilder` · `game::Application`(조립·조율만)으로 분해됨. `main.cpp`는 진입점 한 함수. 새 코드도 이 경계를 지킨다.
- **OCP (개방-폐쇄)** — 기존 타입 수정 없이 확장 가능해야 한다. 위젯은 `Widget` 상속으로 추가한다. 렌더러가 `snapshot.playerX/Y`처럼 특정 게임 개념을 하드코딩하지 않게 하고, 그릴 대상은 스냅샷의 균일한 primitive 배열(`Quad`/`SpriteDraw`)로만 받는다.
- **LSP (리스코프 치환)** — `Widget` 파생 타입은 기반 계약(로컬 좌표 사용, 이벤트 소비 시 `true` 반환, `parentOrigin` 기준 배치)을 어기지 않는다.
- **ISP (인터페이스 분리)** — 크고 뚱뚱한 인터페이스를 만들지 않는다. `docs/ui-architecture.md`의 `UIRenderer`(DrawFilledRect/DrawText/PushClipRect만)가 목표 형태다. 위젯에 렌더 백엔드 전체를 노출하지 않는다.
- **DIP (의존성 역전)** — 상위 모듈은 구현이 아니라 추상에 의존한다. `game::Application`은 `render::IRenderer`(`Start/SetFrameSettings/Submit/Resize/Stop`)에만 의존하고, `main.cpp`만 `Dx11Renderer` 구상 타입을 안다 → DX12 교체 시 `main.cpp` 한 줄. `platform::Win32Window`는 `IWindowEventSink`로 이벤트를 되돌려주고 `Application`이 그것을 구현한다. `Simulation`은 `InputState`를 모르고 `PlayerIntent` 값을 받는다. UI가 `Quad` 방출에만 의존하는 것도 같은 원칙.

추가로: `= delete`로 복사 방지, 소유권은 `unique_ptr`, raw 포인터는 비소유 관찰용, 가상 소멸자 유지, 가능한 곳에 `const`·`[[nodiscard]]`. 상속보다 합성을 우선하되 다형성이 필요한 곳(위젯, 렌더러 추상)에서는 인터페이스를 쓴다.

## 최우선 작업 이력 (1~6 완료 — 위 설계 원칙을 지키며)

1. ~~**`JobSystem::WorkerLoop`의 예외 안전성**~~ — 완료. `job.run()`을 try/catch로 감싸 예외를 `JobFence::State::error`(`exception_ptr`)에 보관, fence는 항상 감소, `JobFence::Wait()`에서 첫 예외 재전파. `RecommendedWorkerCount()`도 `core`로 이동.
2. ~~**알파 블렌딩 활성화**~~ — 완료. `Dx11Renderer`에 `ID3D11BlendState`(SrcAlpha/InvSrcAlpha, straight alpha) 생성 + `Render`에서 `OMSetBlendState`. `a < 1.0` UI 색이 이제 블렌딩된다.
3. ~~**`.gitignore` 추가**~~ — 완료. `x64/`·`.vs/`·`*.user`·빌드 산출물 무시. 커밋돼 있던 `x64/`는 `git rm --cached`로 추적 해제.
4. ~~**시뮬레이션 결과를 스냅샷으로 연결**~~ — 부분 완료. `SnapshotBuilder`가 `Particle` → `RenderSnapshot::worldQuads`. 앞 2,048개만 그리고 나머지 20k advect는 **JobSystem 처리량 스텁**으로 코드에 명시(`Simulation::Step`, `SnapshotBuilder::kVisibleParticleSample`). `simulatedSpriteCount` 데드 코드 제거. 텍스처 `SpriteDraw`는 로드맵 2/3단계.
5. ~~**UI 입력 소비 반환**~~ — 완료. `UIContext::PointerXxx`가 `bool` 반환. `Application::OnMouseButton`이 UI가 소비한 좌클릭을 `InputState`로 전달하지 않음(press/release 대칭 라우팅).
6. ~~**`Game` god class 분해 (SRP/DIP)**~~ — 완료. 위 "설계 원칙 — SRP/DIP" 항목 참조. `Game` 제거, `Application`이 조립만.

### 다음 후보 (측정/필요 시)

- 텍스처 `SpriteDraw` + SpriteBatch(dynamic VB + atlas) — 로드맵 3.
- 시뮬/렌더 파이프라이닝·더블 버퍼링(현재 매 프레임 `ParallelFor(...).Wait()` 완전 블록).
- 고정 timestep 물리/애니메이션 잡 (`FixedTimestep`은 준비됨) — 로드맵 5.
- UI: `Measure`/`Arrange`, `VerticalStack`, `PushClipRect`, `UIScreen` 전환.
- frame-time HUD / profiler — 로드맵 6.

## 알려진 소소한 이슈

- ~~`UI.cpp` `AddText`의 `Glyph` 문자당 7회 재계산~~ — 완료. 문자당 1회로 hoist.
- ~~`Dx11Renderer` `m_frameSettings` 프레임당 2회 락 읽기~~ — 완료. `RenderLoop`에서 1회 읽어 `Render(snapshot, settings)`로 전달.
- ~~`IDXGIFactory::MakeWindowAssociation` 미호출~~ — 완료. `DXGI_MWA_NO_ALT_ENTER`로 Alt+Enter를 앱이 소유.
- 스왑 효과가 레거시 `DXGI_SWAP_EFFECT_DISCARD`. Win10+는 `FLIP_DISCARD` 권장 — SpriteBatch 단계에서 함께 전환.
- 메인 스레드가 `ParallelFor(...).Wait()`로 매 프레임 완전 블록. 시뮬/렌더 파이프라이닝·더블 버퍼링 없음.
- UI 클리핑(`PushClipRect`) 미구현. `TextLine`이 창 밖으로 넘칠 수 있음. UI 문서가 코드보다 앞서 있다(`Measure`/`Arrange`/`VerticalStack`/`UIScreen` 등 미구현).
- `InputState`: 한 프레임 안에서 같은 키가 down→up 하면 `KeyPressed`/`KeyReleased` 둘 다 참(의도됨), 단 최종 held 상태만 다음 프레임에 남는다.

## 코드 스타일

- 네임스페이스 `engine::core` / `engine::render` / `engine::ui`.
- 멤버 변수 `m_camelCase`, 타입 `PascalCase`, 지역/파라미터 `camelCase`.
- 주석은 "왜"를 적는다. 주변 코드의 주석 밀도에 맞춘다. README·docs는 한국어+영어 혼용.
