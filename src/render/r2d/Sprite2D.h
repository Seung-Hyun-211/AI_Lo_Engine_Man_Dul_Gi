#pragma once

// 2D render module: the value types the 2D pass consumes. Peer of render/r3d.
// Nothing here depends on 3D.

#include "math/Math2D.h"   // math::Rect for SpriteDraw::clip

#include <cstdint>

namespace engine::render
{
    // A solid, axis-aligned, colored rectangle in pixel space. Plain value so it
    // crosses the main-to-render-thread boundary inside a RenderSnapshot without
    // referencing any mutable gameplay or UI object. Consumed by QuadPass2D
    // (world overlay + legacy UI).
    struct Quad
    {
        float x{}, y{}, width{}, height{};
        float r{}, g{}, b{}, a{ 1.0f };
    };

    // A textured, tinted, axis-aligned rectangle in pixel space, sampling one
    // atlas page, optionally scissor-clipped. Consumed by SpritePass2D. UV is
    // already resolved (name -> rect) on the main thread; the render thread only
    // needs `atlasId` to bind the right SRV. `atlasId == 0` = the pass's built-in
    // 1x1 white texture, so a solid rect is just a white sprite with a tint.
    // `clip.width <= 0` = no scissor (draw against the full viewport). See
    // docs/texture-atlas-and-sprite-pass.md, docs/atlas-build-pipeline.md.
    struct SpriteDraw
    {
        float x{}, y{}, width{}, height{};       // dest, pixel space
        float u0{ 0.0f }, v0{ 0.0f }, u1{ 1.0f }, v1{ 1.0f };
        float r{ 1.0f }, g{ 1.0f }, b{ 1.0f }, a{ 1.0f };
        std::uint32_t atlasId{ 0 };
        math::Rect clip{ 0.0f, 0.0f, 0.0f, 0.0f };   // width <= 0 -> unclipped
    };
}
