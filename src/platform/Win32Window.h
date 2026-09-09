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
        // Relative mouse motion (raw input), delivered only while the pointer is
        // locked (SetPointerLocked(true)). Absolute OnMouseMove keeps firing too.
        virtual void OnMouseDelta(math::Vec2 delta) = 0;
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

        // Resizes the window so its client area becomes clientWidth x
        // clientHeight (e.g. from a Settings resolution change). Main thread
        // only - same rule as every other window call. SetWindowPos delivers
        // WM_SIZE synchronously on this thread, so it reaches
        // IWindowEventSink::OnResize exactly like a user dragging the border;
        // callers don't need a separate code path for it.
        void RequestResize(int clientWidth, int clientHeight);

        // Starts the normal Win32 close sequence (WM_DESTROY -> PostQuitMessage
        // -> PumpMessages returns false). Main thread only.
        void RequestClose();

        // Locks the pointer for mouse-look: hides the cursor, recenters it, and
        // clips it to the client area, and starts delivering raw relative motion
        // through IWindowEventSink::OnMouseDelta. Automatically suspended while
        // the window is not foreground and re-applied on focus. Main thread only.
        void SetPointerLocked(bool locked);

        [[nodiscard]] HWND Handle() const { return m_window; }
        [[nodiscard]] int Width() const { return m_width; }
        [[nodiscard]] int Height() const { return m_height; }

    private:
        static LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam);
        LRESULT HandleMessage(HWND window, UINT message, WPARAM wParam, LPARAM lParam);
        // Reconciles the actual cursor lock with m_pointerLockDesired + focus.
        void ApplyPointerLock();

        HWND m_window{};
        int m_width{};
        int m_height{};
        IWindowEventSink* m_sink{};
        bool m_quit{};
        bool m_pointerLockDesired{};   // what the app asked for via SetPointerLocked
        bool m_pointerLockActive{};    // currently hiding + clipping the cursor
    };
}
