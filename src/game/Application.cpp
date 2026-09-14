#include "game/Application.h"

#include "game/InGameHud.h"
#include "game/InventoryScreen.h"
#include "game/SettingsScreen.h"
#include "game/TitleScreen.h"

#include <thread>
#include <timeapi.h>

namespace engine::game
{
    namespace
    {
        constexpr wchar_t kWindowTitle[] = L"AI Lo Engine - DX11 2D skeleton";

#if defined(ENGINE_WITH_3D)
        // Demo crowd blast: ground zero at the middle of the lower field
        // (crowd z range ~8..40), and how long after entering InGame it auto-fires.
        constexpr math::Vec3 kBlastCenter{ 0.0f, 0.0f, 24.0f };
        constexpr float kBlastRadius = 14.0f;
        constexpr float kBlastPower = 16.0f;
        constexpr float kAutoBlastDelay = 5.0f;
#endif

        // Windows' default scheduler tick (~15.6ms) is coarser than the frame
        // periods this app paces to (16.67ms @60fps, 8.33ms @120, 6.94ms @144)
        // - without raising the OS timer resolution, `sleep_until` overshoots
        // to the next ~15.6ms tick unpredictably, then the next frame's
        // catch-up (Application::Run's nextFrameDeadline resync) fires with
        // near-zero delay to compensate, producing a visibly noisy FPS
        // counter (measured: bounced 60-117 with a 60fps cap). RAII'd around
        // the whole run so it's released on every exit path, including an
        // exception unwinding out of the frame loop.
        struct HighResTimerScope
        {
            HighResTimerScope() { timeBeginPeriod(1); }
            ~HighResTimerScope() { timeEndPeriod(1); }
        };
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
        ApplyVolumes();
        EnterTitle();
    }

    int Application::Run()
    {
        const HighResTimerScope highResTimer;   // see its declaration above
        m_window.SetEventSink(*this);
        m_renderer.Start(m_window.Handle(),
                         static_cast<std::uint32_t>(m_window.Width()),
                         static_cast<std::uint32_t>(m_window.Height()));
        ApplyFrameSettings();
        m_window.Show();

        // The render thread borrows the window's HWND, so it must be stopped
        // before this function returns and the window is destroyed - including
        // when a job exception propagates out of the frame loop.
        //
        // Frame-rate cap, main-thread side: IRenderer::Submit is a non-
        // blocking mailbox (rule 4 - the renderer never makes the caller
        // wait), so without this the main loop spins as fast as the OS lets
        // it regardless of the render thread's own cap (Dx11Renderer::
        // RenderLoop's nextFrameDeadline, same pattern below) - the top-right
        // FPS counter reads m_fpsSmoothed from THIS loop's delta, so it was
        // reporting the uncapped sim/input rate, not the actually-presented
        // one (game-settings.md - user report: counter exceeded the 60/120/
        // 144 cap). Paces unconditionally, even with vsync on - vsync paces
        // the render thread's Present to whatever the display's actual
        // refresh rate is (unknown here), which does not by itself keep this
        // loop, and therefore the counter, under the user's chosen cap.
        auto nextFrameDeadline = std::chrono::steady_clock::now();
        try
        {
            while (true)
            {
                m_input.BeginFrame();
                if (!m_window.PumpMessages()) break;
                m_audio.Update();   // reap finished one-shot voices

                if (m_pendingResize)
                {
                    const auto width = static_cast<std::uint32_t>(m_pendingResize->cx);
                    const auto height = static_cast<std::uint32_t>(m_pendingResize->cy);
                    m_renderer.Resize(width, height);
                    m_simulation.SetWorldSize(m_pendingResize->cx, m_pendingResize->cy);
                    m_pendingResize.reset();
                }

                const float delta = m_clock.Tick();
                if (delta > 1e-6f)
                {
                    const float instant = 1.0f / delta;
                    m_fpsSmoothed = m_fpsSmoothed <= 0.0f ? instant
                                                          : m_fpsSmoothed * 0.9f + instant * 0.1f;
                }
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
                    if (m_input.KeyPressed(VK_SPACE))
                    {
                        m_simulation.QueueJump();
                        m_audio.PlaySfx("assets/audio/blip.wav");   // demo hook
                    }
                    // Auto-fire one blast a few seconds into the session, then
                    // let 'F' re-trigger it by hand. A real game drives this from
                    // gameplay (weapon impact, tower AoE), not a timer/keyboard.
                    m_inGameElapsed += delta;
                    const bool autoBlast =
                        !m_autoExplodeFired && m_inGameElapsed >= kAutoBlastDelay;
                    if (autoBlast) m_autoExplodeFired = true;
                    if (autoBlast || m_input.KeyPressed('F'))
                    {
                        m_simulation.TriggerExplosion(kBlastCenter, kBlastRadius, kBlastPower);
                        m_audio.PlaySfx("assets/audio/blip.wav");
                    }
                    // Rifle (docs/defense-combat-design.md §5): full-auto, held
                    // down like the flamethrower - Simulation's own fire-rate
                    // cooldown paces the actual shots, so this just forwards
                    // "trigger held" every frame and plays the SFX only on the
                    // frames that really fired (FireWeapon's return value).
                    if (m_input.MouseDown(0))
                    {
                        if (m_simulation.FireWeapon(WeaponKind::Rifle))
                            m_audio.PlaySfx("assets/audio/blip.wav");   // demo hook - no muzzle SFX yet
                    }
                    // Mortar/mine placement (docs/defense-combat-design.md §4):
                    // '1'/'2' are a demo stand-in for the real prep-phase UI
                    // (§0.4, not built yet) - both place at the current
                    // look-ray ground hit, same aim source as the rifle.
                    // Same physical keys double as scene EffectsTest's VFX
                    // preview triggers - both calls self-guard on the active
                    // scene (no-op outside their own scene), so no ActiveScene()
                    // check is needed here.
                    if (m_input.KeyPressed('1'))
                    {
                        m_simulation.PlaceOrdnance(OrdnanceKind::Mortar);
                        m_simulation.PreviewVfxEffect(EffectPreview::MuzzleFlash);
                        m_audio.PlaySfx("assets/audio/blip.wav");
                    }
                    if (m_input.KeyPressed('2'))
                    {
                        m_simulation.PlaceOrdnance(OrdnanceKind::Mine);
                        m_simulation.PreviewVfxEffect(EffectPreview::Explosion);
                        m_audio.PlaySfx("assets/audio/blip.wav");
                    }
                    // Barbed wire (docs/defense-combat-design.md §6): '3', same
                    // demo-key convention as '1'/'2' above.
                    if (m_input.KeyPressed('3'))
                    {
                        m_simulation.PlaceSlowZone();
                        m_simulation.PreviewVfxEffect(EffectPreview::GibBurst);
                        m_audio.PlaySfx("assets/audio/blip.wav");
                    }
                    // Flamethrower (docs/defense-combat-design.md §7): right
                    // mouse HELD, not a press edge - the cone re-applies every
                    // frame it's down. No continuous-loop SFX yet
                    // (audio-design.md is one-shot/looping-clip only, no
                    // per-frame-safe loop start/stop API), so this stays
                    // silent for now rather than spamming PlaySfx per frame.
                    if (m_input.MouseDown(1))
                    {
                        m_simulation.FireWeapon(WeaponKind::Flamethrower);
                    }
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

#if defined(ENGINE_WITH_3D)
                    // Loss condition (docs/defense-combat-design.md §0): no
                    // results screen yet (§0.3 is still design-only), so this
                    // is the bare-minimum connection - drop straight back to
                    // the title screen the instant the objective dies.
                    if (m_simulation.IsMatchLost())
                    {
                        EnterTitle();
                    }
                    else
                    {
                        // Crowd colliders + look-ray + overlap tint: once per
                        // frame, after the step loop, on the final positions.
                        // O(crowd), so running it per sub-step spiralled a
                        // slow frame.
                        m_simulation.UpdateCrowdQueries();
                    }
#endif
                }

                // Submit every frame even with zero sim steps: the UI overlay may
                // have changed and still needs to be redrawn.
                m_renderer.Submit(m_snapshotBuilder.Build(m_frameNumber++, m_simulation, m_ui,
                                                          m_window.Width(), m_window.Height(),
                                                          &m_uiAtlas, m_fpsSmoothed));

                // Paced regardless of vsync: vsync only paces the render
                // thread's Present to the display's actual refresh rate (which
                // this loop has no way to know), it does not stop the main
                // loop from spinning past the user's chosen 60/120/144 cap
                // (user report - the top-right counter still exceeded the cap
                // with vsync on). Capping here directly bounds what the
                // counter can show, independent of vsync/display refresh.
                //
                // Advance from the PREVIOUS scheduled deadline, not from
                // now() - `nextFrameDeadline = max(nextFrameDeadline, now())
                // + frameDuration` looks equivalent but isn't: once real
                // per-frame work (sim step + snapshot build) takes close to
                // or longer than frameDuration, "now()" is already past the
                // old deadline every iteration, so that formula stacks a
                // *full extra* frameDuration on top of the overrun each time
                // - period becomes (work time + frameDuration) instead of
                // just frameDuration, which is roughly 2x too slow right at
                // the point work time ~= frameDuration (user report: counter
                // reads ~half the selected cap). Resyncing to now() only when
                // behind (and not re-adding frameDuration in that case) makes
                // this a true upper bound: never slower than the uncapped
                // rate, never faster than the cap.
                const std::uint32_t targetFps =
                    core::kFrameRatePresets[static_cast<std::size_t>(m_settings.frameRateIndex)];
                nextFrameDeadline += std::chrono::duration_cast<std::chrono::steady_clock::duration>(
                    std::chrono::duration<double>(1.0 / static_cast<double>(targetFps)));
                const auto now = std::chrono::steady_clock::now();
                if (nextFrameDeadline < now) nextFrameDeadline = now;
                std::this_thread::sleep_until(nextFrameDeadline);
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
        m_audio.StopMusic();
        m_ui.ClearOverlay();
        m_ui.SetScreen(BuildTitleScreen(
            [this] { EnterSceneSelect(); },
            [this] { EnterItems(); },
            [this] { OpenSettings(); },
            [this] { m_window.RequestClose(); }));
    }

    void Application::EnterSceneSelect()
    {
        m_state = GameState::Title;   // still a menu context: no simulation step
        m_window.SetPointerLocked(false);
        m_ui.ClearOverlay();
#if defined(ENGINE_WITH_3D)
        m_ui.SetScreen(BuildSceneSelectScreen(
            [this] { EnterInGame(DemoScene::DefenseCombat); },
            [this] { EnterInGame(DemoScene::CharacterDemo); },
            [this] { EnterInGame(DemoScene::ShadowShowcase); },
            [this] { EnterInGame(DemoScene::EffectsTest); },
            [this] { EnterTitle(); }));
#endif
    }

    void Application::EnterItems()
    {
        m_state = GameState::Title;   // still a menu context: no simulation step
        m_window.SetPointerLocked(false);
        m_ui.ClearOverlay();
        m_ui.SetScreen(BuildInventoryScreen([this] { EnterTitle(); }));
    }

    void Application::EnterInGame(DemoScene scene)
    {
        m_state = GameState::InGame;
        m_inGameElapsed = 0.0f;
        m_autoExplodeFired = false;
#if defined(ENGINE_WITH_3D)
        // The only entry point into InGame - whether it's a fresh pick from
        // the scene-select menu or (DefenseCombat only) a restart after a
        // loss. Simulation::EnterScene rebuilds the actor list for `scene`
        // and, for DefenseCombat, runs ResetMatch() so it always starts at
        // wave 1 (docs/defense-combat-design.md §0).
        m_simulation.EnterScene(scene);
#else
        (void)scene;
#endif
        m_ui.ClearOverlay();
        m_ui.SetScreen(BuildInGameHud([this] { OpenSettings(); }));
        m_window.SetPointerLocked(true);   // mouse-look / centre-locked cursor
        // Demo hook (docs/audio-design.md §4/§5): exercises the streaming music
        // path end-to-end. blip.wav is a placeholder loop - swap for a real
        // track when one exists, the call site does not change.
        m_audio.PlayMusic("assets/audio/blip.wav");
    }

    void Application::OpenSettings()
    {
        m_window.SetPointerLocked(false);   // give the cursor back for the menu
        m_ui.SetOverlay(BuildSettingsScreen(m_settings, SettingsScreenActions{
            .onVolumeChanged = [this] { ApplyVolumes(); },
            .onVsyncToggled = [this] { ApplyFrameSettings(); },
            .onResolutionChanged = [this] { ApplyResolution(); },
            .onFrameRateChanged = [this] { ApplyFrameSettings(); },
            .onClose = [this] { CloseSettings(); },
            .onExitToTitle = [this] { m_settings.Save(core::kSettingsFilePath); EnterTitle(); },
        }));
    }

    void Application::CloseSettings()
    {
        m_ui.ClearOverlay();
        m_settings.Save(core::kSettingsFilePath);
        if (m_state == GameState::InGame) m_window.SetPointerLocked(true);
    }

    void Application::ApplyVolumes()
    {
        m_audio.SetMasterVolume(m_settings.masterVolume);
        m_audio.SetMusicVolume(m_settings.musicVolume);
        m_audio.SetSfxVolume(m_settings.sfxVolume);
    }

    void Application::ApplyFrameSettings()
    {
        const std::uint32_t targetFps =
            core::kFrameRatePresets[static_cast<std::size_t>(m_settings.frameRateIndex)];
        m_renderer.SetFrameSettings({ .targetFramesPerSecond = targetFps, .verticalSync = m_settings.vsync });
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

    void Application::OnMouseWheel(float notches)
    {
        // UI first (a ScrollList under the cursor consumes it); otherwise it is
        // gameplay input for the frame.
        if (m_ui.PointerWheel(m_pointerPosition, notches)) return;
        m_input.OnMouseWheel(notches);
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
