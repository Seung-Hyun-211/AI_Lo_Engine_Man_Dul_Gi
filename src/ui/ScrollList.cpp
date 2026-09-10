#include "ui/ScrollList.h"

#include <algorithm>
#include <cmath>

namespace engine::ui
{
    namespace
    {
        using render::Quad;

        constexpr int   kOverscan   = 2;       // extra rows kept live above+below
        constexpr float kBarWidth   = 12.0f;
        constexpr float kMinThumb   = 24.0f;
        constexpr float kWheelRows  = 3.0f;    // rows per wheel notch
        constexpr float kTextScale  = 2.0f;
        constexpr float kTextPixels = 7.0f * kTextScale;
        constexpr float kTextPadX   = 10.0f;

        const Color kViewportBg { 0.09f, 0.10f, 0.14f, 1.0f };
        const Color kBarTrack   { 0.06f, 0.07f, 0.10f, 1.0f };
        const Color kBarThumb   { 0.35f, 0.45f, 0.62f, 1.0f };
        const Color kBarThumbHot{ 0.55f, 0.80f, 1.00f, 1.0f };
        const Color kRowText    { 0.90f, 0.94f, 1.00f, 1.0f };
        const Color kSelectFill { 0.25f, 0.55f, 0.95f, 0.40f };
        const Color kHoverFill  { 1.00f, 1.00f, 1.00f, 0.08f };

        // Clip `q` to `clip` in place; false if nothing is left (fully outside).
        // Works for both the row background and every glyph quad, so text is cut
        // cleanly at the viewport edge (docs/scrollable-list-and-pool.md §1.5 A).
        bool ClampQuad(Quad& q, const Rect& clip)
        {
            const float x0 = std::max(q.x, clip.x);
            const float y0 = std::max(q.y, clip.y);
            const float x1 = std::min(q.x + q.width, clip.x + clip.width);
            const float y1 = std::min(q.y + q.height, clip.y + clip.height);
            if (x1 <= x0 || y1 <= y0) return false;
            q.x = x0; q.y = y0; q.width = x1 - x0; q.height = y1 - y0;
            return true;
        }
    }

    // One recycled row. Never destroyed while the list lives; Build() rebinds
    // `text`/`tint` in place (std::string::assign keeps the buffer, so a
    // rebind allocates nothing after warm-up). Holds no selection/hover/scroll
    // state - that lives on ScrollList keyed by data index.
    struct ScrollList::Row final : RowView
    {
        std::string text;
        Color tint{ 0.13f, 0.15f, 0.20f, 1.0f };
        std::vector<Quad> scratch;   // reused each Emit, capacity survives

        void SetText(std::string_view t) override { text.assign(t.data(), t.size()); }
        void SetTint(Color c) override { tint = c; }

        void Emit(std::vector<Quad>& out, const Rect& rowRect, const Rect& clip,
                  bool selected, bool hovered)
        {
            scratch.clear();
            DrawRect(scratch, rowRect, tint);
            if (selected)     DrawRect(scratch, rowRect, kSelectFill);
            else if (hovered) DrawRect(scratch, rowRect, kHoverFill);
            const float textY = rowRect.y + (rowRect.height - kTextPixels) * 0.5f;
            DrawText(scratch, text, { rowRect.x + kTextPadX, textY }, kTextScale, kRowText);

            for (Quad q : scratch)
                if (ClampQuad(q, clip)) out.push_back(q);
        }
    };

    ScrollList::ScrollList() = default;
    ScrollList::~ScrollList() = default;

    void ScrollList::SetModel(std::unique_ptr<ListModel> model)
    {
        m_model = std::move(model);
        m_selectedIndex = -1;
        m_hoverIndex = -1;
        m_scrollOffset = 0.0f;
    }

    std::size_t ScrollList::ModelCount() const { return m_model ? m_model->Count() : 0; }

    ScrollList::Layout ScrollList::Compute(Vec2 parentOrigin) const
    {
        const Rect abs = AbsoluteBounds(parentOrigin);
        Layout layout;
        layout.count = ModelCount();
        const float rowsWidth = std::max(0.0f, abs.width - kBarWidth);
        layout.viewport = { abs.x, abs.y, rowsWidth, abs.height };
        layout.bar = { abs.x + rowsWidth, abs.y, kBarWidth, abs.height };
        layout.contentHeight = static_cast<float>(layout.count) * m_rowHeight;
        layout.maxScroll = std::max(0.0f, layout.contentHeight - layout.viewport.height);
        return layout;
    }

    void ScrollList::ClampScroll()
    {
        const float contentH = static_cast<float>(ModelCount()) * m_rowHeight;
        const float maxScroll = std::max(0.0f, contentH - m_bounds.height);
        m_scrollOffset = std::clamp(m_scrollOffset, 0.0f, maxScroll);
    }

    ScrollList::Thumb ScrollList::ThumbFor(const Layout& layout) const
    {
        Thumb thumb;
        const float ratio = layout.contentHeight > 1.0f
            ? layout.viewport.height / layout.contentHeight : 1.0f;   // fraction of content shown
        thumb.height = std::max(kMinThumb, layout.bar.height * ratio);
        thumb.trackSpan = std::max(1.0f, layout.bar.height - thumb.height);
        const float t = layout.maxScroll > 0.0f ? m_scrollOffset / layout.maxScroll : 0.0f;
        thumb.y = layout.bar.y + thumb.trackSpan * t;
        return thumb;
    }

    int ScrollList::IndexAt(const Layout& layout, float pointerY) const
    {
        if (pointerY < layout.viewport.y || pointerY >= layout.viewport.y + layout.viewport.height)
            return -1;
        const float scroll = std::clamp(m_scrollOffset, 0.0f, layout.maxScroll);
        const float contentY = pointerY - layout.viewport.y + scroll;
        const int di = static_cast<int>(std::floor(contentY / m_rowHeight));
        return (di >= 0 && di < static_cast<int>(layout.count)) ? di : -1;
    }

    void ScrollList::EnsureVisible(std::size_t index)
    {
        const float top = static_cast<float>(index) * m_rowHeight;
        const float bottom = top + m_rowHeight;
        if (top < m_scrollOffset) m_scrollOffset = top;
        else if (bottom > m_scrollOffset + m_bounds.height) m_scrollOffset = bottom - m_bounds.height;
        ClampScroll();
    }

    void ScrollList::Build(std::vector<Quad>& output, Vec2 parentOrigin) const
    {
        const Layout layout = Compute(parentOrigin);
        m_scrollOffset = std::clamp(m_scrollOffset, 0.0f, layout.maxScroll);

        DrawRect(output, layout.viewport, kViewportBg);

        if (m_model != nullptr && layout.count > 0)
        {
            const int poolSize =
                static_cast<int>(std::ceil(layout.viewport.height / m_rowHeight)) + 1 + kOverscan;
            while (static_cast<int>(m_rows.size()) < poolSize)
                m_rows.push_back(std::make_unique<Row>());

            const int first = static_cast<int>(std::floor(m_scrollOffset / m_rowHeight));
            const float pxWithinFirst = m_scrollOffset - static_cast<float>(first) * m_rowHeight;

            for (int k = 0; k < poolSize; ++k)
            {
                const int di = first + k;
                if (di >= static_cast<int>(layout.count)) break;
                const float rowTop = layout.viewport.y - pxWithinFirst + static_cast<float>(k) * m_rowHeight;
                if (rowTop >= layout.viewport.y + layout.viewport.height) break;

                Row& row = *m_rows[static_cast<std::size_t>(k)];
                m_model->BindRow(row, static_cast<std::size_t>(di));
                row.Emit(output, { layout.viewport.x, rowTop, layout.viewport.width, m_rowHeight },
                         layout.viewport, di == m_selectedIndex, di == m_hoverIndex);
            }
        }

        // Scrollbar: track always, thumb only when the content overflows.
        DrawRect(output, layout.bar, kBarTrack);
        if (layout.maxScroll > 0.0f)
        {
            const Thumb thumb = ThumbFor(layout);
            DrawRect(output, { layout.bar.x + 2.0f, thumb.y, layout.bar.width - 4.0f, thumb.height },
                     m_barDragging ? kBarThumbHot : kBarThumb);
        }
    }

    bool ScrollList::PointerWheel(Vec2 position, float delta, Vec2 parentOrigin)
    {
        const Layout layout = Compute(parentOrigin);
        if (!layout.viewport.Contains(position) && !layout.bar.Contains(position)) return false;
        // delta > 0 = wheel up = scroll toward the top = offset decreases.
        m_scrollOffset = std::clamp(m_scrollOffset - delta * (m_rowHeight * kWheelRows),
                                    0.0f, layout.maxScroll);
        return true;
    }

    bool ScrollList::PointerMove(Vec2 position, Vec2 parentOrigin)
    {
        const Layout layout = Compute(parentOrigin);
        if (m_barDragging)
        {
            const Thumb thumb = ThumbFor(layout);
            const float top = std::clamp(position.y - m_barGrabOffset,
                                         layout.bar.y, layout.bar.y + thumb.trackSpan);
            m_scrollOffset = std::clamp((top - layout.bar.y) / thumb.trackSpan * layout.maxScroll,
                                        0.0f, layout.maxScroll);
            return true;
        }
        m_hoverIndex = IndexAt(layout, position.y);
        return layout.viewport.Contains(position) || layout.bar.Contains(position);
    }

    bool ScrollList::PointerDown(Vec2 position, Vec2 parentOrigin)
    {
        const Layout layout = Compute(parentOrigin);

        if (layout.bar.Contains(position) && layout.maxScroll > 0.0f)
        {
            const Thumb thumb = ThumbFor(layout);
            if (position.y < thumb.y || position.y > thumb.y + thumb.height)
            {
                // Clicked the track: jump so the thumb centres on the cursor.
                const float top = std::clamp(position.y - thumb.height * 0.5f,
                                             layout.bar.y, layout.bar.y + thumb.trackSpan);
                m_scrollOffset = std::clamp((top - layout.bar.y) / thumb.trackSpan * layout.maxScroll,
                                            0.0f, layout.maxScroll);
                m_barGrabOffset = thumb.height * 0.5f;
            }
            else
            {
                m_barGrabOffset = position.y - thumb.y;   // grab where the cursor landed
            }
            m_barDragging = true;
            return true;
        }

        if (layout.viewport.Contains(position))
        {
            const int di = IndexAt(layout, position.y);
            if (di >= 0)
            {
                m_selectedIndex = di;
                if (onSelect) onSelect(static_cast<std::size_t>(di));
            }
            return true;
        }
        return false;
    }

    bool ScrollList::PointerUp(Vec2 /*position*/, Vec2 /*parentOrigin*/)
    {
        if (!m_barDragging) return false;
        m_barDragging = false;
        return true;
    }
}
