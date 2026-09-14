#include "game/vfx/ParticleSystem.h"

#include <algorithm>
#include <cmath>

namespace engine::game::vfx
{
    namespace
    {
        constexpr float kPi = 3.14159265358979323846f;
        constexpr float kGravity = 9.8f;

        // Mushroom cap: once an ExplosionSmokeStem particle has risen this far
        // above its spawn point, its rise stalls and it spreads sideways
        // (docs/particle-system-research.md §7.2).
        constexpr float kCapHeight = 3.0f;
        constexpr float kCapOutSpeed = 1.5f;
    }

    float ParticleSystem::NextRandom01()
    {
        // Same fmod-based, no-<random> convention as Simulation::SeedAgent/
        // SpawnGibs - deterministic, good enough for scatter that is purely
        // visual (not gameplay-affecting).
        m_seed = std::fmod(m_seed * 16807.0f + 1.0f, 2147483647.0f);
        return std::fmod(m_seed, 1000.0f) / 1000.0f;
    }

    void ParticleSystem::SpawnBurst(const ParticleEffectDef& def, math::Vec3 pos, math::Vec3 dir)
    {
        dir = math::Normalized(dir);
        if (math::Length(dir) < 1e-4f) dir = { 0.0f, 1.0f, 0.0f };

        // Orthonormal basis around dir, for scattering within the spread cone.
        math::Vec3 up = std::fabs(dir.y) > 0.99f ? math::Vec3{ 1.0f, 0.0f, 0.0f } : math::Vec3{ 0.0f, 1.0f, 0.0f };
        const math::Vec3 tangent = math::Normalized(math::Cross(up, dir));
        const math::Vec3 bitangent = math::Cross(dir, tangent);

        const float spreadRad = def.spreadDeg * (kPi / 180.0f);
        const int span = std::max(0, def.burstMax - def.burstMin);
        const int count = def.burstMin + (span > 0 ? static_cast<int>(NextRandom01() * static_cast<float>(span + 1)) : 0);

        for (int i = 0; i < count; ++i)
        {
            const auto handle = m_pool.Acquire();
            Particle* p = m_pool.Get(handle);
            if (p == nullptr) continue;   // pool full - drop silently (crowd/gib convention)

            const float phi = NextRandom01() * 2.0f * kPi;
            const float theta = NextRandom01() * spreadRad;
            const float st = std::sin(theta), ct = std::cos(theta);
            const math::Vec3 scatterDir =
                (tangent * (st * std::cos(phi))) + (bitangent * (st * std::sin(phi))) + (dir * ct);

            const float speed = def.speedMin + NextRandom01() * (def.speedMax - def.speedMin);

            p->pos = pos;
            p->vel = scatterDir * speed;
            p->age = 0.0f;
            p->life = def.lifeMin + NextRandom01() * (def.lifeMax - def.lifeMin);
            p->sizeStart = def.sizeStart;
            p->sizeEnd = def.sizeEnd;
            p->colorStart = def.colorStart;
            p->colorEnd = def.colorEnd;
            p->gravityScale = def.gravityScale;
            p->dragPerSec = def.dragPerSec;
            p->spawnY = pos.y;
            p->rotation = NextRandom01() * 2.0f * kPi;                       // random initial orientation
            p->angularVel = (NextRandom01() * 2.0f - 1.0f) * def.angularVelMax;   // random spin direction/rate
            p->blend = def.blend;
            p->kind = def.kind;
            p->phase = MushroomPhase::Rising;
            p->selfIndex = handle.index;
            p->selfGeneration = handle.generation;
        }
    }

    void ParticleSystem::Step(core::JobSystem& jobs, float fixedDelta)
    {
        const std::vector<std::uint32_t>& active = m_pool.ActiveIndices();
        if (active.empty()) return;

        Particle* slots = m_pool.Slots();
        jobs.ParallelFor(0, active.size(), 128,
            [slots, &active, fixedDelta](std::size_t begin, std::size_t end)
            {
                for (std::size_t k = begin; k < end; ++k)
                {
                    Particle& p = slots[active[k]];
                    p.age += fixedDelta;
                    p.vel.y -= kGravity * p.gravityScale * fixedDelta;
                    const float dragMul = std::max(0.0f, 1.0f - p.dragPerSec * fixedDelta);
                    p.vel = p.vel * dragMul;
                    p.pos = p.pos + p.vel * fixedDelta;
                    p.rotation += p.angularVel * fixedDelta;

                    // Mushroom cap transition (§7.2) - only ExplosionSmokeStem
                    // particles carry this kind, everything else no-ops here.
                    if (p.kind == ParticleKind::ExplosionSmokeStem && p.phase == MushroomPhase::Rising
                        && (p.pos.y - p.spawnY) > kCapHeight)
                    {
                        p.phase = MushroomPhase::Capping;
                        math::Vec3 outward{ p.vel.x, 0.0f, p.vel.z };
                        outward = math::Length(outward) > 1e-4f ? math::Normalized(outward) : math::Vec3{ 1.0f, 0.0f, 0.0f };
                        p.vel.x = outward.x * kCapOutSpeed;
                        p.vel.z = outward.z * kCapOutSpeed;
                        p.vel.y *= 0.2f;
                    }
                }
            }).Wait();

        // Despawn: main thread only, after Wait() (rule 6 - same churn pattern
        // as Simulation's gib/ordnance pools). Each particle carries its own
        // {selfIndex, selfGeneration} from Acquire() to reconstruct the Handle
        // core::ObjectPool<Particle>::Release needs (no separate handle-
        // tracking container - same trick as GibPiece/PlacedOrdnance).
        for (const std::uint32_t idx : active)
        {
            const Particle& p = slots[idx];
            if (p.age >= p.life)
                m_pool.Release({ p.selfIndex, p.selfGeneration });
        }
    }
}
