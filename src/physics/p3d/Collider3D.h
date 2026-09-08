#pragma once

#include "math/Math2D.h"   // math::Clamp is shared (lives in the 2D module)
#include "math/Math3D.h"
#include "physics/Collision.h"

#include <cmath>
#include <cstdint>

// 3D collision module. Built only when ENGINE_WITH_3D is defined. Peer of
// physics/p2d.
namespace engine::physics
{
    // Axis-aligned. `halfExtents` for Box, `radius` for Sphere.
    struct Collider3D
    {
        enum class Shape : std::uint8_t { Box, Sphere };

        Shape shape{ Shape::Box };
        math::Vec3 center{};
        math::Vec3 halfExtents{};
        float radius{};
        CollisionLayer layer{ 1u };
        CollisionLayer mask{ kAllLayers };
        std::uint64_t user{};
    };

    [[nodiscard]] inline bool Overlaps(const Collider3D& a, const Collider3D& b)
    {
        using Shape = Collider3D::Shape;

        if (a.shape == Shape::Box && b.shape == Shape::Box)
        {
            return std::fabs(a.center.x - b.center.x) <= a.halfExtents.x + b.halfExtents.x
                && std::fabs(a.center.y - b.center.y) <= a.halfExtents.y + b.halfExtents.y
                && std::fabs(a.center.z - b.center.z) <= a.halfExtents.z + b.halfExtents.z;
        }

        if (a.shape == Shape::Sphere && b.shape == Shape::Sphere)
        {
            const math::Vec3 delta = a.center - b.center;
            const float sumRadius = a.radius + b.radius;
            return math::Dot(delta, delta) <= sumRadius * sumRadius;
        }

        // Box vs Sphere, either order.
        const Collider3D& box = a.shape == Shape::Box ? a : b;
        const Collider3D& sphere = a.shape == Shape::Box ? b : a;
        const float closestX = math::Clamp(sphere.center.x,
            box.center.x - box.halfExtents.x, box.center.x + box.halfExtents.x);
        const float closestY = math::Clamp(sphere.center.y,
            box.center.y - box.halfExtents.y, box.center.y + box.halfExtents.y);
        const float closestZ = math::Clamp(sphere.center.z,
            box.center.z - box.halfExtents.z, box.center.z + box.halfExtents.z);
        const math::Vec3 delta{ sphere.center.x - closestX,
                                sphere.center.y - closestY,
                                sphere.center.z - closestZ };
        return math::Dot(delta, delta) <= sphere.radius * sphere.radius;
    }
}
