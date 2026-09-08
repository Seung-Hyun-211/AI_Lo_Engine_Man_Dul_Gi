#include "render/Dx11Renderer.h"
#include "core/JobSystem.h"
#include "ui/UI.h"

#include <Windows.h>
#include <windowsx.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <stdexcept>
#include <thread>
#include <vector>

namespace
{
    constexpr wchar_t kWindowClass[] = L"CppWindowGameClass";
    constexpr int kInitialWidth = 1280;
    constexpr int kInitialHeight = 720;
    constexpr float kPlayerSize = 64.0f;
    constexpr std::size_t kSimulationObjectCount = 20'000;

    std::size_t DefaultWorkerCount()
    {
        const unsigned int logicalProcessors = std::thread::hardware_concurrency();
        // Keep room for Main, Render, OS, and the GPU driver. This is an
        // adjustable initial policy, not an attempt to consume every SMT thread.
        return std::clamp<std::size_t>(logicalProcessors > 2 ? logicalProcessors - 2 : 1, 1, 14);
    }

    class Game final
    {
    public:
        Game() : m_jobs(DefaultWorkerCount()) {}

        int Run(HINSTANCE instance)
        {
            CreateGameWindow(instance);
            InitializeSimulationObjects();
            ShowWindow(m_window, SW_SHOW);
            UpdateWindow(m_window);
            m_renderer.Start(m_window, m_clientWidth, m_clientHeight);
            m_renderer.SetFrameSettings({ .targetFramesPerSecond = 60, .verticalSync = true });

            auto previous = std::chrono::steady_clock::now();
            std::uint64_t frameNumber{};
            MSG message{};
            bool running = true;
            while (running)
            {
                while (PeekMessage(&message, nullptr, 0, 0, PM_REMOVE))
                {
                    if (message.message == WM_QUIT)
                    {
                        running = false;
                        break;
                    }
                    TranslateMessage(&message);
                    DispatchMessage(&message);
                }
                if (!running) break;

                const auto now = std::chrono::steady_clock::now();
                const float deltaTime = std::min(
                    std::chrono::duration<float>(now - previous).count(), 0.1f);
                previous = now;
                Update(deltaTime);
                m_renderer.Submit(BuildRenderSnapshot(frameNumber++));
            }

            m_renderer.Stop();
            return static_cast<int>(message.wParam);
        }

    private:
        void CreateGameWindow(HINSTANCE instance)
        {
            WNDCLASSEXW windowClass{ sizeof(windowClass) };
            windowClass.lpfnWndProc = WindowProc;
            windowClass.hInstance = instance;
            windowClass.lpszClassName = kWindowClass;
            windowClass.hCursor = LoadCursor(nullptr, IDC_ARROW);
            RegisterClassExW(&windowClass);

            RECT rectangle{ 0, 0, kInitialWidth, kInitialHeight };
            AdjustWindowRect(&rectangle, WS_OVERLAPPEDWINDOW, FALSE);
            m_window = CreateWindowExW(0, kWindowClass, L"Cpp Window Game - DX11 2D",
                WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
                rectangle.right - rectangle.left, rectangle.bottom - rectangle.top,
                nullptr, nullptr, instance, this);
            if (m_window == nullptr) throw std::runtime_error("Could not create game window.");
        }

        // Main-thread-only simulation. No Direct3D calls are allowed here.
        void Update(float deltaTime)
        {
            float x = (m_right ? 1.0f : 0.0f) - (m_left ? 1.0f : 0.0f);
            float y = (m_down ? 1.0f : 0.0f) - (m_up ? 1.0f : 0.0f);
            const float length = std::sqrt(x * x + y * y);
            if (length > 0.0f) { x /= length; y /= length; }
            constexpr float speed = 300.0f;
            m_playerX += x * speed * deltaTime;
            m_playerY += y * speed * deltaTime;
            m_playerX = std::clamp(m_playerX, 0.0f, std::max(0.0f, static_cast<float>(m_clientWidth) - kPlayerSize));
            m_playerY = std::clamp(m_playerY, 0.0f, std::max(0.0f, static_cast<float>(m_clientHeight) - kPlayerSize));

            // Each Job owns a distinct contiguous range. No shared counters or
            // vector resize happen in workers; the Fence is the snapshot boundary.
            m_jobs.ParallelFor(0, m_particles.size(), 2'048,
                [this, deltaTime](std::size_t begin, std::size_t end)
                {
                    for (std::size_t index = begin; index < end; ++index)
                    {
                        MovingParticle& particle = m_particles[index];
                        particle.x += particle.vx * deltaTime;
                        particle.y += particle.vy * deltaTime;
                        if (particle.x > static_cast<float>(m_clientWidth)) particle.x = 0.0f;
                        if (particle.y > static_cast<float>(m_clientHeight)) particle.y = 0.0f;
                    }
                }).Wait();
        }

        engine::render::RenderSnapshot BuildRenderSnapshot(std::uint64_t frameNumber) const
        {
            engine::render::RenderSnapshot snapshot{};
            snapshot.frameNumber = frameNumber;
            snapshot.playerX = m_playerX;
            snapshot.playerY = m_playerY;
            snapshot.simulatedSpriteCount = static_cast<std::uint32_t>(m_particles.size());
            m_ui.Build(snapshot.uiQuads);
            return snapshot;
        }

        void InitializeSimulationObjects()
        {
            m_particles.resize(kSimulationObjectCount);
            for (std::size_t index = 0; index < m_particles.size(); ++index)
            {
                const float seed = static_cast<float>(index);
                m_particles[index] = {
                    std::fmod(seed * 37.0f, static_cast<float>(kInitialWidth)),
                    std::fmod(seed * 19.0f, static_cast<float>(kInitialHeight)),
                    20.0f + std::fmod(seed, 90.0f),
                    10.0f + std::fmod(seed * 0.5f, 70.0f),
                };
            }
        }

        void OnResize(int width, int height)
        {
            if (width <= 0 || height <= 0) return;
            m_clientWidth = width;
            m_clientHeight = height;
            m_renderer.Resize(static_cast<std::uint32_t>(width), static_cast<std::uint32_t>(height));
        }

        static LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
        {
            auto* game = reinterpret_cast<Game*>(GetWindowLongPtrW(window, GWLP_USERDATA));
            if (message == WM_NCCREATE)
            {
                game = static_cast<Game*>(reinterpret_cast<CREATESTRUCTW*>(lParam)->lpCreateParams);
                SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(game));
            }
            if (game == nullptr) return DefWindowProcW(window, message, wParam, lParam);

            switch (message)
            {
            case WM_KEYDOWN:
            case WM_KEYUP:
            {
                const bool pressed = message == WM_KEYDOWN;
                if (wParam == VK_LEFT) game->m_left = pressed;
                if (wParam == VK_RIGHT) game->m_right = pressed;
                if (wParam == VK_UP) game->m_up = pressed;
                if (wParam == VK_DOWN) game->m_down = pressed;
                return 0;
            }
            case WM_KILLFOCUS:
                game->m_left = game->m_right = game->m_up = game->m_down = false;
                return 0;
            case WM_MOUSEMOVE:
                game->m_ui.PointerMove({ static_cast<float>(GET_X_LPARAM(lParam)), static_cast<float>(GET_Y_LPARAM(lParam)) });
                return 0;
            case WM_LBUTTONDOWN:
                SetCapture(window);
                game->m_ui.PointerDown({ static_cast<float>(GET_X_LPARAM(lParam)), static_cast<float>(GET_Y_LPARAM(lParam)) });
                return 0;
            case WM_LBUTTONUP:
                ReleaseCapture();
                game->m_ui.PointerUp({ static_cast<float>(GET_X_LPARAM(lParam)), static_cast<float>(GET_Y_LPARAM(lParam)) });
                return 0;
            case WM_SIZE:
                if (wParam != SIZE_MINIMIZED) game->OnResize(LOWORD(lParam), HIWORD(lParam));
                return 0;
            case WM_DESTROY:
                PostQuitMessage(0);
                return 0;
            default:
                return DefWindowProcW(window, message, wParam, lParam);
            }
        }

        struct MovingParticle { float x, y, vx, vy; };

        HWND m_window{};
        int m_clientWidth = kInitialWidth;
        int m_clientHeight = kInitialHeight;
        bool m_left{}, m_right{}, m_up{}, m_down{};
        float m_playerX = 608.0f;
        float m_playerY = 328.0f;
        std::vector<MovingParticle> m_particles;
        engine::core::JobSystem m_jobs;
        engine::render::Dx11Renderer m_renderer;
        engine::ui::UIContext m_ui;
    };
}

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int)
{
    try { return Game{}.Run(instance); }
    catch (const std::exception&)
    {
        MessageBoxW(nullptr, L"The game could not start. Check that your graphics driver supports DirectX 11.",
            L"Startup error", MB_ICONERROR);
        return 1;
    }
}
