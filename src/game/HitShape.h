#pragma once

#include "math/Math2D.h"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iterator>

// The one hit-area value type of the Circular scene (docs/circular-combat.md
// §2.1). Weapons build a HitShape, MobField tests mobs against it, and the
// attack visual draws the very same value - so what you see is what hits.
// Plain value, no pointers: safe to copy into AttackVisual and across frames.
namespace engine::game
{
    // New shape = one enumerator here + one row in kShapeTests below + one
    // drawer in SnapshotBuilder's shape table.
    enum class HitShapeKind : std::uint8_t { Circle, Arc, Capsule, Count };

    struct HitShape
    {
        HitShapeKind kind{ HitShapeKind::Circle };
        math::Vec2 center{};        // Circle/Arc: centre. Capsule: segment start.
        math::Vec2 end{};           // Capsule: segment end.
        math::Vec2 dir{ 1.0f, 0.0f };   // Arc: facing, normalized ({0,0} = no facing: hits nothing)
        float radius{ 0.0f };       // Circle/Arc: reach. Capsule: half-width.
        float halfAngle{ 0.0f };    // Arc: radians either side of `dir`.

        [[nodiscard]] static HitShape Circle(math::Vec2 center, float radius)
        {
            HitShape s;
            s.kind = HitShapeKind::Circle;
            s.center = center;
            s.radius = radius;
            return s;
        }

        [[nodiscard]] static HitShape Arc(math::Vec2 center, math::Vec2 facing, float halfAngleRad, float radius)
        {
            HitShape s;
            s.kind = HitShapeKind::Arc;
            s.center = center;
            s.dir = math::Normalized(facing);
            s.radius = radius;
            s.halfAngle = halfAngleRad;
            return s;
        }

        [[nodiscard]] static HitShape Capsule(math::Vec2 start, math::Vec2 end, float halfWidth)
        {
            HitShape s;
            s.kind = HitShapeKind::Capsule;
            s.center = start;
            s.end = end;
            s.radius = halfWidth;
            return s;
        }
    };

    // Point-vs-shape tests, `pad` widening the shape (a mob's own radius -
    // docs/circular-combat.md D3: a mob is hit when any part of it overlaps).
    namespace hit_shape_detail
    {
        inline bool CircleContains(const HitShape& s, math::Vec2 p, float pad)
        {
            const float dx = p.x - s.center.x;
            const float dy = p.y - s.center.y;
            const float r = s.radius + pad;
            return dx * dx + dy * dy <= r * r;
        }

        inline bool ArcContains(const HitShape& s, math::Vec2 p, float pad)
        {
            if (s.dir.x == 0.0f && s.dir.y == 0.0f) return false;   // no facing to swing along
            const float dx = p.x - s.center.x;
            const float dy = p.y - s.center.y;
            const float distSq = dx * dx + dy * dy;
            const float r = s.radius + pad;
            if (distSq > r * r) return false;
            if (distSq <= 1e-6f) return true;   // a point exactly on the centre is always "in the cone"
            // The angle is measured at the mob's centre (pad only widens the
            // reach) - simple on purpose, docs §2.1.
            return (dx * s.dir.x + dy * s.dir.y) / std::sqrt(distSq) >= std::cos(s.halfAngle);
        }

        inline bool CapsuleContains(const HitShape& s, math::Vec2 p, float pad)
        {
            const math::Vec2 seg = s.end - s.center;
            const float segLenSq = math::Dot(seg, seg);
            const math::Vec2 toP{ p.x - s.center.x, p.y - s.center.y };
            float t = segLenSq > 1e-6f ? math::Dot(toP, seg) / segLenSq : 0.0f;
            t = t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);
            const float dx = p.x - (s.center.x + seg.x * t);
            const float dy = p.y - (s.center.y + seg.y * t);
            const float r = s.radius + pad;
            return dx * dx + dy * dy <= r * r;
        }

        using ContainsFn = bool (*)(const HitShape&, math::Vec2, float);
        inline constexpr ContainsFn kShapeTests[] = { &CircleContains, &ArcContains, &CapsuleContains };
        static_assert(std::size(kShapeTests) == static_cast<std::size_t>(HitShapeKind::Count), "one test per HitShapeKind");
    }

    // True when a disc of radius `pad` at `p` overlaps the shape.
    [[nodiscard]] inline bool ShapeContains(const HitShape& shape, math::Vec2 p, float pad)
    {
        return hit_shape_detail::kShapeTests[static_cast<std::size_t>(shape.kind)](shape, p, pad);
    }
}
