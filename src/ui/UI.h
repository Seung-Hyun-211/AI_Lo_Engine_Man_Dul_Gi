#pragma once

#include "math/Math.h"
#include "render/RenderSnapshot.h"

#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace engine::ui
{
    // The UI works in the shared math types; no separate geometry vocabulary.
    using math::Color;
    using math::Rect;
    using math::Vec2;

    // Low-level pixel-space primitives, shared by widgets that draw outside the
    // standard Build path (e.g. ScrollList's virtualized rows clip their own
    // Quads). DrawText uses the built-in 5x7 ASCII bitmap (uppercase, digits,
    // `: - . %`) - the same font TextLine renders.
    void DrawRect(std::vector<engine::render::Quad>& output, Rect rect, Color color);
    void DrawText(std::vector<engine::render::Quad>& output, std::string_view text,
                  Vec2 origin, float pixelScale, Color color);

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
        // Vertical wheel over `position`; `delta` in notches (up = positive).
        // Return true when consumed. Base forwards to children (front-most
        // first) and otherwise does not consume - only widgets that scroll do.
        virtual bool PointerWheel(Vec2 position, float delta, Vec2 parentOrigin);

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

    // A labeled on/off toggle. Same click contract as Button (down and up must
    // land in the same bounds), so it doesn't flip while the user is dragging
    // past it toward something else.
    class CheckBox final : public Widget
    {
    public:
        explicit CheckBox(std::string label) : m_label(std::move(label)) {}
        bool checked{ false };
        std::function<void(bool)> onChanged;
        void Build(std::vector<engine::render::Quad>& output, Vec2 parentOrigin) const override;
        bool PointerMove(Vec2 position, Vec2 parentOrigin) override;
        bool PointerDown(Vec2 position, Vec2 parentOrigin) override;
        bool PointerUp(Vec2 position, Vec2 parentOrigin) override;
    private:
        std::string m_label;
        bool m_hovered{};
        bool m_pressed{};
    };

    // A horizontal drag handle over [minValue, maxValue]. PointerMove keeps
    // updating the value while dragging even if the cursor leaves the track
    // (Widget::PointerMove reaches every widget regardless of position, see
    // UI.cpp), so a fast drag doesn't "lose" the handle.
    //
    // Known gap: PointerUp only reaches this widget if nothing earlier in the
    // sibling traversal already claimed it (see docs/ui-architecture.md
    // "Slider 입력 캡처"). In a normal non-overlapping vertical layout this
    // doesn't come up; it's flagged in CLAUDE.md's known-issues list.
    class Slider final : public Widget
    {
    public:
        Slider(float minValue, float maxValue) : m_min(minValue), m_max(maxValue) {}
        float value{};
        std::function<void(float)> onChanged;
        void Build(std::vector<engine::render::Quad>& output, Vec2 parentOrigin) const override;
        bool PointerMove(Vec2 position, Vec2 parentOrigin) override;
        bool PointerDown(Vec2 position, Vec2 parentOrigin) override;
        bool PointerUp(Vec2 position, Vec2 parentOrigin) override;
    private:
        void SetFromPointerX(float x, Vec2 parentOrigin);
        float m_min;
        float m_max;
        bool m_dragging{};
    };

    // Hosts exactly one full screen (Title, in-game HUD, ...) and, on top of
    // it, at most one modal overlay (e.g. Settings). While an overlay is set,
    // every pointer event goes to it alone - the screen underneath is visible
    // but not interactive. This is the "UIScreen" ui-architecture.md described
    // as unimplemented; see docs/scene-flow-design.md for how Application
    // drives it.
    class UIContext final
    {
    public:
        UIContext() = default;

        void SetScreen(std::unique_ptr<Widget> screen) { m_screen = std::move(screen); }
        void SetOverlay(std::unique_ptr<Widget> overlay) { m_overlay = std::move(overlay); }
        void ClearOverlay() { m_overlay.reset(); }
        [[nodiscard]] bool HasOverlay() const { return m_overlay != nullptr; }

        // Each returns true when the UI consumed the event, so the caller can
        // keep it out of gameplay input for this frame.
        bool PointerMove(Vec2 position);
        bool PointerDown(Vec2 position);
        bool PointerUp(Vec2 position);
        bool PointerWheel(Vec2 position, float delta);
        void Build(std::vector<engine::render::Quad>& output, float viewportWidth, float viewportHeight) const;
    private:
        std::unique_ptr<Widget> m_screen;
        std::unique_ptr<Widget> m_overlay;
    };
}
