#pragma once

#include "render/RenderSnapshot.h"

#include <Windows.h>
#include <cstdint>

namespace engine::render
{
    struct FrameSettings
    {
        // 0 means no software cap. VSync takes precedence when enabled:
        //   verticalSync = true                       -> Present(1, 0), display-capped
        //   verticalSync = false, target > 0          -> Present(0, 0) + sleep to target
        //   verticalSync = false, target = 0          -> uncapped
        std::uint32_t targetFramesPerSecond{ 60 };
        bool verticalSync{ true };
    };

    // Abstraction the game layer depends on instead of a concrete backend, so a
    // future DX12 renderer is a drop-in replacement. The implementation owns its
    // own render thread and is the sole owner of every GPU object after Start().
    //
    // Threading contract (see docs/multithreaded_game_engine_architecture.md):
    //   - Start / Stop / SetFrameSettings / Submit / Resize are called from the
    //     main thread and are non-blocking (Start blocks only until the device
    //     is created or creation fails).
    //   - The implementation never touches mutable game state; it only reads the
    //     value-based RenderSnapshot handed to Submit.
    class IRenderer
    {
    public:
        virtual ~IRenderer() = default;

        // Creates the device/swap chain and spins up the render thread. Throws on
        // failure (e.g. no DirectX 11 capable adapter).
        virtual void Start(HWND window, std::uint32_t initialWidth, std::uint32_t initialHeight) = 0;

        virtual void SetFrameSettings(FrameSettings settings) = 0;

        // Hands over the newest frame. If a previous snapshot has not been drawn
        // yet it is dropped (latest-frame mailbox).
        virtual void Submit(RenderSnapshot snapshot) = 0;

        // Requests a back-buffer resize. The actual ResizeBuffers call happens on
        // the render thread at a safe point.
        virtual void Resize(std::uint32_t width, std::uint32_t height) = 0;

        // Stops the render thread and releases every GPU object. Idempotent.
        virtual void Stop() = 0;
    };
}
