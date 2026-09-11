# CLAUDE.md

C++20 / Win32 / DirectX 11 기반 2D 게임 엔진 뼈대. 이 파일은 세션마다 자동 로드되는 상시 지침이다.

## 작업 방식 (사용자는 명령으로만 운전한다)

사용자는 모든 변경을 명령으로 지시하고, 판단할 수 있을 만큼만 보길 원한다. 코드 설명·장문 금지.

- 명령이 오면 **먼저 `docs/command-playbook.md`에서 해당 행**(트리거 → 절차 → 판단지점 → 파일)을 찾아 실행한다.
- 응답은: ① 바뀐 것 1~3줄 ② 판단 필요 시 옵션 A/B(권장안 먼저), 아니면 결정하고 한 줄로 밝힘 ③ 빌드 결과 `경고 N / 오류 N` ④ 커밋은 명시 요청 시에만.
- "사용법/확장법" 지식은 채팅이 아니라 `CLAUDE.md` · `docs/*` · memory에 남긴다.
- **설계(구조·모듈·시스템 문서화)를 하면 반드시 그 문서에 "사용 방법(How to use)" 항목을 남긴다.** 붙이는 법·확장하는 법·하지 말 것을 예시 코드와 함께. 설계만 하고 사용법을 안 적는 것 금지. 기존 예: `docs/time-design.md`, `docs/collider-design.md`.
- CLI 빌드·스모크테스트 절차는 memory `build-and-run` 참조 (이 환경은 GPU 없어 WARP 폴백이 정상).

## 빌드 / 실행

- Visual Studio 2022로 `CppWindowGame.vcxproj`를 연다. `Debug | x64` 선택 후 `F5`.
- 툴셋 v143, `LanguageStandard=stdcpp20`, `WarningLevel=Level4`.
- 링크: `d3d11.lib;dxgi.lib;d3dcompiler.lib`. 인클루드 루트는 `src`.
- 별도 테스트 프로젝트·CI 없음.

## 아키텍처 불변 규칙 (절대 깨지 말 것)

1. **D3D11 API 호출은 렌더 스레드에서만** 한다. 메인 스레드(`src/game/Application.cpp`의 프레임 루프)는 입력·시뮬레이션·스냅샷 생성만 한다.
2. 렌더 스레드(`src/render/Dx11Renderer.cpp`)가 device, immediate context, swap chain, back buffer, depth buffer, `ResizeBuffers`, `Present`의 **유일한 소유자**다. 실제 드로우는 `IRenderPass` 목록(`src/render/r2d/*`, `src/render/r3d/*`)이 하고, 렌더러 코어는 clear·bind·pass 순회 + `ShaderLibrary` 소유만 한다. 새 패스는 `RenderPass.h` 구현 + `main.cpp`에서 `AddRenderPass`(Start 전). **셰이더는 `assets/shaders/<name>.hlsl` + `Initialize(device, ShaderLibrary&)`에서 `shaders.Get(...)`; 인라인 `D3DCompile` 금지. `.hlsl`/`.hlsli`는 커밋 소스, 실행 중 편집하면 핫리로드.** 세부 `docs/shader-pipeline.md`.
3. 스레드 경계는 값 기반 `RenderSnapshot`만 넘어간다(`Quad`·`MeshDraw`·`CameraView` 모두 값, `Mat4` 포함). 가변 게임 객체 포인터를 넣지 않는다.
4. 렌더러는 최신 스냅샷 1개만 보관한다(1슬롯 메일박스). 오래된 미렌더 프레임은 버린다.
5. 창 resize 요청은 메인에서 전달하되 `ResizeBuffers`는 렌더 스레드만 호출한다.
6. `JobSystem::ParallelFor`의 각 잡은 겹치지 않는 연속 `[begin, end)` 범위만 쓴다. 워커 안에서 공유 카운터 증가·`vector` 재할당·엔티티 생성/파괴 금지. `JobFence::Wait()`는 프레임 단계 경계에서만 쓴다.
7. **2D/3D는 별도 모듈이다.** `math/Math2D.h`↔`Math3D.h`, `render/r2d/`↔`render/r3d/`, `physics/p2d/`↔`physics/p3d/`는 서로 `#include` 하지 않는다. 공유는 각 계층의 core(`math` 공통, `render/RenderPass.h`·`IRenderer.h`, `physics/Collision.h`)로만. 3D 코드는 `ENGINE_WITH_3D` 프리프로세서로 감싸 없으면 빌드에서 완전 제외된다(패스 미등록, 스냅샷에 `scene3d` 없음, `.cpp` 본문 `#if`로 비움). 2D는 baseline(`ENGINE_WITH_2D`, UI가 의존).
8. 충돌은 **탐지만**. `CollisionWorld*::Step()`은 콜라이더를 움직이지 않는다. 응답(밀어내기·물리)은 이 모듈 밖. `CollisionWorld`는 메인 스레드(시뮬)만 만진다. 세부는 `docs/collider-design.md`.

9. 서드파티는 `src/vendor/<lib>/`에 소스 vendor + 그 라이브러리 `LICENSE` 동봉, 상업 이용 가능한 permissive/PD 라이선스만. 구현 TU 는 per-file 경고 off(`vcxproj` `TurnOffAllWarnings`). 벤더 타입은 그걸 쓰는 `.cpp` 안에만 — 밖으로는 엔진 타입만.
   - `ufbx` — FBX 로더, MIT/PD, v0.23.0. `ufbx.c` 는 C++ 로 컴파일. 타입은 `src/import/ModelImporter.cpp` 안에만. 세부 `docs/model-animation-research.md`.
   - `stb` — `stb_image.h` 이미지 디코더, MIT/PD(Unlicense), v2.30. 구현은 `src/vendor/stb/stb_image_impl.cpp`(`STB_IMAGE_IMPLEMENTATION` 유일 정의, `STBI_NO_STDIO` + PNG/JPEG/BMP/GIF/TGA 만). 엔진 진입점은 `import::LoadImageFromFile(path)` → `import::ImageData`(RGBA8 top-down straight-alpha). `.tga` 는 자체 `LoadTga`, 그 외는 stb. stb 타입은 `src/import/ImageFile.cpp` 안에만. **이미지 디코드·색공간·dev/ship 경계 규칙은 `docs/image-assets.md` 가 계약** — `stb_*` 를 `ImageFile.cpp` 밖에서 직접 부르지 않는다.

모듈별 상세: `docs/engine-overview.md`(지도), `engine-conventions.md`(축·단위·중력·LOD·텍스처·애니 구조 — 불변값 한 곳), `roadmap.md`(다음에 뭘 — 우선순위 + 애니 재생모드·레이캐스트 미니 설계), `multithreaded_game_engine_architecture.md`(스레드), `ui-architecture.md`, `scrollable-list-and-pool.md`(스크롤 목록 `ui::ScrollList` v1 + 마우스 휠; `core::ObjectPool<T>` 구현 — `src/core/ObjectPool.h`, 슬롯 고정 + `ActiveIndices()` + generation 핸들), `image-assets.md`(이미지 디코드 진입점 `import::LoadImageFromFile` + 색공간/알파 규칙 + dev/ship 경계), `texture-atlas-and-sprite-pass.md`(아틀라스 런타임 + SpriteDraw + scissor 클리핑 — 파이프라인 부분 구현, §7 참조), `atlas-build-pipeline.md`(아틀라스 빌드 — `tools/atlas_pack` v1 구현, 무압축 `.dds`; BC7 압축은 후속), `scene-flow-design.md`(Title/InGame/Settings 씬 상태), `game-settings.md`(설정 카탈로그), `synopsis.md`(게임 시놉시스, 초안), `entity-lifecycle-design.md`(엔티티 식별·생존주기 뼈대), `demo-scene.md`(unity_chan 캐릭터 컨트롤러 + 팔로우 카메라; `Simulation::kDemoScene` 1=로컬배속 액터, 2=절벽 조망 + `SimAgent` 군중을 인스턴스드로 — 수·모델·크기는 `game/CrowdConfig.h` `kActiveCrowd`(`kCrowdBoxes` 600큐브 ↔ `kCrowdZombies` 1500 FBX)), `instanced-rendering.md`(수천 개체 출력 — 인스턴스드 드로우(`MeshInstance`/`InstanceBatch` + `MeshPass3D` 확장) + 프러스텀·거리 컬 + 거리 LOD 2단계 + `core::ObjectPool<SimAgent>` + FBX 크라우드 메시(`MeshId::CrowdModel`) + 디퓨즈 텍스처(§9.6-A) + 1클립 VAT 애니(§9.6-B) + 배치별 셰이더(`InstanceShader`, §9.7) 구현. 크라우드 설정 = `game/CrowdConfig.h` §9.5·§9.7. 빌보드·VAT 확장(법선/셰도우/다중클립)·SoA(`AgentStore`)는 미구현), `horde-design.md`(대규모 좀비 웨이브 — 플로우필드 + VAT, 설계만; 인스턴스 골격은 `instanced-rendering.md`), `loading-and-streaming.md`(로딩 커튼 + 비동기 에셋 로드 + 프리스트림 — 설계만), `time-design.md`(고정 스텝 + 전역/개체별 time scale·일시정지), `collider-design.md`(탐지 + 3D 균일 그리드 브로드페이즈 + 레이캐스트 2D·3D + 데모 씬 2 `Simulation` 연동), `model-animation-research.md`, `animation-design.md`(2D/3D 애니메이션 통합 설계 + 연구 필요 항목), `audio-design.md`(XAudio2 마스터+music/sfx 서브믹스, PCM 원샷/루핑 — 최소 구현), `shader-pipeline.md`(셰이더 로딩), `toon-rendering.md`(셀·아웃라인·크리즈), `lighting.md`(조명), `msaa.md`(AA), `shadows.md`(그림자), `particle-system-research.md`(파티클 시스템 연구 — 총구 이펙트(플래시+연기) + 폭발 버섯구름, 빌보드 인스턴싱 + 새 렌더 패스 `ParticlePass3D` 설계, 연구/설계만·미구현), `post-process-gbuffer-research.md`(포스트프로세싱 인프라 + G-버퍼(노멀/깊이) 연구 — SSAO·스크린스페이스 아웃라인·안개·소프트 파티클의 공통 전제, 오프스크린 RT + MRT + 풀스크린 패스 설계, 연구/설계만·미구현).

렌더러 코어는 **멀티샘플 씬 타깃**(`m_sceneColorRtv`/`m_sceneDepthDsv`, 최대 8x)에 그리고 프레임 끝에 백버퍼로 resolve한다. 패스는 백버퍼가 아니라 씬 타깃에 그린다. 세부 `docs/msaa.md`.

3D 조명은 `Scene3D::lighting`(값 타입, `render/r3d/Lighting.h`) → `Frame` cbuffer(b0). C++ `FrameConstantsGpu` 와 `common3d.hlsli` 의 `cbuffer Frame` 레이아웃은 항상 같이 고친다. 패스마다 `FrameConstants` 재정의 금지 — `render/r3d/FrameConstants.h` 공유.

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

- 3D 파이프라인은 있음: `math`에 `Vec3`/`Mat4`, `MeshPass3D`(depth·원근·Lambert, 내장 큐브·평면), `QuadPass2D`(2D 오버레이), `IRenderPass` 확장 지점. FBX 메시·텍스처·CPU 스키닝·팔로우 카메라 조작(`docs/demo-scene.md`)까지 됨. 다음: 파일 메시 로더 일반화, `MeshPass3D` back-face culling(현재 `CULL_NONE`), 애니메이션 루트 모션·크로스페이드.
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
- UI 클리핑(`PushClipRect`) 미구현. `TextLine`이 창 밖으로 넘칠 수 있음. `Measure`/`Arrange`/`VerticalStack`은 여전히 미구현(좌표를 손으로 배치). 화면 전환(`UIScreen`)은 이제 구현됨 — `UIContext::SetScreen`/`SetOverlay`, `docs/scene-flow-design.md`.
- `ui::Slider`에 진짜 입력 캡처가 없다 — 드래그 종료(`PointerUp`)가 형제 위젯에 먼저 소비되면 그 프레임엔 드래그가 안 풀릴 수 있음(수직 스택처럼 위젯이 안 겹치면 발생 안 함). `docs/ui-architecture.md` "각 위젯의 책임" 참고.
- 설정 항목 중 볼륨(마스터/음악/효과음)은 이제 `AudioEngine` 에 실제로 적용된다(`docs/audio-design.md`). 마우스 감도/반전은 여전히 적용 대상이 없음(마우스 카메라 미구현) — 값만 저장된다. `docs/game-settings.md` §1.
- `InputState`: 한 프레임 안에서 같은 키가 down→up 하면 `KeyPressed`/`KeyReleased` 둘 다 참(의도됨), 단 최종 held 상태만 다음 프레임에 남는다.
- **캐릭터 애니메이션은 GPU 스키닝이 아니라 CPU 스키닝이다.** `ModelMeshPass3D`가 매 프레임 본 팔레트로 LBS를 CPU에서 계산해 DYNAMIC 정점 버퍼에 `Map/Unmap`. 셰이더(`cel.hlsl`/`outline.hlsl`)엔 스킨 관련 코드가 전혀 없다 — `assets/shaders/`에 bone/skin 관련 HLSL 없음(확인됨). GPU 스킨(새 `SkinnedMeshPass3D` + `StructuredBuffer` 본 팔레트, `docs/model-animation-research.md` §5.2)은 여전히 설계만이고, 인스턴스를 여럿(각자 다른 애니메이션) 세우려면 그게 필요하다. 실제 구현은 §5.2a.

## 코드 스타일

- 네임스페이스 `engine::core` / `engine::render` / `engine::ui`.
- 멤버 변수 `m_camelCase`, 타입 `PascalCase`, 지역/파라미터 `camelCase`.
- 주석은 "왜"를 적는다. 주변 코드의 주석 밀도에 맞춘다. README·docs는 한국어+영어 혼용.
