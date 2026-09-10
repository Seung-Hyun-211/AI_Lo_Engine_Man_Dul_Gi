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
        m_uiAtlas.Load("assets/atlas/ui.atlas", render::kUiAtlasId);
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
                // Global time scale: 0 = pause, 0.5 = slow-mo, 2 = fast-forward.
                // Scaling the delta (not the fixed step size) keeps the sim
                // deterministic - it just runs more/fewer fixed steps this frame
                // (docs/time-design.md). Per-actor local scale is separate.
                const int steps = m_timestep.Advance(delta * m_globalTimeScale);
                // The world only advances in-game, and not while Settings (or
                // any future modal) sits on top of it - both read as "paused".
                if (m_state == GameState::InGame && !m_ui.HasOverlay())
                {
#if defined(ENGINE_WITH_3D)
                    // Mouse-look once per frame (independent of the fixed-step
                    // count) so it never double-applies or drops a delta. Jump
                    // is an edge, so latch it the same way - a press on a
                    // zero-step frame must survive to the next step.
                    m_simulation.UpdateCameraLook(intent.look);
                    if (m_input.KeyPressed(VK_SPACE)) m_simulation.QueueJump();
#endif
                    if (steps > 0)
                    {
                        for (int step = 0; step < steps; ++step)
                            m_simulation.Step(m_timestep.Step(), intent);
                    }
                    else if (m_globalTimeScale <= 0.0f)
                    {
                        // Global pause: the world is frozen, but actors flagged
                        // ignoreGlobalPause still take one fixed step per frame.
                        m_simulation.Step(m_timestep.Step(), intent, /*globalPaused=*/true);
                    }
                }

                // Submit every frame even with zero sim steps: the UI overlay may
                // have changed and still needs to be redrawn.
                m_renderer.Submit(m_snapshotBuilder.Build(m_frameNumber++, m_simulation, m_ui,
                                                          m_window.Width(), m_window.Height(), &m_uiAtlas));
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
        m_window.SetPointerLocked(false);
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
        m_window.SetPointerLocked(true);   // mouse-look / centre-locked cursor
    }

    void Application::OpenSettings()
    {
        m_window.SetPointerLocked(false);   // give the cursor back for the menu
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
        if (m_state == GameState::InGame) m_window.SetPointerLocked(true);
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
        // WASD (arrow keys aliased): x = strafe right, y = forward. The
        // simulation rotates this by the camera yaw before moving the character.
        const bool right = m_input.KeyDown('D') || m_input.KeyDown(VK_RIGHT);
        const bool left = m_input.KeyDown('A') || m_input.KeyDown(VK_LEFT);
        const bool forward = m_input.KeyDown('W') || m_input.KeyDown(VK_UP);
        const bool back = m_input.KeyDown('S') || m_input.KeyDown(VK_DOWN);

        PlayerIntent intent{};
        intent.move.x = (right ? 1.0f : 0.0f) - (left ? 1.0f : 0.0f);
        intent.move.y = (forward ? 1.0f : 0.0f) - (back ? 1.0f : 0.0f);
        intent.look = m_input.MouseDelta();
        intent.run = m_input.KeyDown(VK_SHIFT);
        return intent;
    }

    void Application::OnMouseDelta(math::Vec2 delta)
    {
        m_input.OnMouseDelta(delta);
    }

    void Application::OnKey(int virtualKey, bool down)
    {
        if (down && virtualKey == VK_ESCAPE)
        {
            if (m_ui.HasOverlay()) CloseSettings();
            else if (m_state == GameState::InGame) OpenSettings();
            return;   // consumed by the menu, not gameplay
        }

        // Demo wiring for the global time scale: PageUp / PageDown cycle
        // {0, 0.25, 0.5, 1, 2}. A real game would call SetGlobalTimeScale from
        // gameplay (a bullet-time ability, a pause menu, ...), not the keyboard.
        if (down && (virtualKey == VK_PRIOR || virtualKey == VK_NEXT))
        {
            constexpr float kScales[] = { 0.0f, 0.25f, 0.5f, 1.0f, 2.0f };
            int index = 3;
            for (int i = 0; i < 5; ++i)
                if (kScales[i] == m_globalTimeScale) { index = i; break; }
            index += (virtualKey == VK_PRIOR) ? 1 : -1;
            index = index < 0 ? 0 : (index > 4 ? 4 : index);
            SetGlobalTimeScale(kScales[index]);
            return;
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
