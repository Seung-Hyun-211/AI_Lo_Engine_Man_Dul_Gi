# 설정 카탈로그 (Game Settings)

"무엇을 설정할 수 있는가"만 다룬다 — 화면을 어떻게 그리는지는 `docs/ui-architecture.md`(위젯)·`docs/scene-flow-design.md`(오버레이로 여는 방법) 참고. 데이터·영속화는 `src/core/Settings.h/.cpp`, 화면 구성은 `src/game/SettingsScreen.h/.cpp`.

## 1. 항목

| 필드 | 범위/기본값 | UI | 실제로 적용되는가 |
|---|---|---|---|
| `masterVolume` | 0..1, 기본 1.0 | Slider + `NN%` | ❌ **저장만** — 오디오 서브시스템 자체가 아직 없음 |
| `musicVolume` | 0..1, 기본 1.0 | Slider + `NN%` | ❌ 저장만 |
| `sfxVolume` | 0..1, 기본 1.0 | Slider + `NN%` | ❌ 저장만 |
| `mouseSensitivity` | 0.1..3.0, 기본 1.0 | Slider + `N.NX` | ❌ 저장만 — 마우스로 조작하는 카메라/조준이 아직 없음 |
| `invertMouseY` | bool, 기본 false | CheckBox | ❌ 저장만 (위와 같은 이유) |
| `vsync` | bool, 기본 true | CheckBox | ✅ `IRenderer::SetFrameSettings`로 즉시 적용 |
| `resolutionIndex` | `kResolutionPresets`(1280x720/1600x900/1920x1080) 인덱스, 기본 1(1600x900) | PREV/NEXT 버튼 + 값 표시 | ✅ `Win32Window::RequestResize`로 즉시 적용(`docs/scene-flow-design.md` §4) |

**"저장만"인 항목은 거짓말이 아니라 정직한 상태 표시다** — 값은 UI·파일에 실존하고 다음 세션에도 유지되지만, 그 값을 읽어서 뭔가 하는 코드가 아직 없다(오디오 서브시스템 자체 미구현, 마우스 카메라 미구현). 그 서브시스템이 생기면 `Settings`에서 값만 읽으면 된다 — 설정 인프라를 먼저 깔아둔 것.

**시작할 때부터 적용된다**: `Application`은 `Win32Window`를 만들기 전에 `Settings::LoadOrDefault`부터 하고, 그 `resolutionIndex`로 초기 창 크기를 정한다(생성자에서 `m_settings`가 `m_window`보다 먼저 선언·초기화됨) — 창이 하드코딩된 크기로 열렸다가 나중에야 저장된 해상도와 맞아떨어지는 어긋남이 없다. vsync도 `Run()` 진입 시 `m_settings.vsync`로 `SetFrameSettings`.

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
