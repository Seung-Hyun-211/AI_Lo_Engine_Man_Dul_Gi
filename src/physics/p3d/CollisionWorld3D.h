#pragma once

#include "core/NonCopyable.h"
#include "physics/p3d/Collider3D.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <unordered_map>
#include <vector>

namespace engine::physics
{
    // A directed ray for the raycast queries. `dir` must be unit length.
    // `maxDistance` bounds the search - the "objects within a certain range"
    // query is just this with a finite value. `mask` keeps only colliders whose
    // layer is in it; `ignoreId` skips one collider (typically the caster's own).
    struct Ray3D
    {
        math::Vec3     origin{};
        math::Vec3     dir{ 0.0f, 0.0f, 1.0f };
        float          maxDistance{ 3.402823e38f };
        CollisionLayer mask{ kAllLayers };
        ColliderId     ignoreId{ kInvalidCollider };
    };

    struct RayHit3D
    {
        ColliderId    id{ kInvalidCollider };
        std::uint64_t user{};        // the collider's `user` (usually an entity id)
        float         distance{};    // metres from origin
        math::Vec3    point{};
        math::Vec3    normal{};
    };

    // 3D counterpart of CollisionWorld2D. Same contract. Step() uses a uniform
    // grid broadphase (rebuilt from the current colliders); the raycasts are
    // still a linear scan (grid acceleration is roadmap D3b). Built only when
    // ENGINE_WITH_3D is defined.
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

        // Detection only, main/sim thread only - same rules as Step(). Linear
        // scan of every collider (a broadphase would filter candidates first).
        // See docs/roadmap.md §2.2, docs/collider-design.md.
        [[nodiscard]] std::optional<RayHit3D> RaycastClosest(const Ray3D& ray) const;   // nearest hit
        [[nodiscard]] bool RaycastAny(const Ray3D& ray) const;                          // just "does it hit" (LOS)
        void RaycastAll(const Ray3D& ray, std::vector<RayHit3D>& outHits) const;        // every hit, distance-sorted

    private:
        // Uniform-grid broadphase (implementation detail of Step()). Rebuilt
        // lazily when a mutation set m_gridDirty. Cell size adapts to the mean
        // collider extent; total cell count is capped (cell size grows to fit).
        void RebuildGrid() const;
        // Half-open cell index range [x0,x1] x [y0,y1] x [z0,z1] the collider's
        // world AABB covers, clamped to the grid.
        void CellRange(const Collider3D& c, int& x0, int& y0, int& z0,
                       int& x1, int& y1, int& z1) const;
        [[nodiscard]] std::size_t CellIndex(int x, int y, int z) const
        {
            return (static_cast<std::size_t>(z) * static_cast<std::size_t>(m_gridNy)
                    + static_cast<std::size_t>(y)) * static_cast<std::size_t>(m_gridNx)
                    + static_cast<std::size_t>(x);
        }
#if !defined(NDEBUG)
        // Debug-only: the grid broadphase must produce exactly the brute-force
        // contact set. Called under assert() from Step().
        [[nodiscard]] bool ContactsMatchBruteForce() const;
#endif

        std::vector<Collider3D> m_colliders;
        std::vector<ColliderId> m_ids;
        std::unordered_map<ColliderId, std::size_t> m_indexOf;
        std::vector<Contact> m_contacts;
        ColliderId m_nextId{ 1 };

        mutable bool m_gridDirty{ true };
        mutable math::Vec3 m_gridOrigin{};
        mutable float m_gridCell{ 1.0f };
        mutable int m_gridNx{ 0 }, m_gridNy{ 0 }, m_gridNz{ 0 };
        mutable std::vector<std::vector<std::uint32_t>> m_cells;   // cell -> collider indices
        mutable std::vector<std::uint64_t> m_pairStamp;            // per-collider "seen this i" marker
        mutable std::uint64_t m_stamp{ 0 };
        mutable std::uint32_t m_stepCounter{ 0 };
    };
}
