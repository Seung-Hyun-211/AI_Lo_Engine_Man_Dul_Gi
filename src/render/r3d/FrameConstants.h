#pragma once

#include "math/Math3D.h"
#include "render/r3d/Lighting.h"
#include "render/r3d/Scene3D.h"

#include <cstring>

// The per-frame constant buffer shared by every 3D pass (register b0). Layout
// must match `cbuffer Frame` in assets/shaders/common3d.hlsli.
namespace engine::render
{
    struct FrameConstantsGpu
    {
        float viewProj[16];
        float keyDirection[4];   // xyz = normalised travel direction, w = intensity
        float keyColor[4];       // rgb
        float ambientColor[4];   // rgb
    };

    inline void FillFrameConstants(const CameraView& camera, const Lighting& lighting, FrameConstantsGpu& out)
    {
        const math::Mat4 viewProj = camera.view * camera.projection;
        std::memcpy(out.viewProj, viewProj.m, sizeof(out.viewProj));

        const math::Vec3 dir = math::Normalized(lighting.key.direction);
        out.keyDirection[0] = dir.x;
        out.keyDirection[1] = dir.y;
        out.keyDirection[2] = dir.z;
        out.keyDirection[3] = lighting.key.color.a;   // intensity

        out.keyColor[0] = lighting.key.color.r;
        out.keyColor[1] = lighting.key.color.g;
        out.keyColor[2] = lighting.key.color.b;
        out.keyColor[3] = 1.0f;

        out.ambientColor[0] = lighting.ambient.color.r;
        out.ambientColor[1] = lighting.ambient.color.g;
        out.ambientColor[2] = lighting.ambient.color.b;
        out.ambientColor[3] = 1.0f;
    }
}
