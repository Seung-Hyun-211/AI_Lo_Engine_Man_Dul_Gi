#include "game/Application.h"

#include "game/InGameHud.h"
#include "game/SettingsScreen.h"
#include "game/TitleScreen.h"

namespace engine::game
{
    namespace
    {
        constexpr wchar_t kWindowTitle[] = L"AI Lo Engine - DX11 2D skeleton";
    }

    Application::Application(HINSTANCE instance, render::IRenderer& renderer)
        : m_renderer(renderer)
        , m_jobs(core::RecommendedWorkerCount())
        , m_settings(core::Settings::LoadOrDefault(core::kSettingsFilePath))
        , m_window(instance, { kWindowTitle,
                                core::kResolutionPresets[static_cast<std::size_t>(m_settings.resolutionIndex)].width,
                                core::kResolutionPresets[static_cast<std::size_t>(m_settings.resolutionIndex)].height })
        , m_simulation(m_jobs, m_window.Width(), m_window.Height())
    {
        EnterTitle();
    }

    int Application::Run()
    {
        m_window.SetEventSink(*this);
        m_renderer.Start(m_window.Handle(),
                         static_cast<std::uint32_t>(m_window.Width()),
                         static_cast<std::uint32_t>(m_window.Height()));
        m_renderer.SetFrameSettings({ .targetFramesPerSecond = 60, .verticalSync = m_settings.vsync });
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
                // The world only advances in-game, and not while Settings (or
                // any future modal) sits on top of it - both read as "paused".
                if (m_state == GameState::InGame && !m_ui.HasOverlay())
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

    void Application::EnterTitle()
    {
        m_state = GameState::Title;
        m_ui.ClearOverlay();
        m_ui.SetScreen(BuildTitleScreen(
            [this] { EnterInGame(); },
            [this] { OpenSettings(); },
            [this] { m_window.RequestClose(); }));
    }

    void Application::EnterInGame()
    {
        m_state = GameState::InGame;
        m_ui.ClearOverlay();
        m_ui.SetScreen(BuildInGameHud([this] { OpenSettings(); }));
    }

    void Application::OpenSettings()
    {
        m_ui.SetOverlay(BuildSettingsScreen(m_settings, SettingsScreenActions{
            .onVsyncToggled = [this] { ApplyVsync(); },
            .onResolutionChanged = [this] { ApplyResolution(); },
            .onClose = [this] { CloseSettings(); },
        }));
    }

    void Application::CloseSettings()
    {
        m_ui.ClearOverlay();
        m_settings.Save(core::kSettingsFilePath);
    }

    void Application::ApplyVsync()
    {
        m_renderer.SetFrameSettings({ .targetFramesPerSecond = 60, .verticalSync = m_settings.vsync });
    }

    void Application::ApplyResolution()
    {
        const core::Resolution resolution =
            core::kResolutionPresets[static_cast<std::size_t>(m_settings.resolutionIndex)];
        m_window.RequestResize(resolution.width, resolution.height);
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
        if (down && virtualKey == VK_ESCAPE)
        {
            if (m_ui.HasOverlay()) CloseSettings();
            else if (m_state == GameState::InGame) OpenSettings();
            return;   // consumed by the menu, not gameplay
        }
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
        // Sliders/checkboxes already mutate m_settings live; only the disk
        // write was deferred to CloseSettings(). Flush it here too so closing
        // the window (X button / Alt+F4) while Settings is still open doesn't
        // silently drop the change - this is the app's exit pipeline (see
        // docs/scene-flow-design.md "종료 파이프라인"; JobSystem/Dx11Renderer/
        // Win32Window each already tear themselves down via their own
        // destructor, called after Run() returns).
        m_settings.Save(core::kSettingsFilePath);
        // DefWindowProc destroys the window, WM_DESTROY posts WM_QUIT, and
        // PumpMessages ends the loop - no other shutdown step needed here.
    }
}
