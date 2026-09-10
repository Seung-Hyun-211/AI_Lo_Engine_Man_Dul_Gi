#include "physics/p3d/CollisionWorld3D.h"

#if defined(ENGINE_WITH_3D)

#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <utility>

namespace engine::physics
{
    namespace
    {
        // Grid cell size is clamped to this range around 2 * mean collider
        // extent; total cell count is capped and the size grows to fit.
        constexpr float kMinCellSize = 0.25f;
        constexpr float kMaxCellSize = 64.0f;
        constexpr std::size_t kMaxCells = 262144;   // ~6 MB of empty vectors worst case
        // Debug: cross-check the grid result against brute force every Nth Step,
        // but only while the collider count is small enough that O(n^2) is cheap
        // (a scale test with tens of thousands would otherwise stall).
        constexpr std::uint32_t kBruteForceCheckEvery = 16;
        constexpr std::size_t   kBruteForceCheckMaxColliders = 4096;

        [[nodiscard]] math::Vec3 ColliderExtent(const Collider3D& c)
        {
            return c.shape == Collider3D::Shape::Box
                ? c.halfExtents
                : math::Vec3{ c.radius, c.radius, c.radius };
        }

        [[nodiscard]] Contact MakeContact(ColliderId ia, ColliderId ib,
                                          std::uint64_t ua, std::uint64_t ub)
        {
            Contact contact{ ia, ib, ua, ub };
            if (contact.a > contact.b)
            {
                std::swap(contact.a, contact.b);
                std::swap(contact.userA, contact.userB);
            }
            return contact;
        }

        void SortContacts(std::vector<Contact>& contacts)
        {
            std::sort(contacts.begin(), contacts.end(),
                [](const Contact& l, const Contact& r)
                {
                    return l.a != r.a ? l.a < r.a : l.b < r.b;
                });
        }
    }

    ColliderId CollisionWorld3D::Add(const Collider3D& collider)
    {
        const ColliderId id = m_nextId++;
        m_indexOf.emplace(id, m_colliders.size());
        m_colliders.push_back(collider);
        m_ids.push_back(id);
        m_gridDirty = true;
        return id;
    }

    void CollisionWorld3D::Update(ColliderId id, const Collider3D& collider)
    {
        const auto it = m_indexOf.find(id);
        if (it != m_indexOf.end()) { m_colliders[it->second] = collider; m_gridDirty = true; }
    }

    void CollisionWorld3D::Remove(ColliderId id)
    {
        const auto it = m_indexOf.find(id);
        if (it == m_indexOf.end()) return;

        const std::size_t index = it->second;
        const std::size_t last = m_colliders.size() - 1;
        if (index != last)
        {
            m_colliders[index] = m_colliders[last];
            m_ids[index] = m_ids[last];
            m_indexOf[m_ids[index]] = index;
        }
        m_colliders.pop_back();
        m_ids.pop_back();
        m_indexOf.erase(it);
        m_gridDirty = true;
    }

    void CollisionWorld3D::Clear()
    {
        m_colliders.clear();
        m_ids.clear();
        m_indexOf.clear();
        m_contacts.clear();
        m_nextId = 1;
        m_gridDirty = true;
    }

    void CollisionWorld3D::RebuildGrid() const
    {
        m_gridDirty = false;
        m_gridNx = m_gridNy = m_gridNz = 0;
        for (auto& cell : m_cells) cell.clear();   // keep inner capacity

        const std::size_t count = m_colliders.size();
        if (count == 0) { m_cells.clear(); return; }

        math::Vec3 lo{ +1e30f, +1e30f, +1e30f };
        math::Vec3 hi{ -1e30f, -1e30f, -1e30f };
        float extentSum = 0.0f;
        for (const Collider3D& c : m_colliders)
        {
            const math::Vec3 e = ColliderExtent(c);
            lo.x = std::min(lo.x, c.center.x - e.x);  hi.x = std::max(hi.x, c.center.x + e.x);
            lo.y = std::min(lo.y, c.center.y - e.y);  hi.y = std::max(hi.y, c.center.y + e.y);
            lo.z = std::min(lo.z, c.center.z - e.z);  hi.z = std::max(hi.z, c.center.z + e.z);
            extentSum += std::max({ e.x, e.y, e.z });
        }

        float cell = math::Clamp((extentSum / static_cast<float>(count)) * 2.0f,
                                 kMinCellSize, kMaxCellSize);
        const auto dims = [&](float cs)
        {
            return std::array<long long, 3>{
                std::max(1LL, static_cast<long long>(std::ceil((hi.x - lo.x) / cs))),
                std::max(1LL, static_cast<long long>(std::ceil((hi.y - lo.y) / cs))),
                std::max(1LL, static_cast<long long>(std::ceil((hi.z - lo.z) / cs))) };
        };
        std::array<long long, 3> d = dims(cell);
        while (d[0] * d[1] * d[2] > static_cast<long long>(kMaxCells) && cell < 1e6f)
        {
            cell *= 2.0f;
            d = dims(cell);
        }

        m_gridOrigin = lo;
        m_gridCell = cell;
        m_gridNx = static_cast<int>(d[0]);
        m_gridNy = static_cast<int>(d[1]);
        m_gridNz = static_cast<int>(d[2]);

        const std::size_t n = static_cast<std::size_t>(m_gridNx)
                            * static_cast<std::size_t>(m_gridNy)
                            * static_cast<std::size_t>(m_gridNz);
        if (m_cells.size() != n) m_cells.resize(n);
        // (inner vectors already cleared above; resize only touches the tail)

        for (std::size_t i = 0; i < count; ++i)
        {
            int x0, y0, z0, x1, y1, z1;
            CellRange(m_colliders[i], x0, y0, z0, x1, y1, z1);
            for (int z = z0; z <= z1; ++z)
                for (int y = y0; y <= y1; ++y)
                    for (int x = x0; x <= x1; ++x)
                        m_cells[CellIndex(x, y, z)].push_back(static_cast<std::uint32_t>(i));
        }
    }

    void CollisionWorld3D::CellRange(const Collider3D& c, int& x0, int& y0, int& z0,
                                     int& x1, int& y1, int& z1) const
    {
        const math::Vec3 e = ColliderExtent(c);
        const float inv = 1.0f / m_gridCell;
        const auto clampAxis = [](int v, int n) { return v < 0 ? 0 : (v >= n ? n - 1 : v); };
        x0 = clampAxis(static_cast<int>(std::floor((c.center.x - e.x - m_gridOrigin.x) * inv)), m_gridNx);
        x1 = clampAxis(static_cast<int>(std::floor((c.center.x + e.x - m_gridOrigin.x) * inv)), m_gridNx);
        y0 = clampAxis(static_cast<int>(std::floor((c.center.y - e.y - m_gridOrigin.y) * inv)), m_gridNy);
        y1 = clampAxis(static_cast<int>(std::floor((c.center.y + e.y - m_gridOrigin.y) * inv)), m_gridNy);
        z0 = clampAxis(static_cast<int>(std::floor((c.center.z - e.z - m_gridOrigin.z) * inv)), m_gridNz);
        z1 = clampAxis(static_cast<int>(std::floor((c.center.z + e.z - m_gridOrigin.z) * inv)), m_gridNz);
    }

    void CollisionWorld3D::Step()
    {
        m_contacts.clear();
        const std::size_t count = m_colliders.size();
        if (count < 2) return;

        if (m_gridDirty) RebuildGrid();
        if (m_pairStamp.size() < count) m_pairStamp.resize(count, 0);

        for (std::size_t i = 0; i < count; ++i)
        {
            const std::uint64_t stamp = ++m_stamp;
            int x0, y0, z0, x1, y1, z1;
            CellRange(m_colliders[i], x0, y0, z0, x1, y1, z1);
            const Collider3D& ca = m_colliders[i];

            for (int z = z0; z <= z1; ++z)
                for (int y = y0; y <= y1; ++y)
                    for (int x = x0; x <= x1; ++x)
                    {
                        for (const std::uint32_t j : m_cells[CellIndex(x, y, z)])
                        {
                            if (j <= i) continue;                       // ordered pairs only
                            if (m_pairStamp[j] == stamp) continue;      // pair shares another cell
                            m_pairStamp[j] = stamp;

                            const Collider3D& cb = m_colliders[j];
                            if (!LayersInteract(ca.layer, ca.mask, cb.layer, cb.mask)) continue;
                            if (!Overlaps(ca, cb)) continue;
                            m_contacts.push_back(MakeContact(m_ids[i], m_ids[j], ca.user, cb.user));
                        }
                    }
        }
        SortContacts(m_contacts);

#if !defined(NDEBUG)
        if (count <= kBruteForceCheckMaxColliders
            && (m_stepCounter++ % kBruteForceCheckEvery) == 0)
            assert(ContactsMatchBruteForce() && "grid broadphase disagrees with brute force");
#endif
    }

#if !defined(NDEBUG)
    bool CollisionWorld3D::ContactsMatchBruteForce() const
    {
        std::vector<Contact> bf;
        const std::size_t count = m_colliders.size();
        for (std::size_t i = 0; i < count; ++i)
            for (std::size_t j = i + 1; j < count; ++j)
            {
                const Collider3D& ca = m_colliders[i];
                const Collider3D& cb = m_colliders[j];
                if (!LayersInteract(ca.layer, ca.mask, cb.layer, cb.mask)) continue;
                if (!Overlaps(ca, cb)) continue;
                bf.push_back(MakeContact(m_ids[i], m_ids[j], ca.user, cb.user));
            }
        SortContacts(bf);
        if (bf.size() != m_contacts.size()) return false;
        for (std::size_t k = 0; k < bf.size(); ++k)
            if (bf[k].a != m_contacts[k].a || bf[k].b != m_contacts[k].b) return false;
        return true;
    }
#endif

    bool CollisionWorld3D::AreTouching(std::uint64_t userA, std::uint64_t userB) const
    {
        return std::any_of(m_contacts.begin(), m_contacts.end(),
            [userA, userB](const Contact& c)
            {
                return (c.userA == userA && c.userB == userB)
                    || (c.userA == userB && c.userB == userA);
            });
    }

    std::optional<RayHit3D> CollisionWorld3D::RaycastClosest(const Ray3D& ray) const
    {
        std::optional<RayHit3D> best;
        for (std::size_t i = 0; i < m_colliders.size(); ++i)
        {
            if (m_ids[i] == ray.ignoreId) continue;
            if ((ray.mask & m_colliders[i].layer) == 0u) continue;

            float distance = 0.0f;
            math::Vec3 normal{};
            const float bound = best ? best->distance : ray.maxDistance;
            if (!RaycastCollider(m_colliders[i], ray.origin, ray.dir, bound, distance, normal)) continue;

            RayHit3D hit;
            hit.id = m_ids[i];
            hit.user = m_colliders[i].user;
            hit.distance = distance;
            hit.point = ray.origin + ray.dir * distance;
            hit.normal = normal;
            best = hit;   // bound above guarantees this is the closest so far
        }
        return best;
    }

    bool CollisionWorld3D::RaycastAny(const Ray3D& ray) const
    {
        for (std::size_t i = 0; i < m_colliders.size(); ++i)
        {
            if (m_ids[i] == ray.ignoreId) continue;
            if ((ray.mask & m_colliders[i].layer) == 0u) continue;

            float distance = 0.0f;
            math::Vec3 normal{};
            if (RaycastCollider(m_colliders[i], ray.origin, ray.dir, ray.maxDistance, distance, normal))
                return true;
        }
        return false;
    }

    void CollisionWorld3D::RaycastAll(const Ray3D& ray, std::vector<RayHit3D>& outHits) const
    {
        outHits.clear();
        for (std::size_t i = 0; i < m_colliders.size(); ++i)
        {
            if (m_ids[i] == ray.ignoreId) continue;
            if ((ray.mask & m_colliders[i].layer) == 0u) continue;

            float distance = 0.0f;
            math::Vec3 normal{};
            if (!RaycastCollider(m_colliders[i], ray.origin, ray.dir, ray.maxDistance, distance, normal)) continue;

            RayHit3D hit;
            hit.id = m_ids[i];
            hit.user = m_colliders[i].user;
            hit.distance = distance;
            hit.point = ray.origin + ray.dir * distance;
            hit.normal = normal;
            outHits.push_back(hit);
        }
        std::sort(outHits.begin(), outHits.end(),
            [](const RayHit3D& l, const RayHit3D& r) { return l.distance < r.distance; });
    }
}

#endif  // ENGINE_WITH_3D
