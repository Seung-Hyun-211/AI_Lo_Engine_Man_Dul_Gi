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

    // Squared distance from `p` to the nearest point of `box` (0 when inside).
    [[nodiscard]] inline float PointRectDistanceSq(math::Vec2 p, const math::Rect& box)
    {
        const float dx = p.x < box.x ? box.x - p.x : (p.x > box.x + box.width ? p.x - (box.x + box.width) : 0.0f);
        const float dy = p.y < box.y ? box.y - p.y : (p.y > box.y + box.height ? p.y - (box.y + box.height) : 0.0f);
        return dx * dx + dy * dy;
    }

    // Shape-vs-box tests: the player's body is its hitbox rect (player.csv
    // hitbox_*), so enemy attacks test the very collider the player moves with
    // (docs/circular-combat.md §2.1, W7) - same HitShape value as mob hits.
    namespace hit_shape_detail
    {
        inline math::Vec2 ClosestOnRect(math::Vec2 p, const math::Rect& box)
        {
            return { p.x < box.x ? box.x : (p.x > box.x + box.width ? box.x + box.width : p.x),
                     p.y < box.y ? box.y : (p.y > box.y + box.height ? box.y + box.height : p.y) };
        }

        inline float PointSegmentDistanceSq(math::Vec2 p, math::Vec2 a, math::Vec2 b)
        {
            const math::Vec2 seg = b - a;
            const float lenSq = math::Dot(seg, seg);
            float t = lenSq > 1e-6f ? math::Dot(p - a, seg) / lenSq : 0.0f;
            t = t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);
            const math::Vec2 d = p - (a + seg * t);
            return math::Dot(d, d);
        }

        // Slab test: does segment a->b cross the box?
        inline bool SegmentHitsRect(math::Vec2 a, math::Vec2 b, const math::Rect& box)
        {
            float t0 = 0.0f, t1 = 1.0f;
            const float d[2] = { b.x - a.x, b.y - a.y };
            const float o[2] = { a.x, a.y };
            const float lo[2] = { box.x, box.y };
            const float hi[2] = { box.x + box.width, box.y + box.height };
            for (int axis = 0; axis < 2; ++axis)
            {
                if (std::fabs(d[axis]) < 1e-6f)
                {
                    if (o[axis] < lo[axis] || o[axis] > hi[axis]) return false;
                    continue;
                }
                float tNear = (lo[axis] - o[axis]) / d[axis];
                float tFar = (hi[axis] - o[axis]) / d[axis];
                if (tNear > tFar) { const float tmp = tNear; tNear = tFar; tFar = tmp; }
                t0 = tNear > t0 ? tNear : t0;
                t1 = tFar < t1 ? tFar : t1;
                if (t0 > t1) return false;
            }
            return true;
        }

        inline bool CircleOverlapsRect(const HitShape& s, const math::Rect& box)
        {
            return PointRectDistanceSq(s.center, box) <= s.radius * s.radius;
        }

        // Approximate on purpose (KISS): in reach, and the box point nearest the
        // centre lies inside the fan (or the centre is inside the box).
        inline bool ArcOverlapsRect(const HitShape& s, const math::Rect& box)
        {
            if (s.dir.x == 0.0f && s.dir.y == 0.0f) return false;
            const math::Vec2 q = ClosestOnRect(s.center, box);
            const math::Vec2 d = q - s.center;
            const float distSq = math::Dot(d, d);
            if (distSq > s.radius * s.radius) return false;
            if (distSq <= 1e-6f) return true;
            return math::Dot(d, s.dir) / std::sqrt(distSq) >= std::cos(s.halfAngle);
        }

        // Segment-vs-box distance: 0 when they cross, else the nearest of
        // (segment ends -> box) and (box corners -> segment).
        inline bool CapsuleOverlapsRect(const HitShape& s, const math::Rect& box)
        {
            if (SegmentHitsRect(s.center, s.end, box)) return true;
            const float rSq = s.radius * s.radius;
            if (PointRectDistanceSq(s.center, box) <= rSq || PointRectDistanceSq(s.end, box) <= rSq) return true;
            const math::Vec2 corners[4] = { { box.x, box.y }, { box.x + box.width, box.y },
                                            { box.x, box.y + box.height }, { box.x + box.width, box.y + box.height } };
            for (const math::Vec2& c : corners)
                if (PointSegmentDistanceSq(c, s.center, s.end) <= rSq) return true;
            return false;
        }

        using RectFn = bool (*)(const HitShape&, const math::Rect&);
        inline constexpr RectFn kRectTests[] = { &CircleOverlapsRect, &ArcOverlapsRect, &CapsuleOverlapsRect };
        static_assert(std::size(kRectTests) == static_cast<std::size_t>(HitShapeKind::Count), "one rect test per HitShapeKind");
    }

    // True when the shape overlaps the box (the player's hitbox).
    [[nodiscard]] inline bool ShapeOverlapsRect(const HitShape& shape, const math::Rect& box)
    {
        return hit_shape_detail::kRectTests[static_cast<std::size_t>(shape.kind)](shape, box);
    }
}
