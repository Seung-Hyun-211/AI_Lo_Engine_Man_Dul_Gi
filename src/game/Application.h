#pragma once

#include "core/JobSystem.h"
#include "core/NonCopyable.h"
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
    // Composition root and frame conductor. It owns the engine's systems and
    // wires them together, but delegates every real responsibility:
    //   Win32Window     - the OS window and its messages
    //   InputState      - this frame's keyboard/mouse
    //   UIContext       - the screen-space overlay
    //   Simulation      - the mutable world, stepped at a fixed rate
    //   SnapshotBuilder - world + UI -> value-only RenderSnapshot
    //   IRenderer       - an abstract renderer (DX11 today, DX12 tomorrow)
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
        void OnMouseButton(int button, bool down) override;
        void OnFocusLost() override;
        void OnResize(int width, int height) override;
        void OnClose() override;

    private:
        [[nodiscard]] PlayerIntent BuildPlayerIntent() const;

        render::IRenderer& m_renderer;
        core::JobSystem m_jobs;
        platform::Win32Window m_window;
        input::InputState m_input;
        ui::UIContext m_ui;
        Simulation m_simulation;
        SnapshotBuilder m_snapshotBuilder;
        core::FrameClock m_clock;
        core::FixedTimestep m_timestep;

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
