# 스크롤 목록 + 위젯 오브젝트 풀 (ScrollList & Object Pool)

긴 목록을 **스크롤 가능한 창** 안에 보여주는 UI. 논리 항목은 N개여도 실제 위젯은
화면에 보이는 ~M개만 두고, 스크롤할 때 파괴/재생성이 아니라 **재바인딩(recycle)** 한다
(가상화 / UI virtualization — RecyclerView·UITableView cell reuse 와 같은 패턴).

**상태: `ui::ScrollList` 구현됨 (v1) + `core::ObjectPool<T>` 구현됨 (§1.1).** 고정 행 높이 ·
클리핑 A · 휠 입력 · 스크롤바 드래그 · 데모 화면(`game/InventoryScreen`, 타이틀 → ITEMS).
`core::ObjectPool<T>` 는 `src/core/ObjectPool.h`, 첫 사용처는 데모 씬 2 크라우드. 미구현:
`ScrollList` 가 `ObjectPool` 을 쓰도록 리팩터(현재 자체 ring, §1.2), 가변 행 높이, 키보드 네비,
크라우드 SoA 승격(`instanced-rendering.md` §6.2). 이 문서가 계약.

관련: `docs/ui-architecture.md`(Widget·UIContext·Quad 방출·clipping 미구현), `docs/scene-flow-design.md`(화면을 자유 함수가 만든다), `docs/entity-lifecycle-design.md` §4(같은 타입 → 연속 메모리, 이 문서의 풀도 그 규칙), `docs/collider-design.md`(메인 스레드 전용 규칙 선례).

---

## 0. 지금 뭐가 문제인가

현재 UI 는 `Widget::AddChild(unique_ptr<Widget>)` 로 트리를 만든다 — 위젯마다 힙 할당 1회.
목록 항목 N개를 그대로 위젯 N개로 만들면:

- 힙 할당 N회, 상주 위젯 N개(항목 수에 비례).
- 매 프레임 트리 순회 O(N).
- 스크롤하면 트리를 다시 만들거나(파괴+재생성 churn, 프레임 스파이크) 전부 유지(메모리 낭비).
- clipping 이 없어서(`ui-architecture.md`) 창 밖 행도 그려진다.

→ **가상화**: 위젯은 뷰포트에 들어가는 만큼(+overscan)만. 스크롤 = 그 위젯들을 다른 데이터 인덱스로 다시 바인딩.

---

## 1. 구성요소

### 1.1 `core::ObjectPool<T>` — 범용 풀 (메모리 이점) — **구현됨** (`src/core/ObjectPool.h`)

```cpp
template <class T>
class ObjectPool : private core::NonCopyable
{
public:
    struct Handle { std::uint32_t index; std::uint32_t generation; bool Valid() const; };

    ObjectPool() = default;                        // 빈 풀(capacity 0)
    explicit ObjectPool(std::size_t capacity);     // 슬롯 1회 할당
    void Init(std::size_t capacity);               // 나중에 (재)초기화 — 내용 전부 버림

    [[nodiscard]] Handle Acquire();                // 죽은 슬롯 재사용 → T::Reset(). 꽉 차면 무효 핸들
    void Release(Handle);                          // free 스택으로. 파괴 X. stale/무효 핸들은 no-op
    [[nodiscard]] bool IsLive(Handle) const;
    [[nodiscard]] T* Get(Handle);                  // generation·active 불일치면 nullptr

    // 슬롯은 이동하지 않는다 → Handle::index 는 그 객체 수명 내내 유효(외부 보관 가능).
    [[nodiscard]] T* Slots();                                       // capacity 개, ActiveIndices() 만 live
    [[nodiscard]] const std::vector<std::uint32_t>& ActiveIndices() const;   // 살아있는 슬롯 인덱스(조밀, 순서 불특정)
    [[nodiscard]] std::size_t Size() const;   [[nodiscard]] bool Full() const;

private:
    std::vector<T> m_slots;                        // 안정 저장, 재정렬 안 함 (entity-lifecycle-design.md §4)
    std::vector<std::uint32_t> m_generation;       // 슬롯별, Acquire 때 +1
    std::vector<std::uint8_t>  m_slotActive;
    std::vector<std::uint32_t> m_activePos;        // 슬롯 인덱스 → m_active 안 위치 (swap-remove 용)
    std::vector<std::uint32_t> m_active, m_free;
};
```

**설계 노트**: 원안은 `std::span<T> Active()`(슬롯을 조밀 압축, swap-remove)였으나, 그러면
객체가 물리적으로 이동해 `Handle::index` 를 간접 테이블로 다시 매핑해야 한다. 구현은 **슬롯
고정 + `ActiveIndices()` 조밀 리스트** 를 택했다 — `Handle::index` == 슬롯 인덱스라 간접층이
없고 `T` 가 이동 가능할 필요도 없다. `ParallelFor` 는 `ActiveIndices()` 를 겹치지 않는
`[begin,end)` 로 쪼개면 되고(각 항목이 유일한 슬롯 → `Slots()[active[k]]` 쓰기 충돌 없음),
`SnapshotBuilder` 는 `for (i : ActiveIndices()) use Slots()[i]`. 압축형이 필요해지면(원거리
순회 캐시) 그때 확장.

첫 사용처: 데모 씬 2 크라우드(`game::Simulation::m_agents`, `SimAgent` 600마리 / capacity 1024),
`docs/demo-scene.md`·`docs/instanced-rendering.md` §6.

이점 (요구사항: "메모리 관리 이점" + "재사용 이점"):

- **할당 횟수** — 생성 시 `reserve(capacity)` 한 번. 이후 `Acquire`/`Release` 는 힙을 안 건드린다.
- **재사용** — `Release` 는 `T` 를 파괴하지 않는다. 다음 `Acquire` 는 그 슬롯을 `Reset()` 만 하고 돌려준다. `T` 안의 `std::string`/`std::vector` 버퍼 capacity 가 살아남아 **재바인딩 때 realloc 0**.
- **연속 메모리** — `entity-lifecycle-design.md` §4 규칙 그대로. 순회가 캐시 친화적.
- **수명 예측** — 성장(재할당+복사)이 없으므로 프레임 타임이 풀 사용량·회전율과 무관하게 평평하다.
- **stale 방어** — `Handle` 의 generation 이 `EntityId` 와 같은 정신(별개 타입). 재사용된 슬롯을 옛 핸들이 잘못 가리키지 않는다.

스레드: **메인/UI 스레드 전용**. `JobSystem` 워커 안에서 `Acquire`/`Release` 금지(CLAUDE.md 불변 규칙 6, `CollisionWorld`/`EntityRegistry` 와 동일).

### 1.2 `ui::ScrollList` 전용 recycler — 풀의 특수형 (재사용 이점)

`ScrollList` 의 행 수명 패턴은 "항상 정확히 `poolSize` 개가 live" 라 free-list 가 필요 없다 —
인덱스된 고정 배열(ring)이면 충분하고 더 단순하다. `core::ObjectPool` 의 이점(할당 0, 버퍼
재사용, 연속)은 그대로 가져간다.

```cpp
poolSize = ceil(viewportHeight / rowHeight) + 1   // 위·아래 부분 행
         + kOverscan;                              // = 2, 스크롤 중 깜빡임 방지
```

각 `Row`(ScrollList 내부 타입) = 최소 표시 위젯: 배경 rect + `TextLine` 1개 + 선택/hover
하이라이트 rect. `Row::Bind(RowView&)` 로 **내용만** 갈아끼운다 — 파괴/재생성 없음.

### 1.3 `ui::ScrollList : public ui::Widget`

```cpp
class ScrollList final : public Widget
{
public:
    void SetModel(std::unique_ptr<ListModel>);   // 소유 이전
    void SetRowHeight(float);
    void NotifyModelChanged();                    // Count() 재조회 + scrollOffset 재클램프
    void EnsureVisible(std::size_t index);
    std::function<void(std::size_t)> onSelect;

    void Build(std::vector<render::Quad>& out, Vec2 parentOrigin) const override;
    bool PointerMove(Vec2 pos, Vec2 parentOrigin) override;
    bool PointerDown(Vec2 pos, Vec2 parentOrigin) override;
    bool PointerUp(Vec2 pos, Vec2 parentOrigin) override;
    bool PointerWheel(Vec2 pos, float delta, Vec2 parentOrigin) override;   // §1.6

private:
    std::unique_ptr<ListModel> m_model;
    float m_rowHeight{ 28.0f };
    float m_scrollOffset{ 0.0f };       // px, [0, max(0, contentH - viewportH)]
    int m_selectedIndex{ -1 };
    int m_hoverIndex{ -1 };
    std::vector<Row> m_rows;            // = poolSize, 생성 시 고정 (recycler)
    ScrollBar m_bar;                    // 내부: 트랙 + thumb (Slider 계약 재사용)
};
```

**데이터 소스 (DIP)** — `ui/` 는 "항목이 뭔지" 몰라야 한다(렌더러가 `playerX` 를 모르는 것과 같은 OCP):

```cpp
class ListModel
{
public:
    virtual ~ListModel() = default;
    [[nodiscard]] virtual std::size_t Count() const = 0;
    virtual void BindRow(RowView& row, std::size_t index) const = 0;   // 게임 쪽이 구현
};

// ISP 최소 표면 — ui-architecture.md 의 UIRenderer 경계와 같은 형태
class RowView
{
public:
    void SetText(std::string_view);
    void SetTint(Color);
    // 확장: SetIcon, SetSubText, ...
};
```

### 1.4 스크롤 상태 ↔ 데이터 인덱스 매핑

```
contentH        = model.Count() * rowHeight
scrollOffset    = clamp(scrollOffset, 0, max(0, contentH - viewportH))
first           = floor(scrollOffset / rowHeight)          // 첫 (부분) 행의 데이터 인덱스
pxWithinFirst   = scrollOffset - first * rowHeight         // 0..rowHeight
```

`Build()`:

```
for k in [0, poolSize):
    di = first + k
    if di >= model.Count(): 그 슬롯은 이번 프레임 안 그림 (남는 pool 슬롯은 유휴)
    rowTop = viewport.y - pxWithinFirst + k * rowHeight    // 부분 픽셀 오프셋 → 부드러운 스크롤
    model.BindRow(m_rows[k].View(), di)                    // 재바인딩만, 재생성 X
    m_rows[k].highlighted = (di == m_selectedIndex)
    m_rows[k].hovered     = (di == m_hoverIndex)
    m_rows[k].Build(clippedOut, { viewport.x, rowTop })    // §1.5 클리핑
m_bar.Build(...) : thumbH ∝ viewportH / contentH,  thumbY ∝ scrollOffset / (contentH - viewportH)
```

**선택/hover 상태는 데이터 인덱스로 `ScrollList` 가 보관한다.** 행 위젯은 recycle 되므로
행에 상태를 두면 스크롤할 때 엉뚱한 항목이 하이라이트된다.

### 1.5 클리핑 — 판단 필요 (A 권장)

뷰포트 밖(그리고 상·하단에 걸친) 행이 그려지면 안 된다. 현재 clipping 이 없다.

- **A. `ScrollList` 안에서 Quad clamp (렌더러 무변경, 권장)** — 각 행을 임시 버퍼에 그린 뒤,
  `viewport` 와 교차하는 `Quad` 만 본 출력에 복사하되 **각 Quad 의 rect 를 viewport 로 clamp**
  (완전히 밖이면 drop). `Quad` 는 `{x,y,w,h,rgba}` AABB 라 clamp 가 자명하고, glyph quad 도
  같이 잘려 경계가 깔끔하다. "UI 는 `Quad` 배열만 방출" 불변식(`ui-architecture.md`) 유지, 렌더러
  리스크 0.
- **B. 진짜 scissor rect (제대로, 나중)** — `render::Quad` 에 clip rect 필드 추가(또는 quad
  스트림에 `SetClip` 커맨드) + `QuadPass2D` 가 `RSSetScissorRects` + rasterizer `ScissorEnable`.
  `RenderSnapshot.h` + 2D 패스 변경. 중첩 스크롤·임의 마스킹까지 열린다. `ui-architecture.md` #8
  의 `PushClipRect` 를 실제로 구현하는 길.
  → **B 의 상세 설계는 `docs/texture-atlas-and-sprite-pass.md`** (아틀라스 + `SpriteDraw` 와 한 덩어리).
  이미지 아틀라스를 구현하면 텍스처 콘텐츠에는 A 의 rect clamp 가 UV 재계산·엣지 블리딩을
  안고 가므로 B 가 유리해진다 — 그 경우 클리핑은 A 를 건너뛰고 아틀라스 작업의 일부로 B 를 하고,
  `ScrollList` 는 `dl.PushClip(viewport)` 만 쓴다.

### 1.6 입력

- **마우스 휠** — `IWindowEventSink::OnMouseWheel(float delta)` 신설 + `Win32Window` 가
  `WM_MOUSEWHEEL` 처리 + `InputState` 가 프레임 단위 누적(다른 마우스 상태처럼 `BeginFrame` 리셋).
  `Application` 이 `UIContext::PointerWheel(pos, delta)` 로 전달 → hit 된 `ScrollList` 가
  `m_scrollOffset -= delta * kWheelStep` 후 클램프.
- **스크롤바 드래그** — `Slider` 계약 재사용(down 은 thumb bounds 안에서만, 이후 move 는 커서가
  밖으로 나가도 추적). thumb 위치 → `scrollOffset` 역산.
- **행 클릭** — `PointerDown`: `di = first + floor((pos.y - viewport.y + pxWithinFirst) / rowHeight)`;
  `0 <= di < Count()` 면 `m_selectedIndex = di` + `onSelect(di)`. `PointerMove`: 같은 식으로 `m_hoverIndex`(뷰포트 밖이면 -1).
- **키보드 (확장)** — 포커스 시 ↑/↓ = `selectedIndex ± 1` + `EnsureVisible`, PageUp/Down = 뷰포트 행 수만큼.

### 1.7 `Widget` 트리와의 관계

- `ScrollList` 는 `m_children` 을 **생성 시 `poolSize` 행 + 스크롤바로 고정**하고 이후 add/remove
  하지 않는다 — 매 프레임 위치·바인딩만 바꾼다. 트리 구조 불변이라 "이벤트 처리 중 트리 파괴
  금지"(`scene-flow-design.md`) 를 자동으로 지킨다.
- `Build`/`PointerXxx` 는 base 구현을 부르지 않고 자체 구현한다(가상 인덱싱 + 클리핑 때문).

---

## 2. 메모리 · 재사용 이점 (요구사항 대응)

| | 순진한 방식 (위젯 N개) | ScrollList + 풀 |
|---|---|---|
| 힙 할당 | 항목당 1회 → **N회** | 생성 시 `poolSize`회 (~20), 이후 **0** |
| 상주 위젯 | N (항목 수에 비례) | `poolSize` (뷰포트 종속, 항목 수 **무관**) |
| 스크롤 1행 | 트리 재구성 or 전량 유지 | 행 `poolSize`개 **재바인딩** (문자열 capacity 유지, realloc 0) |
| 매 프레임 순회 | O(N) | O(`poolSize`) |
| 프레임 타임 | 항목 수에 비례 | **평평** (항목 100 vs 100k 동일) |
| 캐시 | 위젯이 힙 여기저기 | **연속 배열** (`entity-lifecycle-design.md` §4) |

---

## 3. 하지 말 것

- 행 위젯에 선택/hover/스크롤 상태를 저장하지 않는다 — recycle 되므로 데이터 인덱스로 `ScrollList` 가 보관.
- 매 프레임 `m_rows` 를 `clear()` 후 재생성하지 않는다 — 재바인딩만(풀의 핵심).
- `poolSize` 를 항목 수로 잡지 않는다 — 뷰포트 + overscan 으로만.
- (A안) viewport 밖 Quad 를 clamp 없이 방출하지 않는다 / (B안) scissor 없이 방출하지 않는다.
- `core::ObjectPool` 을 `JobSystem` 워커에서 `Acquire`/`Release` 하지 않는다(규칙 6).
- 가변 행 높이를 prefix-sum/높이 캐시 없이 넣지 않는다 — **v1 은 고정 행 높이**(`first = floor(offset/rowH)` 가 O(1)). 가변 높이는 별도 확장(누적합 인덱스 + 추정 높이).
- `ListModel::BindRow` 안에서 파일 IO·힙 폭증을 하지 않는다 — 이미 로드된 데이터를 참조만(뷰포트 프레임마다 `poolSize`회 불린다).

---

## 4. 사용 방법 (How to use)

구현: `src/ui/ScrollList.{h,cpp}`, 데모 `src/game/InventoryScreen.{h,cpp}`. 배선(휠 경로,
클리핑 A, 저수준 프리미티브)은 §5.

### 목록 화면 하나 만들기

```cpp
// game/InventoryScreen.cpp — UIContext 는 이게 뭔지 모른다 (자유 함수, scene-flow-design.md)
class StringListModel final : public ui::ListModel
{
public:
    explicit StringListModel(std::span<const std::string> items) : m_items(items) {}
    std::size_t Count() const override { return m_items.size(); }
    void BindRow(ui::RowView& row, std::size_t i) const override
    {
        row.SetText(m_items[i]);
        row.SetTint((i & 1) ? ui::Color{ 0.16f, 0.18f, 0.24f, 1 }
                            : ui::Color{ 0.13f, 0.15f, 0.20f, 1 });   // zebra
    }
private:
    std::span<const std::string> m_items;   // 소유 안 함 - 호출부 데이터가 더 오래 산다
};

std::unique_ptr<ui::Widget> BuildInventoryScreen(std::span<const std::string> items,
                                                 std::function<void(std::size_t)> onPick)
{
    auto panel = std::make_unique<ui::UIWindow>();
    panel->SetBounds({ 360, 120, 560, 480 });

    auto list = std::make_unique<ui::ScrollList>();
    list->SetBounds({ 16, 16, 528, 448 });          // = 뷰포트
    list->SetRowHeight(28.0f);
    list->SetModel(std::make_unique<StringListModel>(items));
    list->onSelect = std::move(onPick);
    panel->AddChild(std::move(list));
    return panel;
}
```

`Application` 쪽은 다른 화면과 동일: `m_ui.SetScreen(BuildInventoryScreen(items, [this](std::size_t i){ ... }))`.

### 데이터가 바뀌면 (항목 추가/삭제/정렬)

```cpp
list->NotifyModelChanged();   // Count() 재조회 + scrollOffset 재클램프. 위젯 재생성 없음.
```

### 범용 오브젝트 풀 쓰기 (툴팁, 전이 이펙트, 단명 엔티티 등)

```cpp
core::ObjectPool<Tooltip> m_tooltips{ /*capacity=*/16 };

auto h = m_tooltips.Acquire();          // 죽은 슬롯 재사용 + Tooltip::Reset()
m_tooltips.Get(h)->Show(text, pos);
// ... 수명 끝:
m_tooltips.Release(h);                   // 파괴 X, free 로. 버퍼 capacity 유지.

for (Tooltip& t : m_tooltips.Active())   // 연속 순회
    t.Build(out);
```

`T` 는 `void Reset()` 와 (재)초기화 세터를 제공해야 한다 — 생성자는 풀 생성 시 딱 한 번만 돈다.

### 행 모양 바꾸기

`ui::ScrollList::Row`(내부, `ScrollList.cpp`) = `Row::Emit` 이 배경 Quad(`tint`) + 선택/hover
오버레이 + `ui::DrawText`(글리프 Quad) 를 스크래치 버퍼에 그린 뒤 뷰포트로 clamp 해 출력에 붙인다.
아이콘·다열이 필요하면 `Row::Emit` 에 Quad 를 추가하고 `RowView` 에 세터를 추가한다.
`ScrollList`/`ListModel` 계약은 안 바뀐다(OCP).

---

## 5. 지은 것 / 남은 것

지음 (v1):

- `src/ui/ScrollList.{h,cpp}` — `ScrollList` · `ListModel` · `RowView` · 내부 `Row`(recycler는
  `std::vector<std::unique_ptr<Row>>` ring, 뷰포트+overscan 만큼만, 스크롤 중 `assign`/`clear` 만).
- 휠 입력: `IWindowEventSink::OnMouseWheel` + `Win32Window` `WM_MOUSEWHEEL` + `InputState` 누적
  (`MouseWheel()`, `BeginFrame` 리셋) + `Widget::PointerWheel` + `UIContext::PointerWheel`.
- 클리핑 **A**: `ScrollList` 내부 `ClampQuad`. `render::Quad`/`QuadPass2D` 무변경.
- `ui::DrawRect`/`ui::DrawText` — `UI.h` 에 노출한 저수준 프리미티브(행이 자기 Quad 를 직접 그림).
- `src/game/InventoryScreen.{h,cpp}` + 데모(색상별 200개), 타이틀 화면 ITEMS 버튼.

남음:

- `src/core/ObjectPool.h` — 범용 풀 템플릿(§1.1). ScrollList 는 ring 이라 안 씀 — 툴팁/이펙트/단명
  엔티티 같은 다른 소비자가 생길 때 지음.
- 클리핑 B(진짜 scissor) — 텍스처 아틀라스 작업(`texture-atlas-and-sprite-pass.md`)과 한 덩어리.
- 가변 행 높이(누적합 인덱스), 키보드 네비(↑/↓·PageUp/Down + `EnsureVisible`).
- 문서: `ui-architecture.md` #7(clipping)·위젯 표에 `ScrollList` 반영(했음), `command-playbook.md` 행(했음).
