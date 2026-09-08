#include "physics/p2d/CollisionWorld2D.h"

#include <algorithm>
#include <utility>

namespace engine::physics
{
    ColliderId CollisionWorld2D::Add(const Collider2D& collider)
    {
        const ColliderId id = m_nextId++;
        m_indexOf.emplace(id, m_colliders.size());
        m_colliders.push_back(collider);
        m_ids.push_back(id);
        return id;
    }

    void CollisionWorld2D::Update(ColliderId id, const Collider2D& collider)
    {
        const auto it = m_indexOf.find(id);
        if (it != m_indexOf.end()) m_colliders[it->second] = collider;
    }

    void CollisionWorld2D::Remove(ColliderId id)
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
    }

    void CollisionWorld2D::Clear()
    {
        m_colliders.clear();
        m_ids.clear();
        m_indexOf.clear();
        m_contacts.clear();
        m_nextId = 1;
    }

    void CollisionWorld2D::Step()
    {
        m_contacts.clear();
        const std::size_t count = m_colliders.size();
        for (std::size_t i = 0; i < count; ++i)
        {
            for (std::size_t j = i + 1; j < count; ++j)
            {
                const Collider2D& ca = m_colliders[i];
                const Collider2D& cb = m_colliders[j];
                if (!LayersInteract(ca.layer, ca.mask, cb.layer, cb.mask)) continue;
                if (!Overlaps(ca, cb)) continue;

                Contact contact{ m_ids[i], m_ids[j], ca.user, cb.user };
                if (contact.a > contact.b)
                {
                    std::swap(contact.a, contact.b);
                    std::swap(contact.userA, contact.userB);
                }
                m_contacts.push_back(contact);
            }
        }
        std::sort(m_contacts.begin(), m_contacts.end(),
            [](const Contact& l, const Contact& r)
            {
                return l.a != r.a ? l.a < r.a : l.b < r.b;
            });
    }

    bool CollisionWorld2D::AreTouching(std::uint64_t userA, std::uint64_t userB) const
    {
        return std::any_of(m_contacts.begin(), m_contacts.end(),
            [userA, userB](const Contact& c)
            {
                return (c.userA == userA && c.userB == userB)
                    || (c.userA == userB && c.userB == userA);
            });
    }
}
