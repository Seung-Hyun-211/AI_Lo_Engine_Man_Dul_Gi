#include "game/MobField.h"

#include <cmath>

namespace engine::game
{
    MobField::MobField(std::size_t capacity)
    {
        m_posX.assign(capacity, 0.0f);
        m_posY.assign(capacity, 0.0f);
        m_velX.assign(capacity, 0.0f);
        m_velY.assign(capacity, 0.0f);
        m_health.assign(capacity, 0.0f);
        m_radius.assign(capacity, 0.0f);
        m_slotActive.assign(capacity, 0u);
        m_activePos.assign(capacity, 0u);
        m_active.reserve(capacity);
        m_free.reserve(capacity);
        // Low indices handed out first (same convention as core::ObjectPool::Init).
        for (std::size_t i = capacity; i-- > 0; )
            m_free.push_back(static_cast<std::uint32_t>(i));
    }

    bool MobField::Spawn(math::Vec2 pos, float health, float radius)
    {
        if (m_free.empty()) return false;

        const std::uint32_t index = m_free.back();
        m_free.pop_back();

        m_posX[index] = pos.x;
        m_posY[index] = pos.y;
        m_velX[index] = 0.0f;
        m_velY[index] = 0.0f;
        m_health[index] = health;
        m_radius[index] = radius;

        m_slotActive[index] = 1u;
        m_activePos[index] = static_cast<std::uint32_t>(m_active.size());
        m_active.push_back(index);
        return true;
    }

    void MobField::Kill(std::uint32_t index)
    {
        if (index >= m_slotActive.size() || m_slotActive[index] == 0u) return;

        m_slotActive[index] = 0u;
        const std::uint32_t pos = m_activePos[index];
        const std::uint32_t moved = m_active.back();
        m_active[pos] = moved;
        m_activePos[moved] = pos;
        m_active.pop_back();

        m_free.push_back(index);
    }

    void MobField::Step(core::JobSystem& jobs, math::Vec2 target, float speed, float fixedDelta)
    {
        if (m_active.empty()) return;

        float* posX = m_posX.data();
        float* posY = m_posY.data();
        float* velX = m_velX.data();
        float* velY = m_velY.data();
        const std::uint32_t* active = m_active.data();

        constexpr std::size_t kGrainSize = 256;
        jobs.ParallelFor(0, m_active.size(), kGrainSize,
            [=](std::size_t begin, std::size_t end)
            {
                for (std::size_t i = begin; i < end; ++i)
                {
                    const std::uint32_t idx = active[i];
                    const float dx = target.x - posX[idx];
                    const float dy = target.y - posY[idx];
                    const float lenSq = dx * dx + dy * dy;
                    if (lenSq > 1e-6f)
                    {
                        const float invLen = 1.0f / std::sqrt(lenSq);
                        velX[idx] = dx * invLen * speed;
                        velY[idx] = dy * invLen * speed;
                    }
                    posX[idx] += velX[idx] * fixedDelta;
                    posY[idx] += velY[idx] * fixedDelta;
                }
            }).Wait();
    }

    std::uint32_t MobField::DamageInRadius(math::Vec2 center, float radius, float amount)
    {
        const float r2 = radius * radius;
        m_deadScratch.clear();
        for (const std::uint32_t idx : m_active)
        {
            const float dx = m_posX[idx] - center.x;
            const float dy = m_posY[idx] - center.y;
            if (dx * dx + dy * dy > r2) continue;

            m_health[idx] -= amount;
            if (m_health[idx] <= 0.0f) m_deadScratch.push_back(idx);
        }
        // Killed after the scan, not during it - Kill() swap-removes out of
        // m_active, which would skip or duplicate entries if it mutated the
        // list the loop above is still iterating.
        for (const std::uint32_t idx : m_deadScratch) Kill(idx);
        return static_cast<std::uint32_t>(m_deadScratch.size());
    }

    void MobField::Clear()
    {
        m_active.clear();
        m_free.clear();
        for (std::size_t i = m_posX.size(); i-- > 0; )
        {
            m_slotActive[i] = 0u;
            m_free.push_back(static_cast<std::uint32_t>(i));
        }
    }
}
