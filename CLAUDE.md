# CLAUDE.md

C++20 / Win32 / DirectX 11 기반 2D 게임 엔진 뼈대. 이 파일은 세션마다 자동 로드되는 상시 지침이다.

## 작업 방식 (사용자는 명령으로만 운전한다)

사용자는 모든 변경을 명령으로 지시하고, 판단할 수 있을 만큼만 보길 원한다. 코드 설명·장문 금지.

- 명령이 오면 **먼저 `docs/command-playbook.md`에서 해당 행**(트리거 → 절차 → 판단지점 → 파일)을 찾아 실행한다.
- 응답은: ① 바뀐 것 1~3줄 ② 판단 필요 시 옵션 A/B(권장안 먼저), 아니면 결정하고 한 줄로 밝힘 ③ 빌드 결과 `경고 N / 오류 N` ④ 커밋은 명시 요청 시에만.
- "사용법/확장법" 지식은 채팅이 아니라 `CLAUDE.md` · `docs/*` · memory에 남긴다.
- **설계(구조·모듈·시스템 문서화)를 하면 반드시 그 문서에 "사용 방법(How to use)" 항목을 남긴다.** 붙이는 법·확장하는 법·하지 말 것을 예시 코드와 함께. 설계만 하고 사용법을 안 적는 것 금지. 기존 예: `docs/time-design.md`, `docs/collider-design.md`.
- 문서를 고친 뒤에는 `tools\check_docs.ps1` 로 링크·§ 참조·삭제된 이름 잔존을 점검한다(아래 "빌드 / 실행").

## 지금 무슨 브랜치인가 (세션 시작 / 컨텍스트 압축 뒤 여기부터)

- 브랜치 **`circular`** = 2D 뱀서 라이크 **"서큘러"** 개발. 앱은 **곧장 Circular 씬으로 부팅**한다(타이틀 화면 없음). ESC → 설정 → **LOBBY** 로 로비 메뉴에 돌아갈 수 있다 — **2026-09 부터 로비는 GAME(Circular 정상)·TEST SCENE(99999 HP 더미 1마리, 무기 테스트용) 딱 두 버튼뿐**, 3D 디펜스 등 다른 씬은 어떤 메뉴에서도 안 뜬다(코드는 있음 — `Application::EnterInGame(DemoScene::N)` 직접 호출만 가능).
- **기준(헌법)은 `docs/# Circular 기초 설계.md`** — 수정·위반 금지(사용자가 씀). 그 밑에 사용자 **[확정]**, 그 밑에 설계서의 **[살]**(제안). 충돌하면 먼저 사용자에게 묻는다.
- **가장 먼저 `docs/circular-design.md` 의 "현재 위치" 블록**을 읽는다(구현 상태·다음 할 일·결정 대기 — M1·M2 뼈대·M5 완료, 다음은 M3/M4). 마일스톤을 끝내면 그 블록·격차표(§9)·`docs/roadmap.md` 서큘러 트랙을 함께 갱신한다.

## 빌드 / 실행

- Visual Studio 2022로 `CppWindowGame.vcxproj`를 연다. `Debug | x64` 선택 후 `F5`.
- 툴셋 v143, `LanguageStandard=stdcpp20`, `WarningLevel=Level4`. 링크: `d3d11.lib;dxgi.lib;d3dcompiler.lib`. 인클루드 루트는 `src`.
- **CLI 빌드**(저장소 루트, PowerShell): `& "C:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe" CppWindowGame.vcxproj /p:Configuration=Debug /p:Platform=x64 /m /nologo /v:m /clp:"Summary;WarningsOnly;ErrorsOnly"` → 출력의 `경고 N개 / 오류 N개` 를 그대로 보고한다. 증분 빌드는 몇 초.
- **스모크 테스트**: `x64\Debug\CppWindowGame.exe` 를 작업 디렉터리=저장소 루트로 실행 → 수 초 생존 확인 → `CloseMainWindow`. 종료 시 저장소 루트에 **`settings.cfg` 가 생긴다**(추적 안 되는 실행 산출물 — 지운다). 이 환경은 GPU 없이 WARP 폴백이어도 정상.
- 도구: `tools\run_balance_sim.bat`(서큘러 밸런스 시뮬레이터, `docs/circular-balance.md`), `tools\build_atlas_pack.bat` → `build\tools\atlas_pack.exe`(이미지 패킹, `docs/circular-art-guide.md`), `tools\check_docs.ps1`(문서 점검). 산출물은 `build/`(gitignore).
- 별도 테스트 프로젝트·CI 없음. 로직 검증이 필요하면 2D 전용 하네스를 스크래치에서 `cl /DENGINE_WITH_2D …` 로 컴파일해 `Simulation` 을 창 없이 돌린다(`tools/balance_sim.cpp` 가 예시).

### 환경 함정 (실제로 겪은 것)

- **줄바꿈**: `.vcxproj` 와 일부 소스는 CRLF 다. **Git Bash `sed -i` 는 CRLF 를 LF 로 바꿔 버린다** → 파일 편집은 Edit 도구 또는 PowerShell(`[IO.File]::ReadAllText` → 치환 → CRLF 유지해 `WriteAllText`, UTF-8 BOM 없음)로 한다. 문서(`docs/*.md`)는 UTF-8 BOM 없음.
- 새 `.cpp/.h` 는 **`CppWindowGame.vcxproj` 에 등록**해야 빌드된다(`<ClCompile>`/`<ClInclude>`). 삭제할 땐 등록도 같이 뺀다.
- PowerShell 도구에서 `Remove-Item`/`git rm` 이 "보호 경로" 오탐으로 막힐 수 있다 → Bash 의 `git rm`/`rm` 을 쓴다.
- Python/Node 가 없다 → 일회성 스크립트는 PowerShell 로.
- 파일을 Edit 하기 전에 그 파일을 Read 해야 한다. 큰 문서는 부분 Read 후에도 Write 가 "수정됨" 으로 막히면 다시 Read.

## 아키텍처 불변 규칙 (절대 깨지 말 것)

1. **D3D11 API 호출은 렌더 스레드에서만** 한다. 메인 스레드(`src/game/Application.cpp`의 프레임 루프)는 입력·시뮬레이션·스냅샷 생성만 한다.
2. 렌더 스레드(`src/render/Dx11Renderer.cpp`)가 device, immediate context, swap chain, back buffer, depth buffer, `ResizeBuffers`, `Present`의 **유일한 소유자**다. 실제 드로우는 `IRenderPass` 목록(`src/render/r2d/*`, `src/render/r3d/*`)이 하고, 렌더러 코어는 clear·bind·pass 순회 + `ShaderLibrary` 소유만 한다. 새 패스는 `RenderPass.h` 구현 + `main.cpp`에서 `AddRenderPass`(Start 전). **셰이더는 `assets/shaders/<name>.hlsl` + `Initialize(device, ShaderLibrary&)`에서 `shaders.Get(...)`; 인라인 `D3DCompile` 금지. `.hlsl`/`.hlsli`는 커밋 소스, 실행 중 편집하면 핫리로드.** 세부 `docs/shader-pipeline.md`.
3. 스레드 경계는 값 기반 `RenderSnapshot`만 넘어간다(`Quad`·`MeshDraw`·`CameraView` 모두 값, `Mat4` 포함). 가변 게임 객체 포인터를 넣지 않는다.
4. 렌더러는 최신 스냅샷 1개만 보관한다(1슬롯 메일박스). 오래된 미렌더 프레임은 버린다.
5. 창 resize 요청은 메인에서 전달하되 `ResizeBuffers`는 렌더 스레드만 호출한다.
6. `JobSystem::ParallelFor`의 각 잡은 겹치지 않는 연속 `[begin, end)` 범위만 쓴다. 워커 안에서 공유 카운터 증가·`vector` 재할당·엔티티 생성/파괴 금지. `JobFence::Wait()`는 프레임 단계 경계에서만 쓴다.
7. **2D/3D는 별도 모듈이다.** `math/Math2D.h`↔`Math3D.h`, `render/r2d/`↔`render/r3d/`, `physics/p2d/`↔`physics/p3d/`는 서로 `#include` 하지 않는다. 공유는 각 계층의 core(`math` 공통, `render/RenderPass.h`·`IRenderer.h`, `physics/Collision.h`)로만. 3D 코드는 `ENGINE_WITH_3D` 프리프로세서로 감싸 없으면 빌드에서 완전 제외된다(패스 미등록, 스냅샷에 `scene3d` 없음, `.cpp` 본문 `#if`로 비움). 2D는 baseline(`ENGINE_WITH_2D`, UI가 의존). **서큘러는 2D baseline 만 쓰고, `Simulation::Step`/`SnapshotBuilder::Build`/`Application::Run` 의 3D 블록은 `ActiveScene() != DemoScene::Circular` 런타임 가드로 분리한다** — 3D 전용 코드를 추가할 때 가드를 빠뜨리지 말 것.
8. 충돌은 **탐지만**. `CollisionWorld*::Step()`은 콜라이더를 움직이지 않는다. 응답(밀어내기·물리)은 이 모듈 밖. `CollisionWorld`는 메인 스레드(시뮬)만 만진다. 세부는 `docs/collider-design.md`.

9. 서드파티는 `src/vendor/<lib>/`에 소스 vendor + 그 라이브러리 `LICENSE` 동봉, 상업 이용 가능한 permissive/PD 라이선스만. 구현 TU 는 per-file 경고 off(`vcxproj` `TurnOffAllWarnings`). 벤더 타입은 그걸 쓰는 `.cpp` 안에만 — 밖으로는 엔진 타입만.
   - `ufbx` — FBX 로더, MIT/PD, v0.23.0. `ufbx.c` 는 C++ 로 컴파일. 타입은 `src/import/ModelImporter.cpp` 안에만. 세부 `docs/model-animation-research.md`.
   - `stb` — `stb_image.h` 이미지 디코더, MIT/PD(Unlicense), v2.30. 구현은 `src/vendor/stb/stb_image_impl.cpp`(`STB_IMAGE_IMPLEMENTATION` 유일 정의, `STBI_NO_STDIO` + PNG/JPEG/BMP/GIF/TGA 만). 엔진 진입점은 `import::LoadImageFromFile(path)` → `import::ImageData`(RGBA8 top-down straight-alpha). `.tga` 는 자체 `LoadTga`, 그 외는 stb. stb 타입은 `src/import/ImageFile.cpp` 안에만. **이미지 디코드·색공간·dev/ship 경계 규칙은 `docs/image-assets.md` 가 계약** — `stb_*` 를 `ImageFile.cpp` 밖에서 직접 부르지 않는다.

렌더러 코어는 **멀티샘플 씬 타깃**(`m_sceneColorRtv`/`m_sceneDepthDsv`, 최대 8x)에 그리고 프레임 끝에 백버퍼로 resolve한다. 패스는 백버퍼가 아니라 씬 타깃에 그린다. 세부 `docs/msaa.md`.

3D 조명은 `Scene3D::lighting`(값 타입, `render/r3d/Lighting.h`) → `Frame` cbuffer(b0). C++ `FrameConstantsGpu` 와 `common3d.hlsli` 의 `cbuffer Frame` 레이아웃은 항상 같이 고친다. 패스마다 `FrameConstants` 재정의 금지 — `render/r3d/FrameConstants.h` 공유.

## 문서 지도 (`docs/`) — 여기엔 한 줄씩만, 상세는 각 문서

**서큘러 (이 브랜치의 주제)**

| 문서 | 내용 |
|---|---|
| `# Circular 기초 설계.md` + `images/HUD.png` | **헌법.** 장르·규칙·스테이지·캐릭터·UI·적·스폰. 수정 금지 |
| `circular-design.md` | **먼저 읽기.** "현재 위치" 스냅샷, 태그 [기초]/[확정]/[살]/[미정], 격차표, 개발 순서 M1~M8, 결정 기록(§12), 종류 늘리는 법 |
| `circular-balance.md` | 몹 수·XP·레벨·이동/스태미너·능력치·캐릭터 **CSV 밸런싱 환경**(`assets/data/circular/`, F5/F6/F7, 시뮬레이터) + 앞으로 늘릴 CSV 표 계획 |
| `circular-art-guide.md` | **이미지 추가 절차·파일 이름 규칙·이미지 사양·애니메이션 설계** (패킹은 지금 됨, 월드 스프라이트 표시는 M7) |
| `circular-combat.md` | 무기·적 공격의 **판정 도형·연출·CSV** 를 한 구조로 묶는 설계 작업 목록 + 움직임 패턴(궤도·소용돌이·럴커 = 경로×판정 시점 조합)(W0~W13, 결정 대기 D1~D7) — 구현 전 |

**엔진 공통**

| 문서 | 내용 |
|---|---|
| `engine-overview.md` | 모듈 지도·프레임 흐름·확장 지점 |
| `engine-conventions.md` | 축·단위·시간·텍스처·LOD·애니 구조 — **불변값 한 곳** (2D: +Y 아래, 입력 축 반전 한 번만) |
| `roadmap.md` | **"다음에 뭘"의 단일 소스**(우선순위) — 서큘러 트랙 포함 |
| `command-playbook.md` | 명령 → 처리 절차 표(트리거·판단지점·파일) |
| `multithreaded_game_engine_architecture.md` | 스레드 계약 |
| `scene-flow-design.md` | Menu/InGame/Settings 상태(타이틀 없음), 모달 오버레이 패턴, ESC/핫키 |
| `ui-architecture.md`, `game-settings.md` | 위젯 계층·`UIContext`, 설정 카탈로그(볼륨은 적용, 마우스 감도/반전은 값만 저장) |
| `scrollable-list-and-pool.md` | `ui::ScrollList`(현재 사용처 없음), `core::ObjectPool<T>` |
| `time-design.md` | 고정 스텝 + 전역/개체별 time scale, 레벨업 정지는 별개 메커니즘 |
| `collider-design.md` | 탐지 전용 충돌(2D/3D), 3D 그리드 브로드페이즈, 레이캐스트 |
| `entity-lifecycle-design.md` | 엔티티 식별·생존주기, 저장 전략 §3A/§3B |
| `audio-design.md` | XAudio2 마스터+music/sfx, 스트리밍·voice 풀링 |
| `image-assets.md`, `texture-atlas-and-sprite-pass.md`, `atlas-build-pipeline.md` | 이미지 디코드 계약 / 아틀라스 런타임·`SpriteDraw`·scissor / `tools/atlas_pack` v1 |
| `animation-design.md` | 2D/3D 애니메이션 통합 설계(2D 프레임 애니메이션 = 설계만) |
| `shader-pipeline.md` | 셰이더 로딩·핫리로드 |
| `loading-and-streaming.md` | 로딩 커튼·비동기 에셋 — 설계만(`GameState::Title` 은 현재 `Menu`) |

**3D 그래픽 / 디펜스 (별도 씬 — 서큘러와 무관, 로비 메뉴엔 버튼 없음 — `Application::EnterInGame(DemoScene::N)` 코드 호출로만 진입)**

| 문서 | 내용 |
|---|---|
| `demo-scene.md`, `synopsis.md` | 씬 선택(`DemoScene` 열거형: CharacterDemo·DefenseCombat·ShadowShowcase·EffectsTest·Circular) / 3D 디펜스 시놉시스 |
| `defense-combat-design.md`, `horde-design.md` | 무한 웨이브 디펜스(전투 60초/정비 60초, 무기 5종) 구현 완료 / 좀비 호드(설계만) |
| `instanced-rendering.md` | 수천 개체 인스턴스드 렌더(+FBX 크라우드, 1클립 VAT) 구현 |
| `model-animation-research.md` | FBX/스키닝(CPU 스키닝, GPU 스키닝은 설계만) |
| `toon-rendering.md`, `toon-fresnel-research.md`, `lighting.md`, `light-types-design.md`, `msaa.md`, `shadows.md` | 셀·아웃라인·프레넬 / 조명 / 포인트·스팟(설계만) / AA / 2캐스케이드 그림자 |
| `particle-system-research.md`, `post-process-gbuffer-research.md` | 3D 파티클(`ParticlePass3D`) / 포스트프로세스·G-버퍼·SSAO |

## 설계 원칙 — 최우선 (모든 신규/수정 코드에 적용)

객체지향 설계와 SOLID를 다른 모든 작업보다 우선한다. 아래 "최우선 작업"도 이 원칙을 지키는 방식으로 구현한다.

- **SRP (단일 책임)** — 클래스 하나는 변경 이유가 하나여야 한다. 예전 `main.cpp`의 `Game` god class는 `platform::Win32Window` · `input::InputState` · `game::Simulation` · `game::SnapshotBuilder` · `game::Application`(조립·조율만)으로 분해됨. `main.cpp`는 진입점 한 함수. 새 코드도 이 경계를 지킨다.
- **OCP (개방-폐쇄)** — 기존 타입 수정 없이 확장 가능해야 한다. 위젯은 `Widget` 상속으로 추가한다. 렌더러가 `snapshot.playerX/Y`처럼 특정 게임 개념을 하드코딩하지 않게 하고, 그릴 대상은 스냅샷의 균일한 primitive 배열(`Quad`/`SpriteDraw`)로만 받는다. **종류가 늘어나는 것(몹 행동·스폰 패턴·무기 효과·스테이지 흐름)은 열거+레지스트리 또는 인터페이스+테이블 — 종류별 if 사슬 금지.**
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

### 다음 후보

**단일 소스는 `docs/roadmap.md`** — 여기 별도 목록을 유지하지 않는다(두 곳이 따로 갱신되며 드리프트난 전례가 있었음). "다음에 뭘"은 그 문서의 §1 현재 상태 표 + §3 우선순위 표를 본다. 서큘러는 `circular-design.md` "현재 위치"와 `roadmap.md` "서큘러 트랙".

## 알려진 소소한 이슈

- ~~`UI.cpp` `AddText`의 `Glyph` 문자당 7회 재계산~~ — 완료. 문자당 1회로 hoist.
- ~~`Dx11Renderer` `m_frameSettings` 프레임당 2회 락 읽기~~ — 완료. `RenderLoop`에서 1회 읽어 `Render(snapshot, settings)`로 전달.
- ~~`IDXGIFactory::MakeWindowAssociation` 미호출~~ — 완료. `DXGI_MWA_NO_ALT_ENTER`로 Alt+Enter를 앱이 소유.
- 스왑 효과가 레거시 `DXGI_SWAP_EFFECT_DISCARD`. Win10+는 `FLIP_DISCARD` 권장 — SpriteBatch 단계에서 함께 전환.
- 메인 스레드가 `ParallelFor(...).Wait()`로 매 프레임 완전 블록. 시뮬/렌더 파이프라이닝·더블 버퍼링 없음.
- UI 폰트는 5×7 비트맵(A-Z, 0-9, `: - . %` 만) — **한글 불가**. 라벨은 영문·숫자. 글리프 아틀라스(`docs/texture-atlas-and-sprite-pass.md` §1.3)가 선행 조건.
- UI 클리핑(`PushClipRect`) 미구현. `TextLine`이 창 밖으로 넘칠 수 있음. `Measure`/`Arrange`/`VerticalStack`은 여전히 미구현(좌표를 손으로 배치, 기존 화면은 1280×720 고정 좌표 — HUD 는 앵커/스케일이 필요, `circular-design.md` §7.3). 화면 전환(`UIScreen`)은 구현됨 — `UIContext::SetScreen`/`SetOverlay`, `docs/scene-flow-design.md`.
- `ui::Slider`에 진짜 입력 캡처가 없다 — 드래그 종료(`PointerUp`)가 형제 위젯에 먼저 소비되면 그 프레임엔 드래그가 안 풀릴 수 있음(수직 스택처럼 위젯이 안 겹치면 발생 안 함). `docs/ui-architecture.md` "각 위젯의 책임" 참고.
- 버튼 콜백 안에서 자기 오버레이/화면을 지우면 호출 중인 위젯이 파괴된다 — 레벨업 모달처럼 **선택만 기록하고 다음 프레임에 적용**한다(`Application::ServiceLevelUp`).
- 설정 항목 중 볼륨(마스터/음악/효과음)은 `AudioEngine` 에 실제로 적용된다(`docs/audio-design.md`). 마우스 감도/반전은 마우스 카메라(`Simulation::UpdateCameraLook`)가 있는데도 아직 안 읽음(자기 `kMouseSensitivity` 상수만 씀) — 값만 저장된다. `docs/game-settings.md` §1.
- `InputState`: 한 프레임 안에서 같은 키가 down→up 하면 `KeyPressed`/`KeyReleased` 둘 다 참(의도됨), 단 최종 held 상태만 다음 프레임에 남는다.
- **캐릭터 애니메이션은 GPU 스키닝이 아니라 CPU 스키닝이다.** `ModelMeshPass3D`가 매 프레임 본 팔레트로 LBS를 CPU에서 계산해 DYNAMIC 정점 버퍼에 `Map/Unmap`. 셰이더(`cel.hlsl`/`outline.hlsl`)엔 스킨 관련 코드가 전혀 없다 — `assets/shaders/`에 bone/skin 관련 HLSL 없음(확인됨). GPU 스킨(새 `SkinnedMeshPass3D` + `StructuredBuffer` 본 팔레트, `docs/model-animation-research.md` §5.2)은 여전히 설계만이고, 인스턴스를 여럿(각자 다른 애니메이션) 세우려면 그게 필요하다. 실제 구현은 §5.2a.

## 코드 스타일

- 네임스페이스 `engine::core` / `engine::render` / `engine::ui`(게임 코드는 `engine::game`).
- 멤버 변수 `m_camelCase`, 타입 `PascalCase`, 지역/파라미터 `camelCase`.
- 주석은 "왜"를 적는다. 주변 코드의 주석 밀도에 맞춘다. README·docs는 한국어+영어 혼용.
