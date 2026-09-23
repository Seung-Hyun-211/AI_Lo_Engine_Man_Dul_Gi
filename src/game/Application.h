#pragma once

#include "audio/AudioEngine.h"
#include "core/JobSystem.h"
#include "core/NonCopyable.h"
#include "core/Settings.h"
#include "core/Time.h"
#include "game/Simulation.h"
#include "game/SnapshotBuilder.h"
#include "input/InputState.h"
#include "math/Math.h"
#include "platform/Win32Window.h"
#include "render/IRenderer.h"
#include "ui/UI.h"

#include <Windows.h>
#include <array>
#include <cstdint>
#include <optional>

namespace engine::game
{
    // The player's persona through the app: Menu (the scene-select screen, no
    // simulation) or InGame (a scene stepping) - there is no title screen; Run()
    // boots straight into InGame(Circular). Settings is not a state of its own -
    // it's a modal UIContext overlay reachable from either (docs/scene-flow-design.md),
    // so adding it didn't need a third enumerator. Extend here (Loading,
    // Paused, Result, ...) the same way if a real game needs more states.
    enum class GameState
    {
        Menu,     // the scene-select menu - no simulation step
        InGame,
    };

    // Composition root and frame conductor. It owns the engine's systems and
    // wires them together, but delegates every real responsibility:
    //   Win32Window     - the OS window and its messages
    //   InputState      - this frame's keyboard/mouse
    //   UIContext       - the screen-space overlay
    //   Simulation      - the mutable world, stepped at a fixed rate
    //   SnapshotBuilder - world + UI -> value-only RenderSnapshot
    //   IRenderer       - an abstract renderer (DX11 today, DX12 tomorrow)
    //   Settings        - persisted user options (docs/game-settings.md)
    //
    // Application itself only sequences them and translates window events into
    // input, so its single reason to change is "the frame flow changed".
    class Application final : public platform::IWindowEventSink, private core::NonCopyable
    {
    public:
        Application(HINSTANCE instance, render::IRenderer& renderer);

        // Runs the main loop until the window closes. Returns the process code.
        int Run();

        // platform::IWindowEventSink
        void OnKey(int virtualKey, bool down) override;
        void OnMouseMove(math::Vec2 position) override;
        void OnMouseDelta(math::Vec2 delta) override;
        void OnMouseButton(int button, bool down) override;
        void OnMouseWheel(float notches) override;
        void OnFocusLost() override;
        void OnResize(int width, int height) override;
        void OnClose() override;

        // Global time scale for the simulation: 1 = normal, 0 = paused,
        // 0.5 = slow-mo, 2 = fast-forward. Applied by scaling the delta fed to
        // FixedTimestep (docs/time-design.md) - the fixed step size never
        // changes, so determinism holds. Per-actor local scale is separate and
        // lives in Simulation::Actor. A real game drives this from gameplay; the
        // demo cycles it from the keyboard (see OnKey).
        void SetGlobalTimeScale(float scale) { m_globalTimeScale = scale < 0.0f ? 0.0f : scale; }
        [[nodiscard]] float GlobalTimeScale() const { return m_globalTimeScale; }

    private:
        [[nodiscard]] PlayerIntent BuildPlayerIntent() const;

        // Scene transitions: swap the UIContext's screen and update m_state.
        // The lobby menu (not gameplay): GAME (Circular) and TEST SCENE (the
        // single dummy-mob weapon test) - the only two paths (LobbyScreen.h).
        // Reached from Settings' "LOBBY" button and after a DefenseCombat
        // loss; the app itself boots past it, straight into Circular.
        void EnterLobby();
        // `circularTest` selects Simulation::EnterCircularTestScene() instead
        // of the normal EnterScene(scene) - only meaningful with scene ==
        // DemoScene::Circular (LobbyScreen's "TEST SCENE" button).
        void EnterInGame(DemoScene scene, bool circularTest = false);
        // Settings overlay: layered on top of whichever screen is active.
        void OpenSettings();
        void CloseSettings();
        // Circular level-up modal (docs/circular-design.md): resolves a pick
        // the UI recorded last frame, then opens the modal whenever the
        // simulation is waiting on one. The pick is deferred (not applied in
        // the button callback) because clearing the overlay from inside its own
        // button's onClick would destroy the caller mid-call.
        void ServiceLevelUp();
        void OpenLevelUp();
        // Character-select modal (docs/circular-design.md §2.3): opens on entering
        // Circular and on F7; a pick starts a fresh run as that character. Same
        // deferred-pick pattern as the level-up modal. Esc closes it and keeps
        // the current run/character.
        void ServiceCharacterSelect();
        void OpenCharacterSelect();
        // Run-over modal (docs/circular-design.md §12.3 G): when the player's HP
        // hits 0 the simulation freezes; this offers RETRY (same character, fresh
        // run) or LOBBY. Same deferred-pick pattern; Esc does not close it.
        void ServiceRunOver();
        void OpenRunOver();
        // Writes the last CSV balance load (docs/circular-balance.md) to the
        // debugger Output window: one line per problem, plus a summary.
        void LogBalanceReport() const;
        // The two Settings fields with an effect outside the Settings struct
        // itself; everything else SettingsScreen mutates directly (see there).
        // Pushes both vsync and the frame-rate cap together - they're one
        // IRenderer::SetFrameSettings call (targetFramesPerSecond only takes
        // effect when !verticalSync).
        void ApplyFrameSettings();
        void ApplyResolution();
        void ApplyVolumes();   // push master/music/sfx from m_settings to m_audio

        render::IRenderer& m_renderer;
        core::JobSystem m_jobs;
        audio::AudioEngine m_audio;
        // Loaded before m_window so the very first window size already
        // reflects the saved resolution instead of opening at a hardcoded
        // size and only matching Settings after the user touches it.
        core::Settings m_settings;
        platform::Win32Window m_window;
        input::InputState m_input;
        ui::UIContext m_ui;
        Simulation m_simulation;
        SnapshotBuilder m_snapshotBuilder;
        core::FrameClock m_clock;
        core::FixedTimestep m_timestep;
        GameState m_state{ GameState::Menu };
        float m_globalTimeScale{ 1.0f };
        float m_fpsSmoothed{ 0.0f };   // EMA of 1/delta, shown top-right
        // Demo: auto-fire one crowd explosion this many seconds into a session.
        // Reset every time we (re)enter InGame. 'F' still triggers it manually.
        float m_inGameElapsed{ 0.0f };
        bool  m_autoExplodeFired{ false };

        std::uint64_t m_frameNumber{};
        math::Vec2 m_pointerPosition{};
        // Per-button: did the UI consume the press? The matching release is then
        // routed the same way so gameplay input never sees a half event.
        std::array<bool, 3> m_pointerConsumedByUI{};
        // Resize is requested on the window thread, applied at the top of a frame
        // (only the main thread talks to the renderer; only the render thread
        // calls ResizeBuffers).
        std::optional<SIZE> m_pendingResize;
        std::optional<std::size_t> m_pendingLevelChoice;   // set by the level-up modal's button, consumed by ServiceLevelUp
        bool m_levelUpOverlayOpen{ false };                // the current overlay IS the level-up modal (Esc must not close it)
        std::optional<std::size_t> m_pendingCharacter;     // set by the character-select modal's button, consumed by ServiceCharacterSelect
        bool m_characterSelectOpen{ false };               // the current overlay IS the character-select modal
        std::optional<std::size_t> m_pendingRunOver;       // run-over modal: 0 = retry, 1 = lobby (ServiceRunOver)
        bool m_runOverOpen{ false };                       // the current overlay IS the run-over modal
    };
}
