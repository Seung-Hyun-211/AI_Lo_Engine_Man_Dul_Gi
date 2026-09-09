#include "core/EntityRegistry.h"

namespace engine::core
{
    EntityId EntityRegistry::Create()
    {
        if (!m_freeList.empty())
        {
            const std::uint32_t index = m_freeList.back();
            m_freeList.pop_back();
            m_slots[index].alive = true;
            return EntityId{ index, m_slots[index].generation };
        }

        const std::uint32_t index = static_cast<std::uint32_t>(m_slots.size());
        m_slots.push_back(Slot{ 0, true });
        return EntityId{ index, 0 };
    }

    void EntityRegistry::Destroy(EntityId id)
    {
        if (!IsAlive(id)) return;
        m_slots[id.index].alive = false;
        m_pendingDestroy.push_back(id.index);
    }

    bool EntityRegistry::IsAlive(EntityId id) const
    {
        return id.IsValid() && id.index < m_slots.size()
            && m_slots[id.index].generation == id.generation
            && m_slots[id.index].alive;
    }

    void EntityRegistry::Flush()
    {
        for (const std::uint32_t index : m_pendingDestroy)
        {
            ++m_slots[index].generation;
            m_freeList.push_back(index);
        }
        m_pendingDestroy.clear();
    }
}
