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
 └─ UIContext               화면 UI의 소유자 및 프레임 조정자
     └─ UIScreen            한 화면의 최상위 UI 트리 (예: MainMenu)
         └─ Widget
             ├─ UIWindow    배경, 테두리, 자식 배치 컨테이너
             ├─ Button      hover / pressed / click 이벤트
             └─ TextLine    한 줄 텍스트 표시
```

월드는 먼저 렌더링하고 UI는 마지막에 렌더링한다. 따라서 UI는 항상 월드 위에 표시된다.

## 디렉터리 제안

```text
src/
  platform/NativeWindow.h
  input/Input.h
  graphics/Graphics.h
  graphics/UIRenderer.h
  ui/UIContext.h
  ui/UIScreen.h
  ui/Widget.h
  ui/UIWindow.h
  ui/Button.h
  ui/TextLine.h
  ui/UIStyle.h
  ui/UILayout.h
```

## 핵심 자료형

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
| `TextLine` | UTF-8 문자열 한 줄, 글꼴·색상·정렬, 줄바꿈 없음 | 없음 |

`Button`의 `OnClick`은 마우스 down과 up이 모두 같은 버튼 내부에서 일어날 때만 호출한다. 이 규칙은 드래그 중 실수로 클릭되는 것을 막는다.

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

```cpp
auto menu = std::make_unique<UIWindow>();
menu->SetBounds({ 40, 40, 280, 180 });

auto title = std::make_unique<TextLine>("Cpp Window Game");
title->SetBounds({ 20, 20, 240, 28 });
menu->AddChild(std::move(title));

auto start = std::make_unique<Button>("Start");
start->SetBounds({ 20, 70, 240, 42 });
start->onClick = [this] { StartGame(); };
menu->AddChild(std::move(start));

uiContext.SetScreen(std::move(menu));
```

## 구현 순서

1. `Vec2`, `Rect`, `Color`, `UIStyle`와 `UIRenderer::DrawFilledRect`를 만든다.
2. 자식 소유·bounds·render traversal만 가진 `Widget`과 `UIWindow`를 만든다.
3. `Input`에 마우스 위치, 좌/우 버튼의 pressed/down/released 상태를 추가한다.
4. hit test와 `Button` 상태 전이를 구현하고 `onClick`을 검증한다.
5. bitmap font atlas과 `TextLine`을 추가한다.
6. `VerticalStack`, clipping, keyboard focus, UI 화면 전환을 추가한다.

IME, 여러 줄 편집, 접근성, 반응형 레이아웃은 `TextBox` 같은 입력 위젯을 만들 때 별도 단계로 다룬다. 현재의 `TextLine`은 표시 전용이다.
