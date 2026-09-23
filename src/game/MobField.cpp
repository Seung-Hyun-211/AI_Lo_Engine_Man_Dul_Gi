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
        m_state.assign(capacity, static_cast<std::uint8_t>(MobState::Seek));
        m_stateTimer.assign(capacity, 0.0f);
        m_type.assign(capacity, 0u);
        m_attackTimer.assign(capacity, 0.0f);
        m_slotActive.assign(capacity, 0u);
        m_generation.assign(capacity, 0u);
        m_activePos.assign(capacity, 0u);
        m_active.reserve(capacity);
        m_free.reserve(capacity);
        // Low indices handed out first (same convention as core::ObjectPool::Init).
        for (std::size_t i = capacity; i-- > 0; )
            m_free.push_back(static_cast<std::uint32_t>(i));
    }

    bool MobField::Spawn(math::Vec2 pos, float health, float radius, std::uint8_t type, float attackDelay)
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
        m_state[index] = static_cast<std::uint8_t>(MobState::Seek);
        m_stateTimer[index] = 0.0f;
        m_type[index] = type;
        m_attackTimer[index] = attackDelay;

        m_slotActive[index] = 1u;
        m_activePos[index] = static_cast<std::uint32_t>(m_active.size());
        m_active.push_back(index);
        return true;
    }

    void MobField::Kill(std::uint32_t index)
    {
        if (index >= m_slotActive.size() || m_slotActive[index] == 0u) return;

        m_slotActive[index] = 0u;
        ++m_generation[index];   // any MobRef to this mob is stale from now on
        const std::uint8_t type = m_type[index];
        if (type >= m_killTally.size()) m_killTally.resize(static_cast<std::size_t>(type) + 1, 0u);
        ++m_killTally[type];
        const std::uint32_t pos = m_activePos[index];
        const std::uint32_t moved = m_active.back();
        m_active[pos] = moved;
        m_activePos[moved] = pos;
        m_active.pop_back();

        m_free.push_back(index);
    }

    void MobField::Step(core::JobSystem& jobs, math::Vec2 target, std::span<const MobMotion> motion, float fixedDelta)
    {
        if (m_active.empty() || motion.empty()) return;

        float* posX = m_posX.data();
        float* posY = m_posY.data();
        float* velX = m_velX.data();
        float* velY = m_velY.data();
        std::uint8_t* state = m_state.data();
        float* stateTimer = m_stateTimer.data();
        const std::uint8_t* type = m_type.data();
        const std::uint32_t* active = m_active.data();
        const MobMotion* kinds = motion.data();
        const std::size_t kindCount = motion.size();

        constexpr std::size_t kGrainSize = 256;
        jobs.ParallelFor(0, m_active.size(), kGrainSize,
            [=](std::size_t begin, std::size_t end)
            {
                for (std::size_t i = begin; i < end; ++i)
                {
                    const std::uint32_t idx = active[i];

                    // Windup: frozen until LaunchCharge. Charge: keep the
                    // velocity LaunchCharge fixed (no re-aim), fall back to
                    // Seek when the timer runs out. Each job only writes its
                    // own slots' entries (rule 6).
                    if (state[idx] == static_cast<std::uint8_t>(MobState::Windup)) continue;
                    if (state[idx] == static_cast<std::uint8_t>(MobState::Charge))
                    {
                        posX[idx] += velX[idx] * fixedDelta;
                        posY[idx] += velY[idx] * fixedDelta;
                        stateTimer[idx] -= fixedDelta;
                        if (stateTimer[idx] <= 0.0f) state[idx] = static_cast<std::uint8_t>(MobState::Seek);
                        continue;
                    }

                    // Seek: chase, or hold once inside the kind's keepDistance
                    // (ranged/caster mobs stop at their firing range).
                    const MobMotion& kind = kinds[type[idx] < kindCount ? type[idx] : 0];
                    const float dx = target.x - posX[idx];
                    const float dy = target.y - posY[idx];
                    const float lenSq = dx * dx + dy * dy;
                    if (kind.keepDistance > 0.0f && lenSq <= kind.keepDistance * kind.keepDistance)
                    {
                        velX[idx] = 0.0f;
                        velY[idx] = 0.0f;
                        continue;
                    }
                    if (lenSq > 1e-6f)
                    {
                        const float invLen = 1.0f / std::sqrt(lenSq);
                        velX[idx] = dx * invLen * kind.speed;
                        velY[idx] = dy * invLen * kind.speed;
                    }
                    posX[idx] += velX[idx] * fixedDelta;
                    posY[idx] += velY[idx] * fixedDelta;
                }
            }).Wait();
    }

    void MobField::CollectCasts(float fixedDelta, math::Vec2 target, std::span<const MobAttackTiming> timing,
                                std::vector<MobCast>& out)
    {
        out.clear();
        if (timing.empty()) return;
        for (const std::uint32_t idx : m_active)
        {
            const MobAttackTiming& kind = timing[m_type[idx] < timing.size() ? m_type[idx] : 0];
            if (kind.cooldown <= 0.0f) continue;
            // A frozen/charging mob (the temporary charge pattern) doesn't cast.
            if (m_state[idx] != static_cast<std::uint8_t>(MobState::Seek)) continue;
            if (m_attackTimer[idx] > 0.0f) m_attackTimer[idx] -= fixedDelta;
            if (m_attackTimer[idx] > 0.0f) continue;
            const float dx = target.x - m_posX[idx];
            const float dy = target.y - m_posY[idx];
            if (dx * dx + dy * dy > kind.range * kind.range) continue;   // due, waiting for the target to come close
            m_attackTimer[idx] = kind.cooldown;
            out.push_back({ { m_posX[idx], m_posY[idx] }, m_type[idx] });
        }
    }

    float MobField::StrongestTouching(const math::Rect& box, std::span<const float> contactDamage) const
    {
        float strongest = 0.0f;
        if (contactDamage.empty()) return strongest;
        for (const std::uint32_t idx : m_active)
        {
            const float damage = contactDamage[m_type[idx] < contactDamage.size() ? m_type[idx] : 0];
            if (damage <= strongest) continue;
            if (PointRectDistanceSq({ m_posX[idx], m_posY[idx] }, box) <= m_radius[idx] * m_radius[idx]) strongest = damage;
        }
        return strongest;
    }

    std::uint32_t MobField::DamageInShape(const HitShape& shape, float amount)
    {
        m_deadScratch.clear();
        for (const std::uint32_t idx : m_active)
        {
            if (!ShapeContains(shape, { m_posX[idx], m_posY[idx] }, m_radius[idx])) continue;
            m_health[idx] -= amount;
            if (m_health[idx] <= 0.0f) m_deadScratch.push_back(idx);
        }
        // Killed after the scan, not during it - Kill() swap-removes out of
        // m_active, which would skip or duplicate entries if it mutated the
        // list the loop above is still iterating.
        for (const std::uint32_t idx : m_deadScratch) Kill(idx);
        return static_cast<std::uint32_t>(m_deadScratch.size());
    }

    void MobField::Overlapping(const HitShape& shape, std::vector<MobHit>& out) const
    {
        out.clear();
        for (const std::uint32_t idx : m_active)
        {
            const math::Vec2 pos{ m_posX[idx], m_posY[idx] };
            if (ShapeContains(shape, pos, m_radius[idx])) out.push_back({ { idx, m_generation[idx] }, pos });
        }
    }

    bool MobField::Damage(MobRef ref, float amount)
    {
        if (ref.slot >= m_slotActive.size() || m_slotActive[ref.slot] == 0u || m_generation[ref.slot] != ref.generation)
            return false;
        m_health[ref.slot] -= amount;
        if (m_health[ref.slot] > 0.0f) return false;
        Kill(ref.slot);
        return true;
    }

    std::uint32_t MobField::DamageNearest(math::Vec2 center, float range, float amount, std::uint32_t count,
                                          math::Vec2* hitOut, std::uint32_t& hitCount)
    {
        constexpr std::uint32_t kMaxTargets = 8;
        if (count > kMaxTargets) count = kMaxTargets;
        hitCount = 0;
        if (count == 0) return 0;

        struct Candidate { float distSq; std::uint32_t idx; };
        Candidate best[kMaxTargets];
        std::uint32_t found = 0;
        const float rangeSq = range * range;

        for (const std::uint32_t idx : m_active)
        {
            const float dx = m_posX[idx] - center.x;
            const float dy = m_posY[idx] - center.y;
            const float distSq = dx * dx + dy * dy;
            if (distSq > rangeSq) continue;

            // Keep `best` sorted ascending by distance, at most `count` long.
            std::uint32_t slot;
            if (found < count) slot = found++;
            else if (distSq < best[count - 1].distSq) slot = count - 1;
            else continue;
            best[slot] = { distSq, idx };
            while (slot > 0 && best[slot].distSq < best[slot - 1].distSq)
            {
                const Candidate tmp = best[slot];
                best[slot] = best[slot - 1];
                best[slot - 1] = tmp;
                --slot;
            }
        }

        m_deadScratch.clear();
        for (std::uint32_t i = 0; i < found; ++i)
        {
            const std::uint32_t idx = best[i].idx;
            hitOut[i] = { m_posX[idx], m_posY[idx] };
            m_health[idx] -= amount;
            if (m_health[idx] <= 0.0f) m_deadScratch.push_back(idx);
        }
        hitCount = found;
        for (const std::uint32_t idx : m_deadScratch) Kill(idx);   // after the scan, as in DamageInShape
        return static_cast<std::uint32_t>(m_deadScratch.size());
    }

    bool MobField::ClosestWithin(math::Vec2 center, float radius, math::Vec2& posOut) const
    {
        float bestDistSq = radius * radius;
        bool found = false;
        for (const std::uint32_t idx : m_active)
        {
            const float dx = m_posX[idx] - center.x;
            const float dy = m_posY[idx] - center.y;
            const float distSq = dx * dx + dy * dy;
            if (distSq > bestDistSq) continue;
            bestDistSq = distSq;
            posOut = { m_posX[idx], m_posY[idx] };
            found = true;
        }
        return found;
    }

    std::uint32_t MobField::BeginWindup(math::Vec2 center, const math::Rect& exclude,
                                        float minDistance, float fraction, std::uint32_t salt)
    {
        const float minDistSq = minDistance * minDistance;
        const std::uint32_t threshold = static_cast<std::uint32_t>(fraction * 65535.0f);
        std::uint32_t picked = 0;
        for (const std::uint32_t idx : m_active)
        {
            if (m_state[idx] != static_cast<std::uint8_t>(MobState::Seek)) continue;
            const math::Vec2 pos{ m_posX[idx], m_posY[idx] };
            if (exclude.Contains(pos)) continue;
            const float dx = pos.x - center.x;
            const float dy = pos.y - center.y;
            if (dx * dx + dy * dy < minDistSq) continue;

            // Salted multiplicative hash of the slot index -> 0..65535.
            const std::uint32_t h = ((idx ^ (salt * 0x9E3779B9u)) * 2654435761u) >> 16;
            if (h >= threshold) continue;

            m_state[idx] = static_cast<std::uint8_t>(MobState::Windup);
            ++picked;
        }
        return picked;
    }

    void MobField::LaunchCharge(math::Vec2 target, float speed, float duration)
    {
        for (const std::uint32_t idx : m_active)
        {
            if (m_state[idx] != static_cast<std::uint8_t>(MobState::Windup)) continue;
            const math::Vec2 dir = math::Normalized({ target.x - m_posX[idx], target.y - m_posY[idx] });
            m_velX[idx] = dir.x * speed;
            m_velY[idx] = dir.y * speed;
            m_stateTimer[idx] = duration;
            m_state[idx] = static_cast<std::uint8_t>(MobState::Charge);
        }
    }

    void MobField::Clear()
    {
        ClearKillTally();   // a reset is not a kill
        m_active.clear();
        m_free.clear();
        for (std::size_t i = m_posX.size(); i-- > 0; )
        {
            if (m_slotActive[i] != 0u) ++m_generation[i];
            m_slotActive[i] = 0u;
            m_free.push_back(static_cast<std::uint32_t>(i));
        }
    }
}
