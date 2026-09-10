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

    // One member of an instanced crowd. Uploaded verbatim as a per-instance
    // vertex stream (slot 1) - byte layout must match mesh_instanced.hlsl's
    // per-instance inputs and MeshPass3D's instanced input layout. Transform is
    // compact (position + Y-rotation + uniform scale); see
    // docs/instanced-rendering.md §3/§10 for why, and when to widen it.
    struct MeshInstance
    {
        math::Vec3    pos{};              // world                (offset 0)
        float         yaw{ 0.0f };        // radians, Y axis      (offset 12)
        float         scale{ 1.0f };      // uniform              (offset 16)
        std::uint32_t colorRgba{ 0xffffffffu };   // 8:8:8:8, unpacked in the VS (offset 20)
    };                                    // 24 bytes, no padding

    // A contiguous run of Scene3D::meshInstances that share a mesh (and LOD).
    // One batch == one DrawIndexedInstanced call. docs/instanced-rendering.md §3.
    struct InstanceBatch
    {
        MeshId        mesh{ MeshId::Cube };
        std::uint32_t first{ 0 };         // start index into Scene3D::meshInstances
        std::uint32_t count{ 0 };
        std::uint16_t lod{ 0 };           // 0 near / 1 mid / 2 far - reserved (LOD buckets: §5)
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

        // Pose selection. `animClipIndex < 0` = bind pose. `animParametric` means
        // `animClipTime` is a 0..1 phase (renderer scales by the clip's
        // duration, PlayMode::Once); otherwise it is seconds mapped by
        // `animPlayMode` (0 Loop / 1 Once / 2 PingPong - see anim::PlayMode).
        // When `animBlend` > 0 the renderer also evaluates `animFrom*` and lerps
        // toward the current clip. See docs/roadmap.md §2.1.
        int   animClipIndex{ -1 };
        float animClipTime{ 0.0f };
        int   animPlayMode{ 0 };
        bool  animParametric{ false };
        int   animFromClipIndex{ -1 };
        float animFromClipTime{ 0.0f };
        int   animFromPlayMode{ 0 };
        float animBlend{ 0.0f };
    };

    // One world-space line segment for DebugDrawPass. Value type, so game/sim
    // code fills these into the snapshot and the render thread just draws them.
    // Not for shipping visuals - collider/ray/skeleton visualisation. See
    // docs/roadmap.md §D1.
    struct DebugLine
    {
        math::Vec3 a{};
        math::Vec3 b{};
        math::Color color{ 1.0f, 1.0f, 0.0f, 1.0f };
    };

    // The 3D half of a RenderSnapshot.
    struct Scene3D
    {
        CameraView camera{};
        Lighting lighting{};
        std::vector<MeshDraw> meshDraws;
        std::vector<ModelDraw> modelDraws;
        std::vector<DebugLine> debugLines;

        // Instanced crowds: every batch's instances live contiguously in
        // meshInstances; instanceBatches slices it. docs/instanced-rendering.md.
        std::vector<MeshInstance> meshInstances;
        std::vector<InstanceBatch> instanceBatches;
    };

    // --- debug-line builders (header-only; call from wherever fills a Scene3D) ---
    namespace debug
    {
        inline void Line(std::vector<DebugLine>& out, math::Vec3 a, math::Vec3 b, math::Color c)
        {
            out.push_back({ a, b, c });
        }

        // Axis-aligned box from centre + half-extents (12 edges).
        inline void Box(std::vector<DebugLine>& out, math::Vec3 center, math::Vec3 half, math::Color c)
        {
            const float xs[2]{ center.x - half.x, center.x + half.x };
            const float ys[2]{ center.y - half.y, center.y + half.y };
            const float zs[2]{ center.z - half.z, center.z + half.z };
            for (int i = 0; i < 2; ++i)
                for (int j = 0; j < 2; ++j)
                {
                    Line(out, { xs[0], ys[i], zs[j] }, { xs[1], ys[i], zs[j] }, c);
                    Line(out, { xs[i], ys[0], zs[j] }, { xs[i], ys[1], zs[j] }, c);
                    Line(out, { xs[i], ys[j], zs[0] }, { xs[i], ys[j], zs[1] }, c);
                }
        }

        // Sphere as three axis-aligned rings.
        inline void Sphere(std::vector<DebugLine>& out, math::Vec3 center, float radius, math::Color c, int segments = 16)
        {
            const int n = segments < 3 ? 3 : segments;
            for (int s = 0; s < n; ++s)
            {
                const float a0 = (6.2831853f * static_cast<float>(s)) / static_cast<float>(n);
                const float a1 = (6.2831853f * static_cast<float>(s + 1)) / static_cast<float>(n);
                const float c0 = std::cos(a0) * radius, s0 = std::sin(a0) * radius;
                const float c1 = std::cos(a1) * radius, s1 = std::sin(a1) * radius;
                Line(out, { center.x + c0, center.y + s0, center.z }, { center.x + c1, center.y + s1, center.z }, c);
                Line(out, { center.x + c0, center.y, center.z + s0 }, { center.x + c1, center.y, center.z + s1 }, c);
                Line(out, { center.x, center.y + c0, center.z + s0 }, { center.x, center.y + c1, center.z + s1 }, c);
            }
        }

        // A ray as a line plus a small "+" at the hit end.
        inline void Ray(std::vector<DebugLine>& out, math::Vec3 origin, math::Vec3 dir, float length, math::Color c)
        {
            const math::Vec3 end{ origin.x + dir.x * length, origin.y + dir.y * length, origin.z + dir.z * length };
            Line(out, origin, end, c);
            const float k = 0.06f;
            Line(out, { end.x - k, end.y, end.z }, { end.x + k, end.y, end.z }, c);
            Line(out, { end.x, end.y - k, end.z }, { end.x, end.y + k, end.z }, c);
            Line(out, { end.x, end.y, end.z - k }, { end.x, end.y, end.z + k }, c);
        }
    }
}
