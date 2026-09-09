# 씬 흐름 설계 (Scene Flow) — Title / InGame / Settings

플레이어가 앱을 여는 순간부터 게임 화면까지 거치는 상태(페르소나) 구조. 설계 + 구현 완료.

관련 문서: `docs/ui-architecture.md`(위젯/`UIScreen`), `docs/game-settings.md`(설정값 카탈로그), `docs/time-design.md`(고정 스텝).

---

## 1. 상태

```cpp
enum class GameState { Title, InGame };
```

`game::Application`이 `m_state`로 소유한다. **Settings는 별도 상태가 아니다** — `ui::UIContext`의 모달 오버레이(`SetOverlay`/`ClearOverlay`)로 Title 위에도 InGame 위에도 얹을 수 있다("타이틀 화면에서, 인게임 화면에서 설정창을 열어" 요구사항이 그대로 이 구조다). 오버레이가 있으면:

- 포인터 입력은 오버레이로만 간다(아래 화면은 보이지만 반응 안 함).
- `Simulation::Step`을 건너뛴다 — Title이든 InGame+Settings든 "일시정지"로 취급.

향후 Loading/Paused/Result 등이 필요하면 `GameState`에 값만 추가하면 된다(OCP — `Application::Run`의 스텝 게이팅 조건, 화면 전환 함수만 늘어난다).

```text
        시작
         │
         ▼
     ┌───────┐   START    ┌────────┐
     │ Title │ ─────────► │ InGame │
     └───┬───┘             └───┬────┘
         │  SETTINGS            │  SETTINGS / ESC
         ▼                      ▼
     ┌─────────────────────────────┐
     │   Settings (모달 오버레이)     │  ← 어느 화면 위에도 얹힘, CLOSE/ESC로 복귀
     └─────────────────────────────┘
```

## 2. `Application`이 조율하는 방식

```cpp
void Application::EnterTitle()
{
    m_state = GameState::Title;
    m_ui.ClearOverlay();
    m_ui.SetScreen(BuildTitleScreen(
        [this] { EnterInGame(); },
        [this] { OpenSettings(); },
        [this] { m_window.RequestClose(); }));
}

void Application::EnterInGame()
{
    m_state = GameState::InGame;
    m_ui.ClearOverlay();
    m_ui.SetScreen(BuildInGameHud([this] { OpenSettings(); }));
}
```

`BuildTitleScreen`/`BuildInGameHud`/`BuildSettingsScreen`은 전부 `std::unique_ptr<ui::Widget>`를 반환하는 자유 함수(각각 `game/TitleScreen.*`, `game/InGameHud.*`, `game/SettingsScreen.*`)다 — `UIContext`가 "타이틀이 뭔지 세팅 화면이 뭔지" 알 필요가 없다(렌더러가 `playerX`를 모르는 것과 같은 원칙). `Application`이 콜백(`std::function<void()>`)을 주입해 DIP를 지킨다.

`Run()`의 스텝 게이팅:

```cpp
if (m_state == GameState::InGame && !m_ui.HasOverlay())
    for (int step = 0; step < steps; ++step)
        m_simulation.Step(m_timestep.Step(), intent);
```

`m_timestep.Advance(delta)`는 이 조건과 무관하게 항상 호출한다 — 내부 누적기가 알아서 백로그를 버리므로(`FixedTimestep::Advance`, `docs/time-design.md`) 정지 중에도 안전하다. 정지 중엔 스냅샷만 계속 만들어 제출한다(UI가 매 프레임 다시 그려져야 하므로).

## 3. ESC 키

`Application::OnKey`가 `VK_ESCAPE`를 가로챈다: 오버레이가 열려 있으면 닫고, InGame이고 오버레이가 없으면 Settings를 연다. Title에서 ESC는 아무 일도 안 한다(종료는 QUIT 버튼으로만 — 실수로 창이 닫히는 것을 막는다).

## 4. 해상도 변경이 실제로 창을 바꾸는 경로

```
SettingsScreen(NEXT 버튼) → Settings.resolutionIndex 갱신
  → Application::ApplyResolution()
    → Win32Window::RequestResize(w, h)   [메인 스레드, SetWindowPos]
      → WM_SIZE (동기)
        → IWindowEventSink::OnResize → Application::OnResize
          → m_pendingResize 저장, 다음 프레임 top에서:
            → IRenderer::Resize(w, h)      [렌더 스레드가 실제 ResizeBuffers]
            → Simulation::SetWorldSize(w, h)
```

기존 "사용자가 창 테두리를 드래그" 경로와 완전히 동일한 파이프라인을 탄다 — 새 코드 경로를 만들지 않고 기존 리사이즈 처리를 재사용했다(OCP).

## 5. 종료 파이프라인

앱을 닫는 세 경로(Title의 QUIT 버튼 → `Win32Window::RequestClose`, 창 X 버튼, Alt+F4) 전부 같은 Win32 시퀀스로 모인다: `WM_CLOSE` → `IWindowEventSink::OnClose`(`Application::OnClose`) → (반환 후) `DefWindowProcW`가 `DestroyWindow` → `WM_DESTROY` → `PostQuitMessage` → 다음 프레임 `PumpMessages`가 `false`를 반환 → `Application::Run`의 루프가 끝난다.

무엇이, 어디서 정리되는지:

| 무엇 | 언제 | 어떻게 |
|---|---|---|
| 설정 저장 | `Application::OnClose` | `m_settings.Save(...)` — 슬라이더/체크박스가 `m_settings`는 이미 실시간으로 갱신해 두므로, 여기선 디스크에 쓰기만 하면 된다. Settings 오버레이를 정식으로 안 닫고 창을 바로 닫아도 반영됨(전엔 안 됐음 — 이번에 고침) |
| 렌더 스레드·GPU 리소스 | `Run()`이 반환한 뒤 `Application` 소멸 시 `m_renderer`(참조라 소유 안 함 — 실제 소유자는 `main.cpp`의 `Dx11Renderer` 지역 변수)가 소멸하며 `Stop()`: 패스 `Release()` → 셰이더/섀도우/씬 타깃 해제 → 백버퍼/스왑체인/컨텍스트/디바이스 `Release()`, 전부 렌더 스레드 안에서 | 이미 완비돼 있었음(`Dx11Renderer::RenderLoop` 꼬리, `~Dx11Renderer` → `Stop()`) — 이번에 손댄 곳 아님 |
| JobSystem 워커 스레드 | `Application` 소멸 시 `m_jobs` 소멸자 | `m_running=false` + `notify_all` + 전체 `join` — 이미 완비 |
| OS 창(HWND) | `Application` 소멸 시 `m_window` 소멸자 | `DestroyWindow`(아직 안 지워졌으면) — 이미 완비 |

**이번에 고친 건 "설정 저장" 한 줄뿐이다** — 나머지(렌더러/JobSystem/창)는 이미 RAII로 안전하게 정리되고 있었다(확인만 했고 설계 변경 없음).

## 6. 사용 방법 (How to use)

### 새 화면 추가하기

1. `src/game/<Screen>.h/.cpp`에 `std::unique_ptr<ui::Widget> Build<Screen>(콜백들...)` 자유 함수를 만든다. `ui::UIWindow`/`Button`/`CheckBox`/`Slider`/`TextLine`을 조합한다(`docs/ui-architecture.md`).
2. `Application`에 그 화면으로 들어가는 `EnterXxx()`(전체 화면 교체, `SetScreen`) 또는 열고 닫는 `OpenXxx()`/`CloseXxx()`(오버레이, `SetOverlay`/`ClearOverlay`) 메서드를 추가한다.
3. 필요하면 `GameState`에 새 값을 추가하고 `Run()`의 스텝 게이팅 조건을 갱신한다.

### 새 상태로 전환하는 조건 추가하기

버튼 클릭 콜백 안에서 `EnterXxx()`/`OpenXxx()`를 부르면 된다 — Title의 START 버튼(`this] { EnterInGame(); }`)이 예시. 키 입력 조건(ESC처럼)은 `Application::OnKey`에 추가한다.

### 종료 시 저장할 것 추가하기(세이브 데이터 등)

`Application::OnClose()`에 한 줄 추가한다 — `m_settings.Save(...)`가 예시. 여기서 하는 일은 반드시 **동기·즉시 완료**여야 한다(디스크 쓰기 정도; 네트워크 호출 금지) — `DefWindowProcW`가 이 함수 반환 직후 창을 부수기 시작한다.

### 하지 말 것

- `UIContext`에 "이게 Title 화면이다" 같은 게임 개념을 넣지 않는다 — `SetScreen`/`SetOverlay`는 어떤 위젯 트리든 받는다.
- 오버레이가 열려 있는데 `Simulation::Step`을 부르지 않는다(위 게이팅 조건 유지).
- 화면 전환 함수 밖에서 `m_state`를 직접 대입하지 않는다 — `EnterTitle`/`EnterInGame`이 상태와 화면 트리를 항상 같이 바꾼다는 불변식이 깨진다.
- `OnClose()`에 렌더 스레드나 D3D11을 건드리는 코드를 넣지 않는다 — 이 함수는 메인 스레드(창 프로시저)에서 돈다. GPU 정리는 이미 `Dx11Renderer::Stop()`이 렌더 스레드 안에서 한다(§5).
