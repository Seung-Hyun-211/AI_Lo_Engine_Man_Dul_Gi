#pragma once

#include "math/Math2D.h"
#include "physics/Collision.h"

#include <cmath>
#include <cstdint>
#include <utility>

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

    // Ray vs one collider (2D). `dir` must be unit length; `outDistance` is then
    // in the same units as the positions. Returns false on a miss, a hit behind
    // the origin, or a hit past `maxDistance`. `outNormal` is the outward
    // surface normal at the hit (points back along the ray when the origin is
    // inside). Detection only - 3D counterpart is physics/p3d/Collider3D.h.
    [[nodiscard]] inline bool RaycastCollider(const Collider2D& collider,
                                              math::Vec2 origin, math::Vec2 dir, float maxDistance,
                                              float& outDistance, math::Vec2& outNormal)
    {
        if (collider.shape == Collider2D::Shape::Circle)
        {
            const math::Vec2 m = origin - collider.center;
            const float b = math::Dot(m, dir);
            const float c = math::Dot(m, m) - collider.radius * collider.radius;
            if (c > 0.0f && b > 0.0f) return false;          // outside, pointing away
            const float disc = b * b - c;
            if (disc < 0.0f) return false;
            float t = -b - std::sqrt(disc);
            if (t < 0.0f) t = 0.0f;                          // origin inside the circle
            if (t > maxDistance) return false;
            outDistance = t;
            outNormal = math::Normalized((origin + dir * t) - collider.center);
            return true;
        }

        // Box (AABB), slab method over 2 axes.
        const float o[2]{ origin.x, origin.y };
        const float d[2]{ dir.x, dir.y };
        const float lo[2]{ collider.center.x - collider.halfExtents.x,
                           collider.center.y - collider.halfExtents.y };
        const float hi[2]{ collider.center.x + collider.halfExtents.x,
                           collider.center.y + collider.halfExtents.y };
        float tmin = 0.0f, tmax = maxDistance;
        int   axis = -1;
        float sign = -1.0f;
        for (int a = 0; a < 2; ++a)
        {
            if (std::fabs(d[a]) < 1e-8f)
            {
                if (o[a] < lo[a] || o[a] > hi[a]) return false;   // parallel and outside the slab
                continue;
            }
            const float inv = 1.0f / d[a];
            float t1 = (lo[a] - o[a]) * inv;
            float t2 = (hi[a] - o[a]) * inv;
            float faceSign = -1.0f;
            if (t1 > t2) { std::swap(t1, t2); faceSign = 1.0f; }
            if (t1 > tmin) { tmin = t1; axis = a; sign = faceSign; }
            if (t2 < tmax) tmax = t2;
            if (tmin > tmax) return false;
        }
        outDistance = tmin;
        outNormal = {};
        if (axis == 0)      outNormal.x = sign;
        else if (axis == 1) outNormal.y = sign;
        else                outNormal = dir * -1.0f;           // origin inside the box
        return true;
    }
}
