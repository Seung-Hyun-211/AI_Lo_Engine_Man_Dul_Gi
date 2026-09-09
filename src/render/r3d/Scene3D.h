#pragma once

// 3D render module: the value types the 3D pass consumes. Built only when
// ENGINE_WITH_3D is defined. Peer of render/r2d; nothing here depends on 2D.

#include "math/Math3D.h"
#include "math/Math2D.h"   // math::Color is shared (lives in the 2D module)
#include "render/r3d/Lighting.h"

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

    // Camera for the 3D passes this frame.
    struct CameraView
    {
        math::Mat4 view{ math::Mat4::Identity() };
        math::Mat4 projection{ math::Mat4::Identity() };
    };

    // One instance of the model that ModelMeshPass3D loaded at startup, with
    // the same directional light as MeshDraw. `animClipIndex < 0` (the
    // default) draws the bind pose; otherwise ModelMeshPass3D samples that
    // clip (its own index into the clips it loaded - see
    // render/r3d/CharacterAnimationClips.h) at `animClipTime` seconds and
    // CPU-skins the mesh before drawing. See docs/model-animation-research.md
    // §5.2/5.3 - only one instance is skinned per pass (one shared vertex
    // buffer set, "loaded once at startup").
    struct ModelDraw
    {
        math::Mat4 world{ math::Mat4::Identity() };
        math::Color tint{ 1.0f, 1.0f, 1.0f, 1.0f };
        int animClipIndex{ -1 };
        float animClipTime{ 0.0f };
    };

    // The 3D half of a RenderSnapshot.
    struct Scene3D
    {
        CameraView camera{};
        Lighting lighting{};
        std::vector<MeshDraw> meshDraws;
        std::vector<ModelDraw> modelDraws;
    };
}
