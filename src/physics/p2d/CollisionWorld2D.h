#pragma once

#include "core/NonCopyable.h"
#include "physics/p2d/Collider2D.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <unordered_map>
#include <vector>

namespace engine::physics
{
    // A directed ray for the 2D raycast queries. `dir` must be unit length.
    // `maxDistance` bounds the search - the "objects within a certain range"
    // query is just this with a finite value. `mask` keeps only colliders whose
    // layer is in it; `ignoreId` skips one collider (typically the caster's own).
    // 3D counterpart is physics/p3d Ray3D.
    struct Ray2D
    {
        math::Vec2     origin{};
        math::Vec2     dir{ 0.0f, 1.0f };
        float          maxDistance{ 3.402823e38f };
        CollisionLayer mask{ kAllLayers };
        ColliderId     ignoreId{ kInvalidCollider };
    };

    struct RayHit2D
    {
        ColliderId    id{ kInvalidCollider };
        std::uint64_t user{};        // the collider's `user` (usually an entity id)
        float         distance{};
        math::Vec2    point{};
        math::Vec2    normal{};
    };

    // Holds a set of 2D colliders and, each Step(), reports which pairs overlap.
    // Detection only - it never moves a collider. Main-thread use only.
    //
    // Broadphase is currently brute force (N^2); fine for hundreds of colliders.
    // A uniform grid goes inside Step() when profiling shows it is needed; the
    // interface does not change.
    class CollisionWorld2D final : private core::NonCopyable
    {
    public:
        // Registers a collider and returns its id. Ids are valid until Clear().
        ColliderId Add(const Collider2D& collider);
        void Update(ColliderId id, const Collider2D& collider);
        void Remove(ColliderId id);
        void Clear();

        // Recomputes the overlapping-pair list. Deterministic given the same
        // sequence of Add/Update/Remove calls.
        void Step();

        [[nodiscard]] const std::vector<Contact>& Contacts() const { return m_contacts; }

        // True if some contact this step links these two user values.
        [[nodiscard]] bool AreTouching(std::uint64_t userA, std::uint64_t userB) const;

        [[nodiscard]] std::size_t ColliderCount() const { return m_colliders.size(); }

        // Detection only, main-thread only - same rules as Step(). Independent
        // of Step() (they scan m_colliders directly). Linear scan; a broadphase
        // would filter candidates by the ray's AABB first. 3D counterpart:
        // CollisionWorld3D. See docs/collider-design.md "레이캐스트".
        [[nodiscard]] std::optional<RayHit2D> RaycastClosest(const Ray2D& ray) const;   // nearest hit
        [[nodiscard]] bool RaycastAny(const Ray2D& ray) const;                          // just "does it hit" (LOS)
        void RaycastAll(const Ray2D& ray, std::vector<RayHit2D>& outHits) const;        // every hit, distance-sorted

    private:
        std::vector<Collider2D> m_colliders;   // index-aligned with m_ids
        std::vector<ColliderId> m_ids;
        std::unordered_map<ColliderId, std::size_t> m_indexOf;
        std::vector<Contact> m_contacts;
        ColliderId m_nextId{ 1 };
    };
}
