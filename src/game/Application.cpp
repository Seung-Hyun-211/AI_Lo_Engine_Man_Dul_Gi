#include "game/Application.h"

namespace engine::game
{
    namespace
    {
        constexpr wchar_t kWindowTitle[] = L"AI Lo Engine - DX11 2D skeleton";
        constexpr int kInitialWidth = 1280;
        constexpr int kInitialHeight = 720;
    }

    Application::Application(HINSTANCE instance, render::IRenderer& renderer)
        : m_renderer(renderer)
        , m_jobs(core::RecommendedWorkerCount())
        , m_window(instance, { kWindowTitle, kInitialWidth, kInitialHeight })
        , m_simulation(m_jobs, m_window.Width(), m_window.Height())
    {
    }

    int Application::Run()
    {
        m_window.SetEventSink(*this);
        m_renderer.Start(m_window.Handle(),
                         static_cast<std::uint32_t>(m_window.Width()),
                         static_cast<std::uint32_t>(m_window.Height()));
        m_renderer.SetFrameSettings({ .targetFramesPerSecond = 60, .verticalSync = true });
        m_window.Show();

        // The render thread borrows the window's HWND, so it must be stopped
        // before this function returns and the window is destroyed - including
        // when a job exception propagates out of the frame loop.
        try
        {
            while (true)
            {
                m_input.BeginFrame();
                if (!m_window.PumpMessages()) break;

                if (m_pendingResize)
                {
                    const auto width = static_cast<std::uint32_t>(m_pendingResize->cx);
                    const auto height = static_cast<std::uint32_t>(m_pendingResize->cy);
                    m_renderer.Resize(width, height);
                    m_simulation.SetWorldSize(m_pendingResize->cx, m_pendingResize->cy);
                    m_pendingResize.reset();
                }

                const float delta = m_clock.Tick();
                const PlayerIntent intent = BuildPlayerIntent();
                const int steps = m_timestep.Advance(delta);
                for (int step = 0; step < steps; ++step)
                    m_simulation.Step(m_timestep.Step(), intent);

                // Submit every frame even with zero sim steps: the UI overlay may
                // have changed and still needs to be redrawn.
                m_renderer.Submit(m_snapshotBuilder.Build(m_frameNumber++, m_simulation, m_ui,
                                                          m_window.Width(), m_window.Height()));
            }
        }
        catch (...)
        {
            m_renderer.Stop();
            throw;
        }

        m_renderer.Stop();
        return 0;
    }

    PlayerIntent Application::BuildPlayerIntent() const
    {
        PlayerIntent intent{};
        intent.move.x = (m_input.KeyDown(VK_RIGHT) ? 1.0f : 0.0f) - (m_input.KeyDown(VK_LEFT) ? 1.0f : 0.0f);
        intent.move.y = (m_input.KeyDown(VK_DOWN) ? 1.0f : 0.0f) - (m_input.KeyDown(VK_UP) ? 1.0f : 0.0f);
        return intent;
    }

    void Application::OnKey(int virtualKey, bool down)
    {
        m_input.OnKey(virtualKey, down);
    }

    void Application::OnMouseMove(math::Vec2 position)
    {
        m_pointerPosition = position;
        // Hover is not exclusive: the UI updates its hover state and gameplay
        // still learns the cursor position.
        m_ui.PointerMove(position);
        m_input.OnMouseMove(position);
    }

    void Application::OnMouseButton(int button, bool down)
    {
        if (button < 0 || button >= static_cast<int>(m_pointerConsumedByUI.size())) return;

        bool consumed = false;
        if (down)
        {
            if (button == 0) consumed = m_ui.PointerDown(m_pointerPosition);
            m_pointerConsumedByUI[button] = consumed;
        }
        else
        {
            // Always deliver the release to the UI so a Button can clear its
            // pressed state, but decide routing by how the press was handled.
            if (button == 0) m_ui.PointerUp(m_pointerPosition);
            consumed = m_pointerConsumedByUI[button];
            m_pointerConsumedByUI[button] = false;
        }

        if (!consumed) m_input.OnMouseButton(button, down);
    }

    void Application::OnFocusLost()
    {
        m_input.OnFocusLost();
    }

    void Application::OnResize(int width, int height)
    {
        if (width <= 0 || height <= 0) return;
        m_pendingResize = SIZE{ width, height };
    }

    void Application::OnClose()
    {
        // Nothing extra to do: DefWindowProc destroys the window, WM_DESTROY
        // posts WM_QUIT, and PumpMessages ends the loop. Hook kept for a future
        // "unsaved changes?" prompt.
    }
}
