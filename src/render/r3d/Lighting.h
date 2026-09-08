#pragma once

#include "math/Math2D.h"   // math::Color
#include "math/Math3D.h"   // math::Vec3

// Scene lighting, value types only. Carried in Scene3D, consumed by every 3D
// pass through the shared Frame constant buffer (see common3d.hlsli and
// render/r3d/FrameConstants.h). Design notes: docs/lighting.md.
namespace engine::render
{
    // The single key light. `direction` is the direction light travels in world
    // space (need not be unit; normalised on upload). `color.a` is intensity.
    struct DirectionalLight
    {
        math::Vec3 direction{ 0.3f, -0.6f, 0.7f };   // from front-upper toward the scene
        math::Color color{ 1.0f, 0.97f, 0.90f, 1.0f };
    };

    // Flat fill added to every lit surface so nothing goes fully black.
    struct AmbientLight
    {
        math::Color color{ 0.16f, 0.17f, 0.20f, 1.0f };
    };

    struct Lighting
    {
        DirectionalLight key{};
        AmbientLight ambient{};
    };
}
