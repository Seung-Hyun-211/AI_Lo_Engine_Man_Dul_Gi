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
#include "render/r2d/TextureAtlas.h"
#include "ui/UI.h"

#include <Windows.h>
#include <array>
#include <cstdint>
#include <optional>

namespace engine::game
{
    // The player's persona through the app: Title (menu, no simulation) or
    // InGame (the demo world stepping). Settings is not a state of its own -
    // it's a modal UIContext overlay reachable from either (docs/scene-flow-design.md),
    // so adding it didn't need a third enumerator. Extend here (Loading,
    // Paused, Result, ...) the same way if a real game needs more states.
    enum class GameState
    {
        Title,
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
        void EnterTitle();
        void EnterInGame();
        // Menu-side screen (not gameplay): the ScrollList item demo.
        void EnterItems();
        // Settings overlay: layered on top of whichever screen is active.
        void OpenSettings();
        void CloseSettings();
        // The two Settings fields with an effect outside the Settings struct
        // itself; everything else SettingsScreen mutates directly (see there).
        void ApplyVsync();
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
        // Resident UI atlas manifest (name -> uv). The matching page pixels are
        // loaded by SpritePass2D on the render thread; this CPU side is read by
        // the snapshot builder to resolve named sprites.
        render::AtlasIndex m_uiAtlas;
        core::FrameClock m_clock;
        core::FixedTimestep m_timestep;
        GameState m_state{ GameState::Title };
        float m_globalTimeScale{ 1.0f };

        std::uint64_t m_frameNumber{};
        math::Vec2 m_pointerPosition{};
        // Per-button: did the UI consume the press? The matching release is then
        // routed the same way so gameplay input never sees a half event.
        std::array<bool, 3> m_pointerConsumedByUI{};
        // Resize is requested on the window thread, applied at the top of a frame
        // (only the main thread talks to the renderer; only the render thread
        // calls ResizeBuffers).
        std::optional<SIZE> m_pendingResize;
    };
}
