#pragma once

#include "core/NonCopyable.h"
#include "physics/p2d/Collider2D.h"

#include <cstddef>
#include <cstdint>
#include <unordered_map>
#include <vector>

namespace engine::physics
{
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

    private:
        std::vector<Collider2D> m_colliders;   // index-aligned with m_ids
        std::vector<ColliderId> m_ids;
        std::unordered_map<ColliderId, std::size_t> m_indexOf;
        std::vector<Contact> m_contacts;
        ColliderId m_nextId{ 1 };
    };
}
