#pragma once

// 3D render module: the value types the 3D pass consumes. Built only when
// ENGINE_WITH_3D is defined. Peer of render/r2d; nothing here depends on 2D.

#include "math/Math3D.h"

#include "math/Math2D.h"   // math::Color is shared (lives in the 2D module)

#include <cstdint>
#include <vector>

namespace engine::render
{
    // Built-in meshes the 3D pass creates at startup. `MeshDraw::mesh` holds one
    // of these. Roadmap: a real mesh registry with an upload API and handles
    // replaces the enum once assets load from files.
    enum class MeshId : std::uint32_t
    {
        Cube = 0,
        Plane = 1,
        Count
    };

    // One 3D instance: which mesh, where (row-major world matrix), a flat color.
    struct MeshDraw
    {
        MeshId mesh{ MeshId::Cube };
        math::Mat4 world{ math::Mat4::Identity() };
        math::Color color{ 1.0f, 1.0f, 1.0f, 1.0f };
    };

    // Camera + lighting for the 3D pass this frame.
    struct CameraView
    {
        math::Mat4 view{ math::Mat4::Identity() };
        math::Mat4 projection{ math::Mat4::Identity() };
        // Direction the light travels, world space (need not be unit).
        math::Vec3 lightDirection{ 0.3f, -1.0f, 0.4f };
    };

    // One instance of the model that ModelMeshPass3D loaded at startup. Drawn as
    // static geometry (bind pose) with the same directional light as MeshDraw.
    // Skinning comes later - see docs/model-animation-research.md.
    struct ModelDraw
    {
        math::Mat4 world{ math::Mat4::Identity() };
        math::Color tint{ 1.0f, 1.0f, 1.0f, 1.0f };
    };

    // The 3D half of a RenderSnapshot.
    struct Scene3D
    {
        CameraView camera{};
        std::vector<MeshDraw> meshDraws;
        std::vector<ModelDraw> modelDraws;
    };
}
