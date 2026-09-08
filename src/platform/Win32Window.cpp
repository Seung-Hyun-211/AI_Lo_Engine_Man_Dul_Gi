#include "platform/Win32Window.h"

#include <windowsx.h>

#include <stdexcept>

namespace engine::platform
{
    namespace
    {
        constexpr wchar_t kWindowClass[] = L"EngineWin32WindowClass";
    }

    Win32Window::Win32Window(HINSTANCE instance, const Config& config)
        : m_width(config.width), m_height(config.height)
    {
        WNDCLASSEXW windowClass{ sizeof(windowClass) };
        windowClass.lpfnWndProc = &Win32Window::WindowProc;
        windowClass.hInstance = instance;
        windowClass.lpszClassName = kWindowClass;
        windowClass.hCursor = LoadCursor(nullptr, IDC_ARROW);
        if (RegisterClassExW(&windowClass) == 0 && GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
            throw std::runtime_error("RegisterClassExW failed");

        RECT rectangle{ 0, 0, config.width, config.height };
        AdjustWindowRect(&rectangle, WS_OVERLAPPEDWINDOW, FALSE);
        m_window = CreateWindowExW(0, kWindowClass, config.title.c_str(),
            WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
            rectangle.right - rectangle.left, rectangle.bottom - rectangle.top,
            nullptr, nullptr, instance, this);
        if (m_window == nullptr) throw std::runtime_error("CreateWindowExW failed");
    }

    Win32Window::~Win32Window()
    {
        // The window is usually already gone (user closed it -> WM_DESTROY).
        // Detach the back-pointer first so any message dispatched during
        // DestroyWindow falls through to DefWindowProc instead of a half-torn
        // application.
        if (m_window != nullptr && IsWindow(m_window))
        {
            SetWindowLongPtrW(m_window, GWLP_USERDATA, 0);
            DestroyWindow(m_window);
        }
        m_window = nullptr;
    }

    void Win32Window::Show()
    {
        ShowWindow(m_window, SW_SHOW);
        UpdateWindow(m_window);
    }

    bool Win32Window::PumpMessages()
    {
        MSG message{};
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE))
        {
            if (message.message == WM_QUIT)
            {
                m_quit = true;
                return false;
            }
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
        return !m_quit;
    }

    LRESULT CALLBACK Win32Window::WindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
    {
        if (message == WM_NCCREATE)
        {
            auto* self = static_cast<Win32Window*>(reinterpret_cast<CREATESTRUCTW*>(lParam)->lpCreateParams);
            SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        }
        auto* self = reinterpret_cast<Win32Window*>(GetWindowLongPtrW(window, GWLP_USERDATA));
        if (self == nullptr) return DefWindowProcW(window, message, wParam, lParam);
        return self->HandleMessage(window, message, wParam, lParam);
    }

    LRESULT Win32Window::HandleMessage(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
    {
        // `window` is always valid here, including for creation-time messages
        // (WM_NCCREATE, WM_CREATE) that arrive before CreateWindowExW returns and
        // sets m_window. Falling through to DefWindowProcW with a null m_window
        // would make WM_NCCREATE return FALSE and abort window creation.
        switch (message)
        {
        case WM_KEYDOWN:
        case WM_KEYUP:
            if (m_sink != nullptr) m_sink->OnKey(static_cast<int>(wParam), message == WM_KEYDOWN);
            return 0;

        case WM_KILLFOCUS:
            if (m_sink != nullptr) m_sink->OnFocusLost();
            return 0;

        case WM_MOUSEMOVE:
            if (m_sink != nullptr)
                m_sink->OnMouseMove({ static_cast<float>(GET_X_LPARAM(lParam)), static_cast<float>(GET_Y_LPARAM(lParam)) });
            return 0;

        case WM_LBUTTONDOWN:
        case WM_RBUTTONDOWN:
        case WM_MBUTTONDOWN:
        {
            SetCapture(window);
            const int button = message == WM_LBUTTONDOWN ? 0 : message == WM_RBUTTONDOWN ? 1 : 2;
            if (m_sink != nullptr) m_sink->OnMouseButton(button, true);
            return 0;
        }
        case WM_LBUTTONUP:
        case WM_RBUTTONUP:
        case WM_MBUTTONUP:
        {
            ReleaseCapture();
            const int button = message == WM_LBUTTONUP ? 0 : message == WM_RBUTTONUP ? 1 : 2;
            if (m_sink != nullptr) m_sink->OnMouseButton(button, false);
            return 0;
        }

        case WM_SIZE:
            if (wParam != SIZE_MINIMIZED)
            {
                m_width = LOWORD(lParam);
                m_height = HIWORD(lParam);
                if (m_sink != nullptr) m_sink->OnResize(m_width, m_height);
            }
            return 0;

        case WM_CLOSE:
            if (m_sink != nullptr) m_sink->OnClose();
            return DefWindowProcW(window, message, wParam, lParam);

        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;

        default:
            return DefWindowProcW(window, message, wParam, lParam);
        }
    }
}
