#pragma once

// 2D render module: the value types the 2D pass consumes. Peer of render/r3d.
// Nothing here depends on 3D.

namespace engine::render
{
    // A solid, axis-aligned, colored rectangle in pixel space. Plain value so it
    // crosses the main-to-render-thread boundary inside a RenderSnapshot without
    // referencing any mutable gameplay or UI object.
    //
    // Roadmap: a textured SpriteDraw (atlas id + uv rect) will sit next to Quad
    // once the SpriteBatch pipeline exists.
    struct Quad
    {
        float x{}, y{}, width{}, height{};
        float r{}, g{}, b{}, a{ 1.0f };
    };
}
