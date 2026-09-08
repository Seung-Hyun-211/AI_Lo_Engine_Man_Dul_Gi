#pragma once

#include "core/NonCopyable.h"
#include "math/Math.h"

#include <Windows.h>
#include <string>

namespace engine::platform
{
    // Everything the OS window reports back to the application. The window itself
    // holds no game or UI state; it only translates Win32 messages into these
    // calls. Kept small on purpose (ISP): one cohesive group of window events.
    class IWindowEventSink
    {
    public:
        virtual ~IWindowEventSink() = default;

        virtual void OnKey(int virtualKey, bool down) = 0;
        virtual void OnMouseMove(math::Vec2 position) = 0;
        // button: 0 = left, 1 = right, 2 = middle.
        virtual void OnMouseButton(int button, bool down) = 0;
        virtual void OnFocusLost() = 0;
        virtual void OnResize(int width, int height) = 0;
        virtual void OnClose() = 0;
    };

    // Owns exactly one Win32 top-level window: class registration, the HWND, its
    // client size, and the WndProc. This is the "NativeWindow" from
    // docs/ui-architecture.md, distinct from the in-game UIWindow panel.
    class Win32Window final : private core::NonCopyable
    {
    public:
        struct Config
        {
            std::wstring title{ L"Engine" };
            int width{ 1280 };
            int height{ 720 };
        };

        Win32Window(HINSTANCE instance, const Config& config);
        ~Win32Window();

        // Set once, after the owner is fully constructed, before Show(). Until
        // then window messages are handled by DefWindowProc so a message pumped
        // during creation cannot call into a half-built application.
        void SetEventSink(IWindowEventSink& sink) { m_sink = &sink; }

        void Show();

        // Drains the message queue without blocking. Returns false once WM_QUIT
        // has been seen (the caller should exit its loop).
        [[nodiscard]] bool PumpMessages();

        [[nodiscard]] HWND Handle() const { return m_window; }
        [[nodiscard]] int Width() const { return m_width; }
        [[nodiscard]] int Height() const { return m_height; }

    private:
        static LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam);
        LRESULT HandleMessage(UINT message, WPARAM wParam, LPARAM lParam);

        HWND m_window{};
        int m_width{};
        int m_height{};
        IWindowEventSink* m_sink{};
        bool m_quit{};
    };
}
