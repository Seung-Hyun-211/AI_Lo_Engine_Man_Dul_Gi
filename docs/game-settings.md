# 설정 카탈로그 (Game Settings)

"무엇을 설정할 수 있는가"만 다룬다 — 화면을 어떻게 그리는지는 `docs/ui-architecture.md`(위젯)·`docs/scene-flow-design.md`(오버레이로 여는 방법) 참고. 데이터·영속화는 `src/core/Settings.h/.cpp`, 화면 구성은 `src/game/SettingsScreen.h/.cpp`.

## 1. 항목

| 필드 | 범위/기본값 | UI | 실제로 적용되는가 |
|---|---|---|---|
| `masterVolume` | 0..1, 기본 1.0 | Slider + `NN%` | ✅ `Application::ApplyVolumes` → `AudioEngine::SetMasterVolume` (시작 시 + 슬라이더 실시간, `docs/audio-design.md` §3) |
| `musicVolume` | 0..1, 기본 1.0 | Slider + `NN%` | ✅ `AudioEngine::SetMusicVolume` (music 서브믹스) |
| `sfxVolume` | 0..1, 기본 1.0 | Slider + `NN%` | ✅ `AudioEngine::SetSfxVolume` (sfx 서브믹스) |
| `mouseSensitivity` | 0.1..3.0, 기본 1.0 | Slider + `N.NX` | ❌ 저장만 — 마우스 카메라는 있다(`Simulation::UpdateCameraLook`, `docs/demo-scene.md`)지만 자기 `kMouseSensitivity` 상수를 쓰고 이 값을 안 읽음 |
| `invertMouseY` | bool, 기본 false | CheckBox | ❌ 저장만 (위와 같은 이유 — `UpdateCameraLook` 이 이 필드를 안 읽음) |
| `vsync` | bool, 기본 true | CheckBox | ✅ `IRenderer::SetFrameSettings`로 즉시 적용 |
| `resolutionIndex` | `kResolutionPresets`(1280x720/1600x900/1920x1080) 인덱스, 기본 1(1600x900) | PREV/NEXT 버튼 + 값 표시 | ✅ `Win32Window::RequestResize`로 즉시 적용(`docs/scene-flow-design.md` §4) |
| `frameRateIndex` | `kFrameRatePresets`(60/120/144) 인덱스, 기본 0(60) | PREV/NEXT 버튼 + 값 표시 | ✅ `Application::ApplyFrameSettings` → `IRenderer::SetFrameSettings`(vsync와 같은 호출 — `targetFramesPerSecond`는 vsync가 꺼져 있을 때만 실제로 제한함, `Dx11Renderer`). `Application::Run`의 메인 루프도 vsync 꺼졌을 때 같은 캡으로 자체 페이싱(아래 "카운터가 캡을 넘던 문제" 참고) |

**볼륨 3개는 이제 실제로 적용된다** (`docs/audio-design.md`). 남은 "저장만" 항목(마우스 감도/반전)은 거짓말이 아니라 정직한 상태 표시다 — 값은 UI·파일에 실존하고 다음 세션에도 유지되지만, `Simulation::UpdateCameraLook` 이 이미 있는데도 그 값을 안 읽는다(자기 `kMouseSensitivity` 상수만 씀). 연결은 `UpdateCameraLook` 호출부에 `settings.mouseSensitivity`/`invertMouseY` 를 인자로 넘기기만 하면 된다 — 설정 인프라를 먼저 깔아둔 것(볼륨이 그렇게 살아났다).

**시작할 때부터 적용된다**: `Application`은 `Win32Window`를 만들기 전에 `Settings::LoadOrDefault`부터 하고, 그 `resolutionIndex`로 초기 창 크기를 정한다(생성자에서 `m_settings`가 `m_window`보다 먼저 선언·초기화됨) — 창이 하드코딩된 크기로 열렸다가 나중에야 저장된 해상도와 맞아떨어지는 어긋남이 없다. vsync·프레임레이트도 `Run()` 진입 시 `ApplyFrameSettings()`(`m_settings.vsync`+`frameRateIndex`를 한 번에 `SetFrameSettings`로) — 예전엔 `targetFramesPerSecond`가 60으로 하드코딩돼 있었다.

**카운터가 캡을 넘던 문제** (사용자 신고: 우상단 FPS 표시가 60/120/144 설정보다 높게 나옴,
vsync 켜짐에서도 재현) — `IRenderer::Submit`은 논블로킹 메일박스(불변 규칙 4, 렌더러는
호출자를 기다리게 하지 않음)라 `Dx11Renderer::RenderLoop`의 `nextFrameDeadline` 페이싱은
**렌더 스레드의 Present 속도만** 제한하고, 메인 루프는 그걸 전혀 기다리지 않는다. vsync가
켜져 있어도 그건 렌더 스레드의 Present를 디스플레이 주사율에 맞출 뿐, 메인 루프 자체엔
아무 제약이 없다. 그런데 화면의 FPS 카운터(`m_fpsSmoothed`)는 메인 루프 자신의
`m_clock.Tick()` 델타로 계산 — 즉 캡이 걸리지 않는 쪽의 속도를 표시하고 있었다.
`Application::Run`에 같은 `nextFrameDeadline`/`sleep_until` 페이싱을 vsync 여부와 무관하게
매 프레임 적용해 메인 루프 자체를 항상 캡에 맞춰 재운다 — 이제 카운터가 vsync on/off 모두
선택한 캡을 넘지 않는다. (vsync가 실제 디스플레이 주사율보다 낮은 캡을 골랐다면 Present는
여전히 vblank로 그보다 더 느리게 묶일 수 있음 — 메인 루프 캡은 "이 값 이상은 안 보여준다"는
상한이지, vsync의 실제 페이싱을 대체하진 않음.)

**카운터가 캡의 절반으로 나오던 후속 버그** (사용자 신고) — 위 페이싱 식
`nextFrameDeadline = max(nextFrameDeadline, now()) + frameDuration`가 겉보기엔 맞아
보이지만, 실제 프레임 작업(시뮬 스텝 + 스냅샷 빌드)이 `frameDuration`에 근접하거나
넘어서는 순간부터 문제가 생긴다 — `now()`가 매번 이전 데드라인을 이미 지나 있으므로
`max`는 항상 `now()`를 고르고, 거기에 `frameDuration`을 또 얹어버려서 실제 주기가
`(작업 시간 + frameDuration)`이 됨. 작업 시간이 `frameDuration`과 비슷하면 주기가
`frameDuration`의 약 2배 — 즉 캡의 절반 속도로 나옴. `render/Dx11Renderer.cpp`의 렌더
스레드 쪽 리미터(원본, `Application::Run`이 그대로 복사해 옴)에도 같은 결함이 있었음.
고친 식: `nextFrameDeadline += frameDuration`(항상 **이전 예정 시각** 기준으로 한 틱만
전진) 한 뒤, `now()`보다 뒤처졌을 때만 `nextFrameDeadline = now()`로 재동기화하고
`frameDuration`을 다시 더하지 않음 — 뒤처진 프레임은 그냥 즉시 다음으로 넘어가고,
따라잡은 이후 프레임부터 다시 정상 페이싱. 이러면 카운터는 "캡 이상은 못 넘지만,
하드웨어가 못 따라가면 캡보다 낮게(있는 그대로) 나온다"는 올바른 상한 역할을 함 —
두 파일(`Application.cpp`/`Dx11Renderer.cpp`) 모두 같은 패턴으로 수정.

**검증**: 임시 파일 로깅(`Application::Run` 안, 60프레임마다 `m_fpsSmoothed` 덤프 →
반영 후 삭제)으로 실측. 위 두 수정 전에는 60 cap에서 60~117 사이로 크게 흔들렸음 —
원인은 버그 수정과는 별개로 **Windows 기본 스케줄러 틱(~15.6ms)**이 60/120/144fps의
목표 주기(16.67/8.33/6.94ms)보다 굵어서 `sleep_until`이 다음 15.6ms 틱까지 오버슬립하고,
그다음 프레임이 `nextFrameDeadline` 재동기화로 거의 0초 만에 따라잡으며 번갈아 나타난
것. `winmm`의 `timeBeginPeriod(1)`(`Application::Run` 진입 시 RAII로 걸고 종료 시
`timeEndPeriod(1)`, `CppWindowGame.vcxproj`에 `winmm.lib` 추가)로 OS 타이머 해상도를
1ms로 올려 해결 — 이후 60 cap은 59.9~60.3, 144 cap(vsync on/off 둘 다)은 144~146으로
안정. DefenseCombat 씬(크라우드 500 + 파티클, 실제 부하)에서도 60 cap 기준 59~60.3으로
확인 — WARP(이 개발 환경, GPU 없음)에서도 60fps는 여유롭게 버팀.

전체화면·그래픽 품질(MSAA 샘플 수 등)은 이번에 **포함하지 않았다**: 전자는 DXGI 전체화면 전환, 후자는 디바이스/씬 타깃 재생성이 필요해 검증 없이 넣기엔 리스크가 크다(이 세션은 Windows/D3D11 빌드를 못 함). 다음 후보로 남겨둔다 — 자리(UI 행)는 필요해지면 §1 표에 추가.

## 2. 영속화

파일: `settings.cfg`(작업 디렉터리, `core::kSettingsFilePath`). 포맷은 JSON이 아니라 `key=value` 줄 — 이 엔진은 서드파티 JSON 라이브러리를 vendor 하지 않았고, 필드 7개짜리 평면 구조엔 과하다(YAGNI). 파서는 `Settings::LoadOrDefault`(줄 단위, `=`로 분리, 필드별 `std::stof`/`std::stoi` 안에서 예외를 잡아 그 필드만 기본값 유지) — 파일이 없거나 깨져 있어도 절대 던지지 않는다(시작을 막으면 안 됨).

```text
masterVolume=1
musicVolume=1
sfxVolume=1
mouseSensitivity=1
invertMouseY=0
vsync=1
resolutionIndex=1
frameRateIndex=0
```

저장 시점: Settings 오버레이를 닫을 때(`Application::CloseSettings`) 1회. 값 자체는 슬라이더/체크박스를 만지는 즉시 `core::Settings`(메모리)에 반영되고(그래야 UI가 실시간으로 보여준다), vsync/해상도처럼 실제 부작용이 있는 필드는 그 즉시 적용도 같이 일어난다 — 디스크 저장만 "닫을 때 한 번"으로 미룬 것이다.

## 3. 사용 방법 (How to use)

### 새 설정 항목 추가하기

1. `core::Settings`(`Settings.h`)에 필드 추가 + 기본값.
2. `Settings::LoadOrDefault`/`Save`에 그 필드의 `key=value` 줄 추가.
3. `SettingsScreen.cpp`의 `BuildSettingsScreen`에 행 추가 — 저장만 되는 값이면 `AddSliderRow`/`CheckBox`가 `settings.<field> = v;`만 하는 람다로 충분하다. 실제 부작용이 필요하면 `SettingsScreenActions`에 콜백을 추가하고 `Application`에서 실행부(`ApplyXxx`)를 구현한다.

### 새 서브시스템이 "저장만" 항목을 실제로 쓰게 만들기

해당 값을 읽는 코드(예: 오디오 시스템의 `PlaySound(clip, settings.sfxVolume * settings.masterVolume)`)만 추가하면 된다 — `Settings` 자체나 UI는 안 건드린다. 이게 지금 이 카탈로그를 미리 만들어 둔 이유다.

### 하지 말 것

- `Settings`에 파생 필드(예: `effectiveSfxVolume = sfx*master`)를 저장하지 않는다 — 매번 계산한다. 저장하는 건 사용자가 실제로 고른 값뿐.
- UI 쪽에서 파일 I/O를 직접 하지 않는다 — `SettingsScreen`은 `core::Settings&`만 만지고, 언제 `Save`할지는 `Application`이 결정한다.
- `resolutionIndex`를 화면 밖 임의의 정수로 두지 않는다 — `LoadOrDefault`가 범위를 벗어나면 1로 되돌리는 것과 같은 방어를 새 인덱스형 필드에도 반복한다.
