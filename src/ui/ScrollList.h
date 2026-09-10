#pragma once

#include "ui/UI.h"

#include <cstddef>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

// A scrollable window over a long list. The model may hold N items; the widget
// keeps only the rows that fit the viewport (+ overscan) live and rebinds them
// as the view scrolls - no per-item widget, no allocation while scrolling
// (UI virtualization: RecyclerView / UITableView cell reuse).
//
// Design contract: docs/scrollable-list-and-pool.md. This is the v1 there:
// fixed row height, clipping option A (the widget clamps its own Quads to the
// viewport - the renderer is untouched), wheel via UIContext::PointerWheel.
namespace engine::ui
{
    // ISP-minimal handle a ListModel writes one row through. It never sees the
    // Row or the widget - the same boundary as UIRenderer in ui-architecture.md.
    class RowView
    {
    public:
        virtual ~RowView() = default;
        virtual void SetText(std::string_view text) = 0;
        virtual void SetTint(Color tint) = 0;
        // Extension point: SetIcon, SetSubText, ... (see doc "행 모양 바꾸기").
    };

    // The data source. `ui/` does not know what an item is (DIP), exactly as the
    // renderer does not know what `playerX` is. The game side implements this.
    class ListModel
    {
    public:
        virtual ~ListModel() = default;
        [[nodiscard]] virtual std::size_t Count() const = 0;
        // Called every frame for each visible row - reference already-loaded
        // data, do no IO or heap growth here.
        virtual void BindRow(RowView& row, std::size_t index) const = 0;
    };

    class ScrollList final : public Widget
    {
    public:
        ScrollList();
        ~ScrollList() override;

        void SetModel(std::unique_ptr<ListModel> model);
        void SetRowHeight(float rowHeight) { m_rowHeight = rowHeight > 1.0f ? rowHeight : 1.0f; }
        // Re-clamp the scroll offset after the data changes. Recreates no row.
        void NotifyModelChanged() { ClampScroll(); }
        void EnsureVisible(std::size_t index);

        std::function<void(std::size_t)> onSelect;

        void Build(std::vector<render::Quad>& output, Vec2 parentOrigin) const override;
        bool PointerMove(Vec2 position, Vec2 parentOrigin) override;
        bool PointerDown(Vec2 position, Vec2 parentOrigin) override;
        bool PointerUp(Vec2 position, Vec2 parentOrigin) override;
        bool PointerWheel(Vec2 position, float delta, Vec2 parentOrigin) override;

    private:
        struct Row;

        // Absolute-space geometry the handlers and Build all need, derived from
        // m_bounds + parentOrigin + the model. `viewport` excludes the bar.
        struct Layout
        {
            Rect viewport{};
            Rect bar{};
            float contentHeight{};
            float maxScroll{};
            std::size_t count{};
        };
        [[nodiscard]] Layout Compute(Vec2 parentOrigin) const;
        [[nodiscard]] std::size_t ModelCount() const;
        void ClampScroll();

        // Scrollbar thumb geometry for the current scroll offset. One source for
        // Build (draw), PointerDown (hit-test), PointerMove (drag).
        struct Thumb { float height{}; float trackSpan{}; float y{}; };
        [[nodiscard]] Thumb ThumbFor(const Layout& layout) const;
        // Data index under a pointer y inside the viewport, or -1.
        [[nodiscard]] int IndexAt(const Layout& layout, float pointerY) const;

        std::unique_ptr<ListModel> m_model;
        float m_rowHeight{ 28.0f };
        // px into the content, [0, maxScroll]. mutable: Build re-clamps it to the
        // current geometry (idempotent housekeeping after a resize), it is not
        // semantic state that Build owns.
        mutable float m_scrollOffset{ 0.0f };
        int m_selectedIndex{ -1 };
        int m_hoverIndex{ -1 };

        // The recycler: enough Row buffers for the viewport + overscan, grown
        // only when the viewport gets taller. Never cleared - Build only
        // rebinds. mutable because Build (const) rebinds text/tint in place.
        mutable std::vector<std::unique_ptr<Row>> m_rows;

        bool m_barDragging{ false };
        float m_barGrabOffset{ 0.0f };   // cursor y - thumb top, at grab time
    };
}
