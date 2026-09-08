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

    // Hemisphere fill: `sky` tints surfaces facing up, `ground` those facing
    // down, blended by the world normal. Brighter and livelier than a flat grey.
    struct AmbientLight
    {
        math::Color sky{ 0.34f, 0.38f, 0.46f, 1.0f };
        math::Color ground{ 0.20f, 0.18f, 0.16f, 1.0f };
    };

    struct Lighting
    {
        DirectionalLight key{};
        AmbientLight ambient{};

        // View-projection for the directional shadow map, fitted to the scene by
        // the game layer (SnapshotBuilder). Identity + shadowsEnabled=false skips
        // shadow rendering entirely.
        math::Mat4 lightViewProj{ math::Mat4::Identity() };
        bool shadowsEnabled{ false };
    };
}
