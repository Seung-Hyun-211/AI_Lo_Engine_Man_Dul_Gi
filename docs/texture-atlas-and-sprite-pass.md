# 텍스처 아틀라스 + 스프라이트 패스 + 클리핑 (Atlas / SpriteDraw / Scissor)

이미지 아틀라스(스프라이트 + 글리프)와 그걸 그리는 텍스처 2D 패스, 그리고 그 패스에 얹는
**scissor 클리핑**(= `docs/scrollable-list-and-pool.md` §1.5 의 클리핑 "B")을 한 덩어리로 설계.
텍스처 콘텐츠가 생기면 CPU 로 rect+UV 를 잘라내는 A 안이 부담이 되고(UV 재계산·엣지 블리딩),
scissor 는 아틀라스 패스가 어차피 손대는 파일에 필드 하나 얹는 한계비용이 된다.

**상태: 부분 구현.** §7 의 1·2 완료 + 3 최소본(동기 `.dds` 로더 + `.atlas` 파서 + `AtlasIndex`, `AssetRegistry` 는 아직) + **4 (`tools/atlas_pack` v1, 무압축 페이지)** 완료. `ui::DrawList`·`ScrollList` 연동·`GlyphAtlas`·BC7 압축은 미구현.

관련: `docs/ui-architecture.md`(Quad 방출·clipping 미구현·"텍스트는 glyph atlas"), `docs/scrollable-list-and-pool.md`(ScrollList 가 이 클리핑을 씀), `docs/loading-and-streaming.md`(아틀라스 = 비동기 로드 에셋), `docs/model-animation-research.md`·`docs/animation-design.md` §2(2D 스프라이트 애니메이션의 선행 조건), `command-playbook.md` #3(SpriteDraw + SpriteBatch 로드맵).

---

## 0. 지금 상태

- `render::Quad {x,y,w,h,rgba}` — 단색 AABB, UV 없음. `render/r2d/Sprite2D.h`.
- `QuadPass2D` — 동적 VB 1개 + `Draw` 1번, `quad2d.hlsl`(텍스처 샘플 없음). straight-alpha 블렌드, depth off. **scissor rasterizer state 없음.**
- UI 텍스트 = `UI.cpp` 의 5×7 절차적 비트맵 폰트 → 글자마다 단색 quad 다발.
- 진짜 scissor 클리핑 없음(`ui-architecture.md` #8 `PushClipRect` 미구현). `ScrollList` 는 Quad clamp(옵션 A)로 자체 처리.

---

## 1. 아틀라스

### 1.1 아틀라스 에셋 = 페이지 이미지 + 매니페스트

**빌드/번들/페이지 크기/포맷/밉맵의 상세는 `docs/atlas-build-pipeline.md`.** 요약:

- 아틀라스는 **그룹**(`ui` / `char/<name>` / `obj/<cat>` / `scene/<id>`) 단위로 관리 — 같이 바뀌는 것끼리 묶고, 그룹별로 **증분 빌드**(매번 전부 안 만듦).
- 페이지는 고정 크기 정사각형 — `{1024, 2048, 4096}`, 기본 4096. 그룹이 넘치면 페이지 1, 2...
- 파일 포맷 전역 고정: **`.dds` + BC7(`*_SRGB` 컬러) / BC4(단일채널)**. DirectX 전용이라 크로스플랫폼 트랜스코드 계층 없음. 큰 그룹은 밉 포함.
- 패킹은 오프라인(`tools/atlas_pack`), 런타임은 로드만.

```
assets/atlas/ui.0.dds                           — 그룹 페이지 (밉 포함, BC7)
assets/atlas/ui.atlas                           — 스프라이트 표
```

매니페스트 한 항목:

```
name          : "icon_sword"
page          : 0
uv            : x0 y0 x1 y1      (0..1, 픽셀 아님 — 해상도 무관)
pixelSize     : w h              (레이아웃용 원본 크기)
pivot         : px py            (0..1, 기본 0.5 0.5)
nineSlice     : l t r b          (선택 — 9-slice 패널용, 없으면 0)
```

- **gutter**: 패커는 각 스프라이트 둘레에 여백(edge-extend)을 둔다 — 크기는 밉 레벨 수에 맞춘다(`2^mips` 텍셀). bilinear·MSAA·밉 축소가 이웃 스프라이트를 빨아들이는 fringe 방지.

### 1.2 `render::TextureAtlas` — GPU + `AtlasIndex` — CPU

```cpp
struct SpriteRect { float u0, v0, u1, v1;  math::Vec2 pixelSize;  math::Vec2 pivot; };

// CPU 쪽 (메인/UI 스레드). 레이아웃 계산에 필요. 작음(수 KB) — 상주 유지.
class AtlasIndex
{
public:
    [[nodiscard]] const SpriteRect* Find(std::string_view name) const;   // 없으면 nullptr
    [[nodiscard]] std::uint32_t AtlasId() const;                          // GPU SRV 를 가리키는 핸들
};

// GPU 쪽 (렌더 스레드). 아틀라스 텍스처 + SRV 한 장. AssetRegistry 소유.
```

- **UV 는 메인 스레드가 Build 때 resolve 한다** — `AtlasIndex::Find(name)->u0..v1` 을 읽어 `SpriteDraw` 에 이미 넣는다. 렌더 스레드는 이름 조회 안 함, `atlasId` 로 SRV 만 바인드. (스냅샷은 값만 나른다 — 큰 이름표를 매 프레임 복사하지 않는다.)
- 큰 픽셀 데이터·SRV 는 `AssetRegistry`(`loading-and-streaming.md` §3.2)에, 작은 `AtlasIndex` 는 UI 가 `const AtlasIndex*` 로 참조.

### 1.3 폰트도 아틀라스 — `GlyphAtlas`

```cpp
struct GlyphMetrics { SpriteRect rect;  float advance, bearingX, bearingY; };

class GlyphAtlas   // = TextureAtlas + char → GlyphMetrics
{
public:
    [[nodiscard]] const GlyphMetrics* Glyph(char32_t codepoint) const;
    [[nodiscard]] float LineHeight() const;
    [[nodiscard]] std::uint32_t AtlasId() const;
};
```

- 글리프는 "메트릭이 붙은 스프라이트" 다 — 아틀라스 시스템이 UI 스프라이트와 글리프를 같은 방식으로 다룬다.
- 알파-커버리지 폰트 아틀라스: 텍셀은 커버리지(R 또는 A 채널), 색은 `SpriteDraw::tint` 에서. (SDF 폰트는 v2 — 셰이더만 바뀜.)
- `UI.cpp` 의 5×7 절차 폰트는 **부트스트랩용으로 유지** 하되, `GlyphAtlas` 가 로드되면 `AddText` 가 그쪽으로 스위치(`ui-architecture.md` #5 가 예고한 교체).

### 1.4 `loading-and-streaming` 통합

- `AssetKind::Atlas` — `AssetLoader` IO 워커가 이미지 디코드(`import::LoadImageFromFile` 재사용) + `.atlas` 파싱 → CPU `AtlasIndex`. 렌더 스레드 업로드 펌프가 `ID3D11Texture2D` + SRV 생성 → `AssetRegistry` 에 `GpuReady`.
- **부팅 매니페스트** 에 UI 아틀라스 + 폰트 아틀라스 포함 → 첫 프레임부터 텍스트·아이콘 렌더 가능(로딩 커튼 자체도 이걸 씀).
- 씬 매니페스트가 씬별 아틀라스를 나열. 공유 아틀라스는 매니페스트 diff 로 재로드 안 됨.

---

## 2. 텍스처 스프라이트 패스 + scissor 클리핑 (= 클리핑 B)

### 2.1 `render::SpriteDraw` 값 타입 (`render/r2d/Sprite2D.h`, `Quad` 옆)

```cpp
struct SpriteDraw
{
    float x, y, width, height;         // 대상 사각형, 픽셀 공간
    float u0, v0, u1, v1;              // 아틀라스 서브렉트 (메인 스레드가 이미 resolve)
    float r, g, b, a{ 1.0f };          // tint (곱). 글리프면 텍스트색(텍셀은 커버리지)
    std::uint32_t atlasId{ 0 };        // 바인드할 아틀라스 SRV 핸들
    math::Rect clip{ kNoClip };        // scissor 사각형 (전체 화면이 sentinel) ← 클리핑 B
};
```

`Quad` 는 그대로 둔다(레거시 `worldQuads` 2D 오버레이 데모 전용, 텍스처·클립 불필요).

### 2.2 UI 프리미티브 통일 — 판단 (통일 권장)

- **통일 (권장)**: UI 는 전부 `SpriteDraw` 로 방출. 단색 사각형 = 아틀라스의 **1×1 흰 스프라이트**(`"__white"`)를 tint 로. → 패스 하나·드로우 경로 하나, "모든 UI 는 텍스처 quad" (대부분 UI 렌더러의 형태). `Quad`/`QuadPass2D` 는 월드 오버레이만 남기고 파티클 데모 은퇴 시 함께 제거.
- **분리 (대안)**: 단색은 `Quad` 유지 + 텍스처만 `SpriteDraw`. 스냅샷에 리스트 2개, 패스가 두 단계. 마이그레이션 폭 작지만 클립·배칭 로직이 두 벌.

### 2.3 `render/r2d/SpritePass2D`

- `RenderSnapshot` 에 `std::vector<SpriteDraw> uiSprites` 추가.
- `Execute`:
  1. `uiSprites` 를 `(atlasId, clip)` 로 안정 그룹핑(원본 순서 = painter 순서 유지, 같은 키 연속 구간만 묶음).
  2. 그룹마다: `atlasId` 바뀌면 그 SRV 바인드 · `clip` 바뀌면 `RSSetScissorRects(1, &toPixels(clip))` · 그룹 quad 를 동적 VB 에 append 후 `Draw`.
  3. **가상 목록 = 아틀라스 1 + 클립 1 = 드로우 1.** 아틀라스 2개 + 스크롤 영역 1개 화면 ≈ 드로우 3.
- rasterizer state 는 `ScissorEnable = TRUE` (`Dx11Renderer` 에서 만들어 넘기거나 패스가 소유). 기본 clip = 뷰포트 전체.
- 블렌드·depth-off 는 `QuadPass2D` 와 동일 규칙. `render/detail/D3DCommon.h` 헬퍼 재사용.

### 2.4 `assets/shaders/sprite2d.hlsl`

```hlsl
cbuffer Screen : register(b0) { float2 screen; float2 _pad; };
Texture2D atlas : register(t0);
SamplerState samp : register(s0);   // MIN_MAG_LINEAR, CLAMP

struct VSIn  { float2 pos : POSITION; float2 uv : TEXCOORD; float4 tint : COLOR; };
struct VSOut { float4 pos : SV_POSITION; float2 uv : TEXCOORD; float4 tint : COLOR; };

VSOut VSMain(VSIn i) {
    VSOut o;
    o.pos  = float4(i.pos.x/screen.x*2-1, 1-i.pos.y/screen.y*2, 0, 1);
    o.uv   = i.uv;  o.tint = i.tint;  return o;
}
float4 PSMain(VSOut i) : SV_TARGET {
    float4 t = atlas.Sample(samp, i.uv);
    // RGBA 스프라이트: 곱.  커버리지 폰트: t.r 을 알파로 (아틀라스 빌드 시 결정, 별도 define 또는 별도 shader)
    return t * i.tint;
}
```

### 2.5 `Dx11Renderer`

- scissor 켠 rasterizer state 1개 추가(2D 패스가 요청). 3D 패스 상태와 별개 — 3D 는 scissor 안 씀.
- MSAA 씬 타깃에 그려도 scissor 는 정상(픽셀 사각형 컬링, 커버리지와 무관).

---

## 3. UI 쪽 변경 — `ui::DrawList` + 클립 스택

`Widget::Build(std::vector<Quad>&, Vec2)` → `Widget::Build(ui::DrawList&, Vec2)`.

```cpp
class DrawList
{
public:
    void FilledRect(Rect, Color);                 // "__white" 스프라이트 + tint + 현재 clip
    void Sprite(const SpriteRect&, Rect dest, Color tint = white);   // 아틀라스 스프라이트
    void Text(std::string_view, Vec2 pen, const GlyphAtlas&, Color);
    void Border(Rect, float w, Color);

    void PushClip(Rect);   // 현재 clip 과 교집합을 스택에 push
    void PopClip();
    [[nodiscard]] Rect Clip() const;   // 스택 top (없으면 kNoClip)

private:
    std::vector<render::SpriteDraw>& m_out;   // 스냅샷의 uiSprites
    std::vector<Rect> m_clipStack;
};
```

- 방출되는 모든 `SpriteDraw` 는 `clip = m_clipStack` top 을 달고 나간다.
- `ScrollList::Build` 클리핑 = **2줄**:
  ```cpp
  dl.PushClip(viewport);
  for (row : pooledRows) row.Build(dl, ...);
  m_bar.Build(dl, ...);
  dl.PopClip();
  ```
  → `scrollable-list-and-pool.md` §1.5 의 클리핑이 여기서 해결됨(A안 CPU clamp 불필요).
- **단계적 마이그레이션**: `Build(std::vector<Quad>&, Vec2)` 오버로드를 한동안 유지(레거시 위젯) + `Build(DrawList&, Vec2)` 신설. `UIContext::Build` 가 둘 다 호출해 `uiQuads` + `uiSprites` 채움. 위젯을 하나씩 `DrawList` 로 옮기고 마지막에 `Quad` 오버로드 제거.

---

## 4. 진행중 작업과의 접점

| WIP | 이 설계와의 관계 |
|---|---|
| `scrollable-list-and-pool.md` | 클리핑을 **B(scissor)로 확정**. `ScrollList` 는 `dl.PushClip/PopClip` 만. `RowView::SetIcon(SpriteRect)` 가 실제로 가능해짐. 풀/recycler 설계는 불변. |
| `loading-and-streaming.md` | 아틀라스 = `AssetKind::Atlas` 에셋. `AssetLoader` 디코드 + 렌더 스레드 SRV 업로드 + `AssetRegistry` 상주. 부팅 매니페스트에 UI/폰트 아틀라스. |
| `import::LoadImageFromFile` | 아틀라스 페이지 디코드에 재사용. `.tga` 는 자체 `LoadTga`, `.png/.jpg/.bmp/.gif` 는 `stb_image`(vendored, `src/import/ImageFile.*`). 결과는 `import::ImageData`(RGBA8 top-down). |
| `command-playbook.md` #3 (SpriteDraw + SpriteBatch) | 이 문서가 그 로드맵 항목의 구체화. `SpritePass2D` = 그 "SpriteBatch". |
| `animation-design.md` §2 (2D 스프라이트 애니메이션) | 선행 조건이던 "텍스처 `SpriteDraw`" 가 여기서 생김. `SpriteAnimator` 가 `SpriteRect` 를 시간에 따라 바꿔 `SpriteDraw` 에 복사. |
| `time-design.md` / demo-scene | 무관, 안 건드림. |

---

## 5. 성능 · 정확성

- **배칭**: `(atlasId, clip)` 그룹당 드로우 1회. 아틀라스를 몇 장으로 유지하고 클립 영역을 적게 쓰면(가상 목록은 1개) 드로우 수가 낮게 유지. 독립 클립 위젯이 수백 개면 배칭이 파편화 — 그 규모면 stencil 마스크 등 다른 수단(설계 밖).
- **엣지 블리딩**: 아틀라스 gutter(§1.1) 없으면 bilinear/MSAA 가 이웃 스프라이트를 빤다. 패커가 여백 + edge-extend 필수.
- **scissor + MSAA**: 씬 타깃이 멀티샘플이어도 scissor 는 픽셀 사각형 컬링이라 정상 동작.
- **클립 스택 깊이**: 상한(예: 16) 두고 초과 시 assert. 실제 UI 는 2~3 depth.
- **UV resolve 위치**: 반드시 메인 스레드 Build 에서. 렌더 스레드가 이름→UV 조회하면 스냅샷이 "값만" 원칙(불변 규칙 3) 을 어기고 아틀라스 인덱스를 렌더 스레드로 복사해야 함.

---

## 6. 사용 방법 (How to use)

### 새 아틀라스 추가

1. 낱장 PNG 를 그룹 폴더에 넣고 `tools/atlas_pack --group <name>` → `assets/atlas/<name>.0.dds` + `<name>.atlas`. 그룹·번들·포맷·밉맵 규칙은 `docs/atlas-build-pipeline.md`.
2. 부팅 또는 씬 매니페스트(`loading-and-streaming.md` §3.3)에 `<name>` 그룹 id 추가.
3. 화면 빌더가 `const AtlasIndex* atlas = registry.Atlas("<name>")` 를 받아 위젯에 넘김.

### 위젯에서 스프라이트 그리기

```cpp
void IconButton::Build(ui::DrawList& dl, Vec2 origin) const
{
    const Rect b = AbsoluteBounds(origin);
    dl.FilledRect(b, m_hovered ? kHover : kBase);
    if (const auto* r = m_atlas->Find(m_iconName))
        dl.Sprite(*r, { b.x + 8, b.y + 8, 24, 24 }, kTint);
}
```

### 스크롤/패널 클리핑

```cpp
dl.PushClip(viewportRect);
// ... 자식 Build ...
dl.PopClip();               // 반드시 짝 맞추기 (RAII 가드 ui::ClipScope 권장)
```

### 폰트를 아틀라스로 교체

`UI.cpp` `AddText` 를 `GlyphAtlas` 기반으로 재작성: 코드포인트마다 `Glyph(cp)` → `dl.Sprite(g->rect, {pen.x + g->bearingX, ...})` → `pen.x += g->advance`. 5×7 절차 폰트는 폴백으로 남김.

### 하지 말 것

- 아틀라스를 gutter 없이 패킹하지 않는다(fringe).
- `PushClip` 과 `PopClip` 짝을 안 맞추지 않는다(`ui::ClipScope` RAII 로).
- `SpriteDraw::u0..v1` 를 렌더 스레드에서 이름 조회로 채우지 않는다 — 메인 스레드 Build 에서 resolve.
- `JobSystem` 워커에서 `AtlasIndex`/`AssetRegistry` 접근 금지(불변 규칙 6).
- 아틀라스를 화면마다 새로 만들지 않는다 — `AssetRegistry` 상주 + 매니페스트 diff.
- 클립 영역을 quad 마다 다르게 잡아 드로우 콜을 폭증시키지 않는다(같은 clip 을 연속 방출).
- `SpriteDraw` 와 `Quad` 를 한 패스에서 섞어 그리지 않는다(통일 안이면 전부 `SpriteDraw`, 분리 안이면 단계 분리).

---

## 7. 지을 것 + 빌드 순서

1. ✅ `render/r2d/Sprite2D.h` 에 `SpriteDraw`(dest+uv+tint+atlasId+`clip` math::Rect). `RenderSnapshot` 에 `uiSprites`.
2. ✅ `assets/shaders/sprite2d.hlsl` + `render/r2d/SpritePass2D.{h,cpp}` — `(atlasId, clip)` 연속 런 그룹핑, SRV 바인드, `RSSetScissorRects`, 동적 VB `WRITE_DISCARD`/`NO_OVERWRITE`. scissor rasterizer state 는 패스가 소유. `Dx11Renderer::AddRenderPass(pass, atEnd=true)` 신설(scissor state 가 `QuadPass2D` 로 새지 않게 맨 끝) + `main.cpp` 등록. `atlasId 0` = 내장 1×1 흰 텍스처.
3. 최소본 ✅ / 나머지 ❌ — `render/r2d/TextureAtlas.{h,cpp}` 에 `SpriteRect` + `AtlasIndex`(텍스트 `.atlas` 파서, 메인 스레드). `SpritePass2D` 가 `Initialize` 에서 `.dds` 1장 로드(최소 DX10-헤더 DDS 리더, RGBA8/BC7/BC4). `Application` 이 `m_uiAtlas` 로드 → `SnapshotBuilder::Build` 에 넘김(데모 스프라이트). **`AssetKind::Atlas` / `AssetRegistry` / `loading-and-streaming` 연동은 아직** — 지금은 `ModelMeshPass3D` 처럼 로드-원스.
4. ✅ v1 — `tools/atlas_pack.{cpp,bat}` (오프라인, 엔진 빌드 밖): 플랫 `atlas.groups` 파싱 + shelf 패킹 + edge-extend gutter + box-filter 밉 + 무압축 `R8G8B8A8_UNORM_SRGB` `.dds`(DX10 헤더) + `.atlas` + `.cache`(증분). 디코드는 `import::LoadImageFromFile` 재사용. `assets/src/ui/*` → `assets/atlas/ui.{0.dds,atlas}`. fixture 생성기 `tools/make_test_atlas_src.cpp`. ❌ 남음: **BC7/BC4 압축**(`bc7enc` vendor + `--format bc7`), 셀 16px 밉 컷.
5. ❌ `ui::DrawList` + 클립 스택 + `ui::ClipScope`. `Widget::Build(DrawList&, Vec2)` 오버로드. `UIContext::Build` 가 `uiSprites` 채움. 위젯 단계적 이전. (그 전까지 `SnapshotBuilder::BuildDemoUiSprites` 가 임시로 채움.)
6. ❌ `ScrollList` 가 `dl.PushClip(viewport)` 사용.
7. ❌ `GlyphAtlas` + `AddText` 재작성. 5×7 폴백 유지.
8. ❌ `command-playbook.md` #3ea·`ui-architecture.md` #8·`scrollable-list-and-pool.md` §1.5 갱신.

의존: 1·2 완료. 3 은 최소본만(레지스트리 연동은 `loading-and-streaming` 구현과 묶임). 5~6 은 지금 가능(2 이후). 7 은 3 이후.
