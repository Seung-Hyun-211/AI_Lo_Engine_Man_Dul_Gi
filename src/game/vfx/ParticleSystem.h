#pragma once

#include "core/JobSystem.h"
#include "core/NonCopyable.h"
#include "core/ObjectPool.h"
#include "game/vfx/Particle.h"
#include "game/vfx/ParticleEffectDef.h"

// Mechanism only - "what a burst looks like" lives in ParticleEffectDef
// (policy), "what a muzzle flash or explosion is" lives in WeaponVfx.h
// (SRP layering, docs/particle-system-research.md §6/§7).
namespace engine::game::vfx
{
    class ParticleSystem final : private core::NonCopyable
    {
    public:
        static constexpr std::size_t kCapacity = 2048;   // muzzle (tens) + explosion (hundreds), measurement gate if raised

        // Spawns `def.burstMin..burstMax` particles at `pos`, scattered within
        // a `def.spreadDeg` cone around `dir` (normalized internally; treated
        // as +Y if near-zero). Main thread, between Step() calls only (rule 6 -
        // same contract as Simulation's other Acquire-on-spawn call sites).
        // Silently drops particles once the pool is full (matches the crowd/
        // gib convention - no error, no forced eviction).
        void SpawnBurst(const ParticleEffectDef& def, math::Vec3 pos, math::Vec3 dir);

        // Fixed-step integration: gravity, drag, position, age, and the
        // ExplosionSmokeStem mushroom-cap transition. `jobs` parallelizes it
        // the same way Simulation::StepSimAgents does.
        void Step(core::JobSystem& jobs, float fixedDelta);

        [[nodiscard]] const core::ObjectPool<Particle>& Pool() const { return m_pool; }

    private:
        core::ObjectPool<Particle> m_pool{ kCapacity };
        float m_seed{ 1.0f };   // deterministic no-RNG scatter (same convention as Simulation::SpawnGibs)

        [[nodiscard]] float NextRandom01();   // advances m_seed, returns a pseudo-random value in [0, 1)
    };
}
