#pragma once

#include "math/Math.h"

#include <array>
#include <bitset>

namespace engine::input
{
    // This frame's keyboard and mouse state, plus one-frame edge queries
    // (Pressed / Released). The OS window feeds it raw events; gameplay reads it.
    //
    // Frame contract:
    //   BeginFrame()              -> roll "current" into "previous", clear edges
    //   window pumps messages     -> On* mutate "current" and set edge flags
    //   gameplay reads Down/Pressed/Released for the frame
    //
    // Edge flags are set on the event itself (not derived only from the
    // previous/current diff) so a key that is pressed and released inside one
    // frame is still reported by both Pressed() and Released().
    class InputState
    {
    public:
        static constexpr int kKeyCount = 256;   // Win32 virtual-key range
        static constexpr int kButtonCount = 3;  // left, right, middle

        // Clears only the one-frame edge flags. Held state (Down) persists until
        // the matching key-up or a focus loss.
        void BeginFrame()
        {
            m_pressedKeys.reset();
            m_releasedKeys.reset();
            m_pressedButtons.fill(false);
            m_releasedButtons.fill(false);
            m_mouseDelta = {};
        }

        void OnKey(int virtualKey, bool down)
        {
            if (virtualKey < 0 || virtualKey >= kKeyCount) return;
            if (down && !m_keys.test(virtualKey)) m_pressedKeys.set(virtualKey);
            if (!down && m_keys.test(virtualKey)) m_releasedKeys.set(virtualKey);
            m_keys.set(virtualKey, down);
        }

        void OnMouseMove(math::Vec2 position) { m_mouse = position; }

        // Relative motion (raw input) accumulated across the frame; used for
        // mouse-look while the pointer is locked. Cleared by BeginFrame().
        void OnMouseDelta(math::Vec2 delta) { m_mouseDelta.x += delta.x; m_mouseDelta.y += delta.y; }

        void OnMouseButton(int button, bool down)
        {
            if (button < 0 || button >= kButtonCount) return;
            if (down && !m_buttons[button]) m_pressedButtons[button] = true;
            if (!down && m_buttons[button]) m_releasedButtons[button] = true;
            m_buttons[button] = down;
        }

        // Losing focus can swallow the matching key-up messages; treat every key
        // and button as released so movement does not stick.
        void OnFocusLost()
        {
            m_keys.reset();
            m_buttons.fill(false);
            m_mouseDelta = {};
        }

        [[nodiscard]] bool KeyDown(int virtualKey) const
        {
            return virtualKey >= 0 && virtualKey < kKeyCount && m_keys.test(virtualKey);
        }
        [[nodiscard]] bool KeyPressed(int virtualKey) const
        {
            return virtualKey >= 0 && virtualKey < kKeyCount && m_pressedKeys.test(virtualKey);
        }
        [[nodiscard]] bool KeyReleased(int virtualKey) const
        {
            return virtualKey >= 0 && virtualKey < kKeyCount && m_releasedKeys.test(virtualKey);
        }

        [[nodiscard]] math::Vec2 MousePosition() const { return m_mouse; }
        [[nodiscard]] math::Vec2 MouseDelta() const { return m_mouseDelta; }
        [[nodiscard]] bool MouseDown(int button) const
        {
            return button >= 0 && button < kButtonCount && m_buttons[button];
        }
        [[nodiscard]] bool MousePressed(int button) const
        {
            return button >= 0 && button < kButtonCount && m_pressedButtons[button];
        }
        [[nodiscard]] bool MouseReleased(int button) const
        {
            return button >= 0 && button < kButtonCount && m_releasedButtons[button];
        }

    private:
        std::bitset<kKeyCount> m_keys;
        std::bitset<kKeyCount> m_pressedKeys;
        std::bitset<kKeyCount> m_releasedKeys;

        std::array<bool, kButtonCount> m_buttons{};
        std::array<bool, kButtonCount> m_pressedButtons{};
        std::array<bool, kButtonCount> m_releasedButtons{};

        math::Vec2 m_mouse{};
        math::Vec2 m_mouseDelta{};
    };
}
