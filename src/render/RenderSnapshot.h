#pragma once

#include <cstdint>
#include <vector>

namespace engine::render
{
    // The only primitive the renderer understands. A solid, axis-aligned,
    // colored rectangle in pixel space. It is a plain value so it can cross the
    // main-thread-to-render-thread boundary inside a RenderSnapshot without
    // referencing any mutable gameplay or UI object.
    //
    // Roadmap: a textured `SpriteDraw` (atlas id + uv rect) will sit next to Quad
    // here once the SpriteBatch pipeline exists. Until then, gameplay draws with
    // Quads too and the renderer stays free of game-specific fields.
    struct Quad
    {
        float x{}, y{}, width{}, height{};
        float r{}, g{}, b{}, a{ 1.0f };
    };

    // Built by the main thread after simulation + UI have finished for the frame.
    // Owns only values and its own buffers, never a pointer into the live world,
    // so the render thread can consume it at its own pace. The renderer keeps
    // exactly one of these (latest-frame mailbox) and discards older unrendered
    // frames.
    struct RenderSnapshot
    {
        std::uint64_t frameNumber{};
        float clearColor[4]{ 0.06f, 0.07f, 0.10f, 1.0f };

        // Drawn first, in order: the game world (player, sprites, particles).
        std::vector<Quad> worldQuads;
        // Drawn last, in order, on top of the world: the UI overlay.
        std::vector<Quad> uiQuads;
    };
}
