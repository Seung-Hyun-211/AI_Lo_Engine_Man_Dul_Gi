#include "ui/UI.h"

#include <algorithm>
#include <array>
#include <cctype>

namespace
{
    using engine::render::Quad;
    using engine::ui::Color;
    using engine::ui::Rect;
    using engine::ui::Vec2;

    void AddQuad(std::vector<Quad>& output, Rect rect, Color color)
    {
        output.push_back({ rect.x, rect.y, rect.width, rect.height, color.r, color.g, color.b, color.a });
    }

    // A compact built-in 5x7 ASCII font lets the first UI be self-contained.
    // Replace this implementation with a glyph atlas when localization arrives.
    std::array<unsigned char, 7> Glyph(char character)
    {
        switch (static_cast<char>(std::toupper(static_cast<unsigned char>(character))))
        {
        case 'A': return { 14,17,17,31,17,17,17 }; case 'B': return { 30,17,17,30,17,17,30 };
        case 'C': return { 15,16,16,16,16,16,15 }; case 'D': return { 30,17,17,17,17,17,30 };
        case 'E': return { 31,16,16,30,16,16,31 }; case 'F': return { 31,16,16,30,16,16,16 };
        case 'G': return { 15,16,16,23,17,17,15 }; case 'H': return { 17,17,17,31,17,17,17 };
        case 'I': return { 31,4,4,4,4,4,31 }; case 'J': return { 1,1,1,1,17,17,14 };
        case 'K': return { 17,18,20,24,20,18,17 }; case 'L': return { 16,16,16,16,16,16,31 };
        case 'M': return { 17,27,21,21,17,17,17 }; case 'N': return { 17,25,21,19,17,17,17 };
        case 'O': return { 14,17,17,17,17,17,14 }; case 'P': return { 30,17,17,30,16,16,16 };
        case 'Q': return { 14,17,17,17,21,18,13 }; case 'R': return { 30,17,17,30,20,18,17 };
        case 'S': return { 15,16,16,14,1,1,30 }; case 'T': return { 31,4,4,4,4,4,4 };
        case 'U': return { 17,17,17,17,17,17,14 }; case 'V': return { 17,17,17,17,17,10,4 };
        case 'W': return { 17,17,17,21,21,21,10 }; case 'X': return { 17,17,10,4,10,17,17 };
        case 'Y': return { 17,17,10,4,4,4,4 }; case 'Z': return { 31,1,2,4,8,16,31 };
        case '0': return { 14,17,19,21,25,17,14 }; case '1': return { 4,12,4,4,4,4,14 };
        case '2': return { 14,17,1,2,4,8,31 }; case '3': return { 30,1,1,14,1,1,30 };
        case '4': return { 2,6,10,18,31,2,2 }; case '5': return { 31,16,30,1,1,17,14 };
        case '6': return { 6,8,16,30,17,17,14 }; case '7': return { 31,1,2,4,8,8,8 };
        case '8': return { 14,17,17,14,17,17,14 }; case '9': return { 14,17,17,15,1,2,12 };
        case ':': return { 0,4,4,0,4,4,0 }; case '-': return { 0,0,0,31,0,0,0 };
        case '.': return { 0,0,0,0,0,12,12 }; case '%': return { 25,26,4,8,19,0,0 };
        default: return { 0,0,0,0,0,0,0 };
        }
    }

    void AddText(std::vector<Quad>& output, std::string_view text, Vec2 origin, float scale, Color color)
    {
        float x = origin.x;
        for (const char character : text)
        {
            const std::array<unsigned char, 7> glyph = Glyph(character);
            for (int row = 0; row < 7; ++row)
            {
                const unsigned char bits = glyph[row];
                for (int column = 0; column < 5; ++column)
                    if ((bits & (1u << (4 - column))) != 0)
                        AddQuad(output, { x + column * scale, origin.y + row * scale, scale, scale }, color);
            }
            x += 6.0f * scale;
        }
    }
}

namespace engine::ui
{
    void DrawRect(std::vector<Quad>& output, Rect rect, Color color) { AddQuad(output, rect, color); }
    void DrawText(std::vector<Quad>& output, std::string_view text, Vec2 origin, float pixelScale, Color color)
    {
        AddText(output, text, origin, pixelScale, color);
    }

    void Widget::AddChild(std::unique_ptr<Widget> child) { m_children.push_back(std::move(child)); }
    Rect Widget::AbsoluteBounds(Vec2 origin) const { return { origin.x + m_bounds.x, origin.y + m_bounds.y, m_bounds.width, m_bounds.height }; }
    void Widget::Build(std::vector<Quad>& output, Vec2 parentOrigin) const
    {
        const Rect bounds = AbsoluteBounds(parentOrigin);
        for (const auto& child : m_children) child->Build(output, { bounds.x, bounds.y });
    }
    bool Widget::PointerMove(Vec2 position, Vec2 parentOrigin)
    {
        const Rect bounds = AbsoluteBounds(parentOrigin);
        bool handled = false;
        for (auto child = m_children.rbegin(); child != m_children.rend(); ++child)
            handled = (*child)->PointerMove(position, { bounds.x, bounds.y }) || handled;
        // Hover state must reach every widget; unlike clicks it is not consumed
        // by an overlapping text element before a button can clear its state.
        return handled || bounds.Contains(position);
    }
    bool Widget::PointerDown(Vec2 position, Vec2 parentOrigin)
    {
        const Rect bounds = AbsoluteBounds(parentOrigin);
        for (auto child = m_children.rbegin(); child != m_children.rend(); ++child)
            if ((*child)->PointerDown(position, { bounds.x, bounds.y })) return true;
        return bounds.Contains(position);
    }
    bool Widget::PointerUp(Vec2 position, Vec2 parentOrigin)
    {
        const Rect bounds = AbsoluteBounds(parentOrigin);
        for (auto child = m_children.rbegin(); child != m_children.rend(); ++child)
            if ((*child)->PointerUp(position, { bounds.x, bounds.y })) return true;
        return bounds.Contains(position);
    }
    bool Widget::PointerWheel(Vec2 position, float delta, Vec2 parentOrigin)
    {
        const Rect bounds = AbsoluteBounds(parentOrigin);
        for (auto child = m_children.rbegin(); child != m_children.rend(); ++child)
            if ((*child)->PointerWheel(position, delta, { bounds.x, bounds.y })) return true;
        return false;   // a plain container does not consume the wheel
    }

    void UIWindow::Build(std::vector<Quad>& output, Vec2 parentOrigin) const
    {
        const Rect bounds = AbsoluteBounds(parentOrigin);
        AddQuad(output, bounds, background);
        AddQuad(output, { bounds.x, bounds.y, bounds.width, 2 }, border);
        AddQuad(output, { bounds.x, bounds.y + bounds.height - 2, bounds.width, 2 }, border);
        AddQuad(output, { bounds.x, bounds.y, 2, bounds.height }, border);
        AddQuad(output, { bounds.x + bounds.width - 2, bounds.y, 2, bounds.height }, border);
        for (const auto& child : m_children) child->Build(output, { bounds.x, bounds.y });
    }
    void TextLine::Build(std::vector<Quad>& output, Vec2 parentOrigin) const
    {
        const Rect bounds = AbsoluteBounds(parentOrigin);
        AddText(output, m_text, { bounds.x, bounds.y }, pixelScale, color);
    }
    void Button::Build(std::vector<Quad>& output, Vec2 parentOrigin) const
    {
        const Rect bounds = AbsoluteBounds(parentOrigin);
        const Color background = m_pressed ? Color{ .12f,.38f,.62f,1 } : m_hovered ? Color{ .16f,.48f,.76f,1 } : Color{ .12f,.27f,.46f,1 };
        AddQuad(output, bounds, background);
        AddQuad(output, { bounds.x, bounds.y, bounds.width, 1 }, { .70f,.88f,1,1 });
        AddText(output, m_label, { bounds.x + 12, bounds.y + 12 }, 2.0f, { .96f,.98f,1,1 });
    }
    bool Button::PointerMove(Vec2 position, Vec2 parentOrigin)
    {
        m_hovered = AbsoluteBounds(parentOrigin).Contains(position);
        return m_hovered;
    }
    bool Button::PointerDown(Vec2 position, Vec2 parentOrigin)
    {
        m_pressed = AbsoluteBounds(parentOrigin).Contains(position);
        return m_pressed;
    }
    bool Button::PointerUp(Vec2 position, Vec2 parentOrigin)
    {
        const bool clicked = m_pressed && AbsoluteBounds(parentOrigin).Contains(position);
        m_pressed = false;
        if (clicked && onClick) onClick();
        return clicked;
    }

    void CheckBox::Build(std::vector<Quad>& output, Vec2 parentOrigin) const
    {
        const Rect bounds = AbsoluteBounds(parentOrigin);
        constexpr float kBoxSize = 20.0f;
        const Rect box{ bounds.x, bounds.y + (bounds.height - kBoxSize) * 0.5f, kBoxSize, kBoxSize };
        const Color boxColor = m_pressed ? Color{ .16f,.48f,.76f,1 } : m_hovered ? Color{ .14f,.34f,.56f,1 } : Color{ .10f,.22f,.36f,1 };
        AddQuad(output, box, boxColor);
        AddQuad(output, { box.x, box.y, box.width, 1 }, { .70f,.88f,1,1 });
        AddQuad(output, { box.x, box.y + box.height - 1, box.width, 1 }, { .70f,.88f,1,1 });
        AddQuad(output, { box.x, box.y, 1, box.height }, { .70f,.88f,1,1 });
        AddQuad(output, { box.x + box.width - 1, box.y, 1, box.height }, { .70f,.88f,1,1 });
        if (checked) AddQuad(output, { box.x + 4, box.y + 4, box.width - 8, box.height - 8 }, { .30f,.85f,.55f,1 });
        AddText(output, m_label, { bounds.x + kBoxSize + 12, bounds.y + (bounds.height - 14) * 0.5f }, 2.0f, { .92f,.96f,1,1 });
    }
    bool CheckBox::PointerMove(Vec2 position, Vec2 parentOrigin)
    {
        m_hovered = AbsoluteBounds(parentOrigin).Contains(position);
        return m_hovered;
    }
    bool CheckBox::PointerDown(Vec2 position, Vec2 parentOrigin)
    {
        m_pressed = AbsoluteBounds(parentOrigin).Contains(position);
        return m_pressed;
    }
    bool CheckBox::PointerUp(Vec2 position, Vec2 parentOrigin)
    {
        const bool clicked = m_pressed && AbsoluteBounds(parentOrigin).Contains(position);
        m_pressed = false;
        if (clicked)
        {
            checked = !checked;
            if (onChanged) onChanged(checked);
        }
        return clicked;
    }

    void Slider::Build(std::vector<Quad>& output, Vec2 parentOrigin) const
    {
        const Rect bounds = AbsoluteBounds(parentOrigin);
        constexpr float kTrackHeight = 6.0f;
        constexpr float kHandleWidth = 14.0f;
        const Rect track{ bounds.x, bounds.y + (bounds.height - kTrackHeight) * 0.5f, bounds.width, kTrackHeight };
        AddQuad(output, track, { .10f,.13f,.20f,1 });
        const float range = m_max - m_min;
        const float t = range > 1e-6f ? (value - m_min) / range : 0.0f;
        const float handleX = bounds.x + t * std::max(0.0f, bounds.width - kHandleWidth);
        AddQuad(output, { bounds.x, track.y, handleX - bounds.x + kHandleWidth * 0.5f, kTrackHeight }, { .30f,.60f,.85f,1 });
        AddQuad(output, { handleX, bounds.y, kHandleWidth, bounds.height }, m_dragging ? Color{ .55f,.80f,1,1 } : Color{ .40f,.65f,.92f,1 });
    }
    bool Slider::PointerMove(Vec2 position, Vec2 parentOrigin)
    {
        if (m_dragging) { SetFromPointerX(position.x, parentOrigin); return true; }
        return AbsoluteBounds(parentOrigin).Contains(position);
    }
    bool Slider::PointerDown(Vec2 position, Vec2 parentOrigin)
    {
        if (!AbsoluteBounds(parentOrigin).Contains(position)) return false;
        m_dragging = true;
        SetFromPointerX(position.x, parentOrigin);
        return true;
    }
    bool Slider::PointerUp(Vec2 /*position*/, Vec2 /*parentOrigin*/)
    {
        if (!m_dragging) return false;
        m_dragging = false;
        return true;
    }
    void Slider::SetFromPointerX(float x, Vec2 parentOrigin)
    {
        const Rect bounds = AbsoluteBounds(parentOrigin);
        const float t = bounds.width > 1e-6f ? std::clamp((x - bounds.x) / bounds.width, 0.0f, 1.0f) : 0.0f;
        value = m_min + t * (m_max - m_min);
        if (onChanged) onChanged(value);
    }

    bool UIContext::PointerMove(Vec2 position)
    {
        if (m_overlay) return m_overlay->PointerMove(position, {});
        return m_screen ? m_screen->PointerMove(position, {}) : false;
    }
    bool UIContext::PointerDown(Vec2 position)
    {
        if (m_overlay) return m_overlay->PointerDown(position, {});
        return m_screen ? m_screen->PointerDown(position, {}) : false;
    }
    bool UIContext::PointerUp(Vec2 position)
    {
        if (m_overlay) return m_overlay->PointerUp(position, {});
        return m_screen ? m_screen->PointerUp(position, {}) : false;
    }
    bool UIContext::PointerWheel(Vec2 position, float delta)
    {
        if (m_overlay) return m_overlay->PointerWheel(position, delta, {});
        return m_screen ? m_screen->PointerWheel(position, delta, {}) : false;
    }
    void UIContext::Build(std::vector<Quad>& output, float viewportWidth, float viewportHeight) const
    {
        if (m_screen) m_screen->Build(output, {});
        if (m_overlay)
        {
            // Dim the screen behind the modal overlay so it reads as inactive.
            AddQuad(output, { 0, 0, viewportWidth, viewportHeight }, { 0, 0, 0, 0.45f });
            m_overlay->Build(output, {});
        }
    }
}
