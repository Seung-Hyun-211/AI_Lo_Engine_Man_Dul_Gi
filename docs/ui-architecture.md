# 2D UI 아키텍처

## 목표와 범위

게임 월드 렌더링과 독립적으로, 게임 화면 위에 패널(`UIWindow`), 버튼(`Button`), 한 줄 텍스트(`TextLine`)를 그린다. 모든 UI는 하나의 `UIContext`가 소유하며, 프레임마다 **입력 처리 → 레이아웃 → 렌더링** 순서로 실행한다.

`NativeWindow`는 Win32의 실제 운영체제 창이고, `UIWindow`는 게임 화면 안에 그리는 패널이다. 두 개념을 반드시 구분한다.

## 계층

```text
Application
 ├─ NativeWindow            Win32 메시지, 클라이언트 크기
 ├─ Input                   마우스/키보드의 이번 프레임 상태
 ├─ Graphics (DX11)         SpriteBatch, font atlas, scissor, draw call
 ├─ Game                    월드 업데이트·월드 렌더링
 └─ UIContext               화면(m_screen) + 모달 오버레이(m_overlay) 슬롯 두 개
     ├─ 화면(Title / InGame HUD / ...)   SetScreen 으로 통째 교체
     │   └─ Widget
     │       ├─ UIWindow    배경, 테두리, 자식 배치 컨테이너
     │       ├─ Button      hover / pressed / click 이벤트
     │       ├─ CheckBox    hover / pressed / checked 이벤트
     │       ├─ Slider      드래그로 값 설정, [min,max]
     │       └─ TextLine    한 줄 텍스트 표시
     └─ 오버레이(Settings 등)             SetOverlay/ClearOverlay, 있으면 입력 독점
```

"UIScreen"이라는 별도 클래스는 없다 — `UIContext`가 화면/오버레이 각각을 `unique_ptr<Widget>` 슬롯 하나로 직접 들고 있다(그 위젯 트리의 루트가 보통 `UIWindow`). 화면 전환은 `docs/scene-flow-design.md`가 `game::Application`에서 어떻게 조율하는지 다룬다.

월드는 먼저 렌더링하고 UI는 마지막에 렌더링한다. 따라서 UI는 항상 월드 위에 표시된다.

## 디렉터리: 목표와 현재

```text
src/
  platform/Win32Window.h    [구현됨] NativeWindow 역할. IWindowEventSink 로 이벤트 전달
  input/InputState.h        [구현됨] 마우스/키보드 이번 프레임 상태 + 에지 질의
  ui/UI.h / UI.cpp          [구현됨] Widget · UIWindow · Button · CheckBox · Slider · TextLine · UIContext 를 한 파일에
  render/RenderSnapshot.h   [구현됨] UI 는 여기의 값 타입 Quad 만 방출한다
  game/TitleScreen.h/.cpp   [구현됨] Title 화면 위젯 트리 (자유 함수, UIContext 는 game 을 모른다)
  game/InGameHud.h/.cpp     [구현됨] InGame 화면 위젯 트리
  game/SettingsScreen.h/.cpp [구현됨] Settings 오버레이 위젯 트리 — docs/game-settings.md
  ─────────────────────────────────────────────────────────────
  graphics/UIRenderer.h     [미구현] DrawFilledRect/DrawText/PushClipRect 경계. 현재는 Quad 직접 방출로 대체
  ui/UIStyle.h, ui/UILayout.h [미구현] Measure/Arrange, VerticalStack
```

화면 전환(옛 "UIScreen") 자체는 구현됐다 — `UIContext::SetScreen`/`SetOverlay`/`ClearOverlay`, 상세는 `docs/scene-flow-design.md`.

위젯 수가 늘거나 텍스트가 glyph atlas로 가면 `UI.cpp`를 `Widget`/`UIWindow`/`Button`/`TextLine`/`UIContext` 파일로 분리한다. 지금은 한 파일로도 SRP가 유지된다(각 타입의 책임이 분명하고 서로 독립적).

## 핵심 자료형

`Vec2` / `Rect` / `Color`는 `src/math/Math.h`의 `engine::math`에 있고 `engine::ui`는 `using`으로 그대로 재사용한다(UI 전용 기하 어휘를 따로 두지 않는다). `UIStyle` / `Visibility` / 정렬 enum은 아직 미도입이다.

```cpp
struct Vec2 { float x, y; };
struct Rect {
    float x, y, width, height;
    bool Contains(Vec2 point) const;
};

struct UIStyle {
    Color background;
    Color foreground;
    Color border;
    float borderWidth = 0.0f;
    float padding = 8.0f;
    float fontSize = 18.0f;
};

enum class Visibility { Visible, Hidden, Collapsed };
enum class HorizontalAlignment { Left, Center, Right, Stretch };
enum class VerticalAlignment { Top, Center, Bottom, Stretch };
```

`Hidden`은 공간은 차지하지만 그리지 않고, `Collapsed`는 공간도 차지하지 않는다.

## Widget 공통 인터페이스

```cpp
class Widget {
public:
    virtual ~Widget() = default;

    void AddChild(std::unique_ptr<Widget> child);
    void SetBounds(Rect bounds);
    const Rect& GetBounds() const;

    virtual Vec2 Measure(Vec2 availableSize);
    virtual void Arrange(Rect finalBounds);
    virtual bool OnPointerMove(Vec2 position);
    virtual bool OnPointerDown(Vec2 position);
    virtual bool OnPointerUp(Vec2 position);
    virtual void Render(UIRenderer& renderer) const;

    bool enabled = true;
    Visibility visibility = Visibility::Visible;
    UIStyle style{};
    std::vector<std::unique_ptr<Widget>> children;

protected:
    Rect bounds_{};
};
```

입력 이벤트는 **가장 앞에 그려진 자식부터 역순으로** hit test한다. 자식이 이벤트를 소비하면 부모에게 전달하지 않는다. `Widget` 자신이 소비하지 않은 이벤트만 부모가 처리한다.

## 각 위젯의 책임

| 타입 | 책임 | 자식 |
| --- | --- | --- |
| `UIWindow` | 사각형 배경·테두리 렌더링, padding 적용, 자식 컨테이너 | 허용 |
| `Button` | normal/hover/pressed/disabled 상태, 클릭 콜백, 키보드 포커스 | 선택적으로 `TextLine` 하나 |
| `CheckBox` | on/off 토글, 체크박스 + 라벨 렌더링, `onChanged(bool)` | 없음(라벨은 내부에서 텍스트로 그림) |
| `Slider` | `[min,max]` 값을 가로 드래그로 설정, 트랙+핸들 렌더링, `onChanged(float)` | 없음 |
| `TextLine` | UTF-8 문자열 한 줄, 글꼴·색상·정렬, 줄바꿈 없음 | 없음 |

`Button`/`CheckBox`의 클릭(토글)은 마우스 down과 up이 모두 같은 위젯 내부에서 일어날 때만 발생한다. 이 규칙은 드래그 중 실수로 클릭되는 것을 막는다.

`Slider`는 다른 계약이다 — 드래그 시작(down)은 자기 bounds 안에서만 인정하지만, 그 뒤 `PointerMove`는 커서가 bounds 밖으로 나가도 계속 값을 갱신한다(`Widget::PointerMove`가 위치와 무관하게 모든 자식에 도달하는 걸 이용). **알려진 한계**: `Widget::PointerUp`은 형제 중 먼저 `true`를 반환하는 위젯에서 순회가 멈춘다 — 드래그를 끝내는 up 이벤트가 다른 형제(겹치지 않는 레이아웃이면 거의 안 생김)에 먼저 소비되면 Slider가 그 프레임에 드래그를 못 풀 수 있다. 실사용 세팅 패널(수직 스택, 겹침 없음)에서는 발생하지 않지만, 진짜 입력 캡처(포인터를 누른 위젯에 강제 고정)는 아직 없다 — `CLAUDE.md` 알려진 이슈 참고.

```cpp
class Button final : public Widget {
public:
    std::function<void()> onClick;
    bool OnPointerMove(Vec2 position) override;
    bool OnPointerDown(Vec2 position) override;
    bool OnPointerUp(Vec2 position) override;
    void Render(UIRenderer& renderer) const override;
private:
    bool hovered_ = false;
    bool pressed_ = false;
};
```

## 레이아웃 규칙

첫 구현은 복잡한 HTML/CSS식 레이아웃 대신 다음 두 컨테이너만 제공한다.

1. `UIWindow`: 절대 좌표로 자식의 `Rect`를 지정한다. HUD와 디버그 메뉴에 적합하다.
2. `VerticalStack`: 자식을 세로로 쌓고 간격(`spacing`)과 padding을 적용한다. 메뉴에 적합하다.

프레임 중 `UIScreen::Layout(clientRect)`에서 `Measure` 후 `Arrange`를 실행한다. 창 크기가 바뀌거나 UI가 변경된 경우에만 `layoutDirty`를 표시해 재계산한다.

## 프레임 순서

```text
Win32 message → Input::BeginFrame
              → Game::Update
              → UIContext::Update(Input)
                   └─ hit test / hover / click / layout
              → Graphics::BeginFrame
              → Game::Render
              → UIContext::Render(UIRenderer)
              → Graphics::Present
```

`UIContext`는 현재 화면 하나만 활성화하고, 화면 전환 요청은 프레임 끝에 적용한다. 이벤트 처리 중 UI 트리를 즉시 파괴하지 않으므로 안전하다.

### 현재 구현의 입력 경로

위 순서는 목표다. 지금은 `Measure`/`Arrange`/`UIScreen`이 없어 다음과 같이 동작한다.

```text
Win32Window (WM_MOUSE*) → IWindowEventSink → Application
  · UIContext::PointerDown/Move/Up(pos) 즉시 호출, bool 반환 = "UI가 소비함"
  · 좌클릭을 UI가 소비하면 그 이벤트는 InputState 로 전달되지 않는다 (게임 입력 스킵)
  · hover(PointerMove)는 배타적이지 않다: UI hover 갱신 + 게임도 커서 위치 획득
Application::Run 프레임 루프:
  InputState::BeginFrame → PumpMessages → Simulation::Step → SnapshotBuilder(UIContext::Build) → Submit
```

`Button::OnClick`은 down과 up이 같은 버튼 안에서 일어날 때만 호출된다(드래그 중 오클릭 방지). 눌림 상태를 정리하려고 up 이벤트는 소비 여부와 무관하게 항상 위젯 트리에 전달한다.

## UIRenderer 경계

UI 위젯은 DX11 API를 직접 호출하지 않는다. `UIRenderer`만 GPU 리소스를 다룬다.

```cpp
class UIRenderer {
public:
    void DrawFilledRect(Rect rect, Color color);
    void DrawBorder(Rect rect, float width, Color color);
    void DrawText(std::string_view utf8, Vec2 position,
                  const Font& font, Color color);
    void PushClipRect(Rect rect);
    void PopClipRect();
};
```

이 경계 덕분에 버튼의 동작·레이아웃 단위 테스트가 가능하며, 추후 DX12로 렌더러를 교체해도 UI 위젯 코드는 유지된다. DX11 쪽은 사각형들을 동적 vertex buffer에 모아 한 번에 그리는 배치 렌더링으로 시작한다. 텍스트는 bitmap font atlas 또는 DirectWrite 기반 glyph atlas을 사용한다.

## 사용 예시

실제 코드(`src/game/TitleScreen.cpp`)에서 그대로 가져온 형태 — 화면 하나를 자유 함수가 만들어 반환하고, `Application`이 `UIContext::SetScreen`으로 꽂는다.

```cpp
std::unique_ptr<ui::Widget> BuildTitleScreen(
    std::function<void()> onStart,
    std::function<void()> onOpenSettings,
    std::function<void()> onQuit)
{
    auto panel = std::make_unique<ui::UIWindow>();
    panel->SetBounds({ 480, 260, 320, 220 });

    auto start = std::make_unique<ui::Button>("START");
    start->SetBounds({ 16, 56, 288, 42 });
    start->onClick = std::move(onStart);
    panel->AddChild(std::move(start));
    // ... SETTINGS/QUIT 버튼도 같은 식

    return panel;
}

// Application 쪽:
m_ui.SetScreen(BuildTitleScreen(
    [this] { EnterInGame(); },
    [this] { OpenSettings(); },
    [this] { m_window.RequestClose(); }));
```

Slider/CheckBox를 곁들인 예시(설정 화면, `docs/game-settings.md`)는 `src/game/SettingsScreen.cpp`의 `AddSliderRow` 참고 — 슬라이더의 `onChanged`가 옆 `TextLine` 라벨을 직접 갱신하는 패턴(캡처한 raw 포인터, 트리 전체가 한 소유자 밑에 있어 안전)을 그대로 재사용하면 된다.

## 구현 순서

1. **완료(변형):** `Vec2`/`Rect`/`Color`는 `engine::math`에. `UIStyle`·`UIRenderer` 경계 대신 위젯이 `Quad`를 직접 방출한다.
2. **완료:** 자식 소유·bounds·render traversal을 가진 `Widget`과 `UIWindow`.
3. **완료:** `InputState`에 마우스 위치, 좌/우/중 버튼의 down/pressed/released, 키 down/pressed/released.
4. **완료:** hit test(앞에 그린 자식부터 역순), `Button` normal/hover/pressed 전이, `onClick`(down·up 동일 버튼 내부). `UIContext::PointerXxx`가 소비 여부를 `bool`로 반환.
5. **완료(임시):** 내장 5×7 ASCII 비트맵 폰트(대문자 A-Z, 0-9, `: - . %`) + `TextLine`. glyph atlas는 로컬라이제이션 시 교체 — 그때까진 위젯 라벨이 영어로 고정.
6. **완료:** `CheckBox`, `Slider`. `UIContext`의 화면/오버레이 전환(`SetScreen`/`SetOverlay`/`ClearOverlay`) — `docs/scene-flow-design.md`.
7. **미구현:** `VerticalStack`(지금은 좌표를 손으로 계산), clipping(`PushClipRect`), keyboard focus, Slider의 진짜 입력 캡처.

IME, 여러 줄 편집, 접근성, 반응형 레이아웃은 `TextBox` 같은 입력 위젯을 만들 때 별도 단계로 다룬다. 현재의 `TextLine`은 표시 전용이다.
