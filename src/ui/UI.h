#pragma once

#include "render/RenderSnapshot.h"

#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace engine::ui
{
    struct Vec2 { float x{}, y{}; };
    struct Rect
    {
        float x{}, y{}, width{}, height{};
        [[nodiscard]] bool Contains(Vec2 point) const
        {
            return point.x >= x && point.x < x + width && point.y >= y && point.y < y + height;
        }
    };
    struct Color { float r{}, g{}, b{}, a{ 1.0f }; };

    // Widgets use local coordinates. The parent turns them into screen-space
    // render commands, keeping UI independent from Direct3D and the render thread.
    class Widget
    {
    public:
        virtual ~Widget() = default;
        void SetBounds(Rect bounds) { m_bounds = bounds; }
        void AddChild(std::unique_ptr<Widget> child);
        virtual void Build(std::vector<engine::render::Quad>& output, Vec2 parentOrigin) const;
        virtual bool PointerMove(Vec2 position, Vec2 parentOrigin);
        virtual bool PointerDown(Vec2 position, Vec2 parentOrigin);
        virtual bool PointerUp(Vec2 position, Vec2 parentOrigin);

    protected:
        [[nodiscard]] Rect AbsoluteBounds(Vec2 parentOrigin) const;
        std::vector<std::unique_ptr<Widget>> m_children;
        Rect m_bounds{};
    };

    class UIWindow final : public Widget
    {
    public:
        Color background{ 0.10f, 0.13f, 0.20f, 0.96f };
        Color border{ 0.30f, 0.50f, 0.75f, 1.0f };
        void Build(std::vector<engine::render::Quad>& output, Vec2 parentOrigin) const override;
    };

    class TextLine final : public Widget
    {
    public:
        explicit TextLine(std::string text) : m_text(std::move(text)) {}
        void SetText(std::string text) { m_text = std::move(text); }
        void Build(std::vector<engine::render::Quad>& output, Vec2 parentOrigin) const override;
        Color color{ 0.92f, 0.96f, 1.0f, 1.0f };
        float pixelScale{ 2.0f };
    private:
        std::string m_text;
    };

    class Button final : public Widget
    {
    public:
        explicit Button(std::string label) : m_label(std::move(label)) {}
        std::function<void()> onClick;
        void Build(std::vector<engine::render::Quad>& output, Vec2 parentOrigin) const override;
        bool PointerMove(Vec2 position, Vec2 parentOrigin) override;
        bool PointerDown(Vec2 position, Vec2 parentOrigin) override;
        bool PointerUp(Vec2 position, Vec2 parentOrigin) override;
    private:
        std::string m_label;
        bool m_hovered{};
        bool m_pressed{};
    };

    class UIContext final
    {
    public:
        UIContext();
        void PointerMove(Vec2 position);
        void PointerDown(Vec2 position);
        void PointerUp(Vec2 position);
        void Build(std::vector<engine::render::Quad>& output) const;
    private:
        std::unique_ptr<UIWindow> m_root;
        TextLine* m_status{}; // owned by m_root's child tree
    };
}
