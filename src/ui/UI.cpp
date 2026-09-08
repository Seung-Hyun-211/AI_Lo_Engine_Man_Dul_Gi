#include "ui/UI.h"

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
        case ':': return { 0,4,4,0,4,4,0 }; case '-': return { 0,0,0,31,0,0,0 };
        default: return { 0,0,0,0,0,0,0 };
        }
    }

    void AddText(std::vector<Quad>& output, const std::string& text, Vec2 origin, float scale, Color color)
    {
        float x = origin.x;
        for (const char character : text)
        {
            for (int row = 0; row < 7; ++row)
            {
                const unsigned char bits = Glyph(character)[row];
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

    UIContext::UIContext()
    {
        m_root = std::make_unique<UIWindow>();
        m_root->SetBounds({ 20, 20, 332, 170 });
        auto title = std::make_unique<TextLine>("CPP WINDOW GAME");
        title->SetBounds({ 16, 16, 0, 0 }); title->pixelScale = 2.0f;
        m_root->AddChild(std::move(title));
        auto button = std::make_unique<Button>("START");
        button->SetBounds({ 16, 52, 300, 42 });
        button->onClick = [this] { m_status->SetText("STATUS: RUNNING"); };
        m_root->AddChild(std::move(button));
        auto status = std::make_unique<TextLine>("STATUS: READY");
        status->SetBounds({ 16, 116, 0, 0 }); status->pixelScale = 2.0f;
        m_status = status.get();
        m_root->AddChild(std::move(status));
    }
    void UIContext::PointerMove(Vec2 position) { m_root->PointerMove(position, {}); }
    void UIContext::PointerDown(Vec2 position) { m_root->PointerDown(position, {}); }
    void UIContext::PointerUp(Vec2 position) { m_root->PointerUp(position, {}); }
    void UIContext::Build(std::vector<Quad>& output) const { m_root->Build(output, {}); }
}
