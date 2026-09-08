#pragma once

#include "math/Math.h"

#include <cstdint>
#include <vector>

namespace engine::render
{
    // A solid, axis-aligned, colored rectangle in pixel space. Value type; it
    // crosses the main-to-render-thread boundary inside a RenderSnapshot without
    // referencing any mutable gameplay or UI object. Consumed by the 2D pass.
    struct Quad
    {
        float x{}, y{}, width{}, height{};
        float r{}, g{}, b{}, a{ 1.0f };
    };

    // Built-in meshes the 3D pass creates at startup. `MeshDraw::mesh` holds one
    // of these values. Roadmap: a real mesh registry with an upload API and
    // handles replaces the enum once assets are loaded from files.
    enum class MeshId : std::uint32_t
    {
        Cube = 0,
        Plane = 1,
        Count
    };

    // One 3D instance for the mesh pass: which mesh, where (row-major world
    // matrix), and a flat color. Lit by a single directional light.
    struct MeshDraw
    {
        MeshId mesh{ MeshId::Cube };
        math::Mat4 world{ math::Mat4::Identity() };
        math::Color color{ 1.0f, 1.0f, 1.0f, 1.0f };
    };

    // Camera and lighting for the 3D pass this frame. Built by the game layer.
    struct CameraView
    {
        math::Mat4 view{ math::Mat4::Identity() };
        math::Mat4 projection{ math::Mat4::Identity() };
        // Direction the light travels, in world space (does not need to be unit).
        math::Vec3 lightDirection{ 0.3f, -1.0f, 0.4f };
    };

    // Built by the main thread after simulation + UI finish for the frame. Owns
    // only values and its own buffers, never a pointer into the live world, so
    // the render thread consumes it at its own pace. The renderer keeps exactly
    // one (latest-frame mailbox) and discards older unrendered frames.
    //
    // Draw order across the default pipeline:
    //   1. mesh pass   - meshDraws, depth-tested, perspective
    //   2. quad pass    - worldQuads then uiQuads, screen space, no depth
    struct RenderSnapshot
    {
        std::uint64_t frameNumber{};
        float clearColor[4]{ 0.06f, 0.07f, 0.10f, 1.0f };

        CameraView camera{};
        std::vector<MeshDraw> meshDraws;

        std::vector<Quad> worldQuads;
        std::vector<Quad> uiQuads;
    };
}
