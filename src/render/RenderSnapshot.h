#pragma once

#include "render/r2d/Sprite2D.h"

#include <cstdint>
#include <vector>

#if defined(ENGINE_WITH_3D)
#include "render/r3d/Scene3D.h"
#endif

namespace engine::render
{
    // Built by the main thread after simulation + UI finish for the frame. Owns
    // only values and its own buffers, never a pointer into the live world, so
    // the render thread consumes it at its own pace. The renderer keeps exactly
    // one (latest-frame mailbox) and discards older unrendered frames.
    //
    // It is a container of per-module payloads: the 3D module contributes
    // `scene3d` (only when ENGINE_WITH_3D), the 2D module contributes the quad
    // lists. Draw order across the default pipeline:
    //   1. mesh pass  - scene3d.meshDraws, depth-tested, perspective
    //   2. quad pass  - worldQuads then uiQuads, screen space, no depth
    struct RenderSnapshot
    {
        std::uint64_t frameNumber{};
        float clearColor[4]{ 0.06f, 0.07f, 0.10f, 1.0f };

#if defined(ENGINE_WITH_3D)
        Scene3D scene3d{};
#endif

        std::vector<Quad> worldQuads;
        std::vector<Quad> uiQuads;
    };
}
