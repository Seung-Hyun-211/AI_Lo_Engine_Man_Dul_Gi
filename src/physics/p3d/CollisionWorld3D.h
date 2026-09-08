#pragma once

#include "core/NonCopyable.h"
#include "physics/p3d/Collider3D.h"

#include <cstddef>
#include <cstdint>
#include <unordered_map>
#include <vector>

namespace engine::physics
{
    // 3D counterpart of CollisionWorld2D. Same contract, same brute-force
    // broadphase. Built only when ENGINE_WITH_3D is defined.
    class CollisionWorld3D final : private core::NonCopyable
    {
    public:
        ColliderId Add(const Collider3D& collider);
        void Update(ColliderId id, const Collider3D& collider);
        void Remove(ColliderId id);
        void Clear();
        void Step();

        [[nodiscard]] const std::vector<Contact>& Contacts() const { return m_contacts; }
        [[nodiscard]] bool AreTouching(std::uint64_t userA, std::uint64_t userB) const;
        [[nodiscard]] std::size_t ColliderCount() const { return m_colliders.size(); }

    private:
        std::vector<Collider3D> m_colliders;
        std::vector<ColliderId> m_ids;
        std::unordered_map<ColliderId, std::size_t> m_indexOf;
        std::vector<Contact> m_contacts;
        ColliderId m_nextId{ 1 };
    };
}
