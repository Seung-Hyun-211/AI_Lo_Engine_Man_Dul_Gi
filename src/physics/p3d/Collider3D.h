#pragma once

#include "math/Math2D.h"   // math::Clamp is shared (lives in the 2D module)
#include "math/Math3D.h"
#include "physics/Collision.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <utility>

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

    // Ray vs one collider. `dir` must be unit length; `outDistance` is then in
    // metres. Returns false on a miss, a hit behind the origin, or a hit past
    // `maxDistance`. `outNormal` is the outward surface normal at the hit (for a
    // ray starting inside, it points back along the ray). Detection only.
    [[nodiscard]] inline bool RaycastCollider(const Collider3D& collider,
                                              math::Vec3 origin, math::Vec3 dir, float maxDistance,
                                              float& outDistance, math::Vec3& outNormal)
    {
        if (collider.shape == Collider3D::Shape::Sphere)
        {
            const math::Vec3 m = origin - collider.center;
            const float b = math::Dot(m, dir);
            const float c = math::Dot(m, m) - collider.radius * collider.radius;
            if (c > 0.0f && b > 0.0f) return false;          // outside, pointing away
            const float disc = b * b - c;
            if (disc < 0.0f) return false;
            float t = -b - std::sqrt(disc);
            if (t < 0.0f) t = 0.0f;                          // origin inside the sphere
            if (t > maxDistance) return false;
            outDistance = t;
            outNormal = math::Normalized((origin + dir * t) - collider.center);
            return true;
        }

        // Box (AABB), slab method.
        const float o[3]{ origin.x, origin.y, origin.z };
        const float d[3]{ dir.x, dir.y, dir.z };
        const float lo[3]{ collider.center.x - collider.halfExtents.x,
                           collider.center.y - collider.halfExtents.y,
                           collider.center.z - collider.halfExtents.z };
        const float hi[3]{ collider.center.x + collider.halfExtents.x,
                           collider.center.y + collider.halfExtents.y,
                           collider.center.z + collider.halfExtents.z };
        float tmin = 0.0f, tmax = maxDistance;
        int   axis = -1;
        float sign = -1.0f;
        for (int a = 0; a < 3; ++a)
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
        else if (axis == 2) outNormal.z = sign;
        else                outNormal = dir * -1.0f;           // origin inside the box
        return true;
    }
}
