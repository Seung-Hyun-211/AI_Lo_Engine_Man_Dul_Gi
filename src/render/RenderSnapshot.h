#pragma once

#include <cstdint>
#include <vector>

namespace engine::render
{
    // Value-only UI/world primitive. It crosses the main-thread-to-render-thread
    // boundary as part of a RenderSnapshot, so it cannot reference a Widget.
    struct Quad { float x, y, width, height, r, g, b, a; };
    // Main thread creates this value after simulation. It owns no pointers to
    // mutable gameplay objects, so the render thread can consume it safely.
    struct RenderSnapshot
    {
        std::uint64_t frameNumber{};
        float playerX{};
        float playerY{};
        std::uint32_t simulatedSpriteCount{};
        float clearColor[4]{ 0.06f, 0.07f, 0.10f, 1.0f };
        std::vector<Quad> uiQuads;
    };
}
