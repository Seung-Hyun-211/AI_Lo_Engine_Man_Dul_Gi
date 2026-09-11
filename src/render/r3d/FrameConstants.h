#pragma once

#include "math/Math3D.h"
#include "render/r3d/Lighting.h"
#include "render/r3d/Scene3D.h"

#include <cstring>

// The per-frame constant buffer shared by every 3D pass (register b0). Layout
// must match `cbuffer Frame` in assets/shaders/common3d.hlsli.
namespace engine::render
{
    // Was 2048 (pre-cascade stopgap for scene 2's wide frustum, docs/shadows.md).
    // Per-cascade resolution - both cascades render into the same size texture
    // array slice (render/r3d/Lighting.h kShadowCascadeCount). Costs ~64MB *
    // cascade count for the depth texture (R32) instead of ~16MB each at 2048.
    inline constexpr unsigned kShadowMapSize = 4096;

    struct FrameConstantsGpu
    {
        float viewProj[16];
        // Camera view alone (world -> view space), row-major, no transpose needed
        // (same convention as viewProj). Lets a pixel shader rotate a world-space
        // normal into view space (docs/post-process-gbuffer-research.md §3.3) or
        // pull the camera's world-space right/up axes from rows 0/1 for billboard
        // construction (docs/particle-system-research.md §3) without either
        // feature needing its own cbuffer field.
        float view[16];
        float cascadeViewProj[kShadowCascadeCount][16]; // world -> shadow map clip space, one per cascade
        float keyDirection[4];   // xyz = normalised travel direction, w = intensity
        float keyColor[4];       // rgb
        float ambientSky[4];     // rgb, hemisphere fill from above
        float ambientGround[4];  // rgb, hemisphere fill from below
        float shadowParams[4];   // x = texel size, y = depth bias, z = enabled (0/1)
    };

    inline void FillFrameConstants(const CameraView& camera, const Lighting& lighting, FrameConstantsGpu& out)
    {
        const math::Mat4 viewProj = camera.view * camera.projection;
        std::memcpy(out.viewProj, viewProj.m, sizeof(out.viewProj));
        std::memcpy(out.view, camera.view.m, sizeof(out.view));
        for (int i = 0; i < kShadowCascadeCount; ++i)
            std::memcpy(out.cascadeViewProj[i], lighting.cascadeViewProj[i].m, sizeof(out.cascadeViewProj[i]));

        const math::Vec3 dir = math::Normalized(lighting.key.direction);
        out.keyDirection[0] = dir.x;
        out.keyDirection[1] = dir.y;
        out.keyDirection[2] = dir.z;
        out.keyDirection[3] = lighting.key.color.a;   // intensity

        out.keyColor[0] = lighting.key.color.r;
        out.keyColor[1] = lighting.key.color.g;
        out.keyColor[2] = lighting.key.color.b;
        out.keyColor[3] = 1.0f;

        out.ambientSky[0] = lighting.ambient.sky.r;
        out.ambientSky[1] = lighting.ambient.sky.g;
        out.ambientSky[2] = lighting.ambient.sky.b;
        out.ambientSky[3] = 1.0f;

        out.ambientGround[0] = lighting.ambient.ground.r;
        out.ambientGround[1] = lighting.ambient.ground.g;
        out.ambientGround[2] = lighting.ambient.ground.b;
        out.ambientGround[3] = 1.0f;

        out.shadowParams[0] = 1.0f / static_cast<float>(kShadowMapSize);
        // Depth bias, normalised NDC-z, shared by both cascades (an
        // approximation - they cover different depth ranges, docs/shadows.md).
        // Tightened from 0.0018 alongside the lower rasterizer bias
        // (Dx11Renderer::CreateShadowResources) to cut peter-panning without
        // bringing back acne.
        out.shadowParams[1] = 0.0012f;
        out.shadowParams[2] = lighting.shadowsEnabled ? 1.0f : 0.0f;
        out.shadowParams[3] = 0.0f;
    }
}
