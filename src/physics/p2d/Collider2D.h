#pragma once

#include "math/Math2D.h"
#include "physics/Collision.h"

#include <cmath>
#include <cstdint>

// 2D collision module. Peer of physics/p3d.
namespace engine::physics
{
    // Axis-aligned. `halfExtents` is used for Box, `radius` for Circle; the
    // unused one is ignored. No rotation (OBB is roadmap).
    struct Collider2D
    {
        enum class Shape : std::uint8_t { Box, Circle };

        Shape shape{ Shape::Box };
        math::Vec2 center{};
        math::Vec2 halfExtents{};
        float radius{};
        CollisionLayer layer{ 1u };
        CollisionLayer mask{ kAllLayers };
        std::uint64_t user{};
    };

    [[nodiscard]] inline bool Overlaps(const Collider2D& a, const Collider2D& b)
    {
        using Shape = Collider2D::Shape;

        if (a.shape == Shape::Box && b.shape == Shape::Box)
        {
            return std::fabs(a.center.x - b.center.x) <= a.halfExtents.x + b.halfExtents.x
                && std::fabs(a.center.y - b.center.y) <= a.halfExtents.y + b.halfExtents.y;
        }

        if (a.shape == Shape::Circle && b.shape == Shape::Circle)
        {
            const math::Vec2 delta = a.center - b.center;
            const float sumRadius = a.radius + b.radius;
            return math::Dot(delta, delta) <= sumRadius * sumRadius;
        }

        // Box vs Circle, either argument order: closest point on the box to the
        // circle centre, then compare distance to the radius.
        const Collider2D& box = a.shape == Shape::Box ? a : b;
        const Collider2D& circle = a.shape == Shape::Box ? b : a;
        const float closestX = math::Clamp(circle.center.x,
            box.center.x - box.halfExtents.x, box.center.x + box.halfExtents.x);
        const float closestY = math::Clamp(circle.center.y,
            box.center.y - box.halfExtents.y, box.center.y + box.halfExtents.y);
        const math::Vec2 delta{ circle.center.x - closestX, circle.center.y - closestY };
        return math::Dot(delta, delta) <= circle.radius * circle.radius;
    }
}
