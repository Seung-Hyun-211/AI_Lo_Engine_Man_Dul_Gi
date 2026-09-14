#pragma once

#include "game/vfx/ParticleEffects.h"
#include "game/vfx/ParticleSystem.h"

// Translation layer between game concepts ("a rifle fired", "something
// exploded", "a zombie died") and particle primitives (docs/particle-system-
// research.md §7). ParticleSystem itself knows nothing about muzzles or
// explosions - these are the only functions that do (SRP).
namespace engine::game::vfx
{
    inline void SpawnMuzzleFlash(ParticleSystem& vfx, math::Vec3 muzzlePos, math::Vec3 muzzleDir)
    {
        vfx.SpawnBurst(kMuzzleFlash, muzzlePos, muzzleDir);
        vfx.SpawnBurst(kMuzzleSmoke, muzzlePos, muzzleDir);
    }

    inline void SpawnExplosion(ParticleSystem& vfx, math::Vec3 groundPos)
    {
        constexpr math::Vec3 kUp{ 0.0f, 1.0f, 0.0f };
        vfx.SpawnBurst(kExplosionFlash, groundPos, kUp);
        vfx.SpawnBurst(kExplosionFireball, groundPos, kUp);
        vfx.SpawnBurst(kExplosionSmokeStem, groundPos, kUp);
        vfx.SpawnBurst(kExplosionEmbers, groundPos, kUp);
        // Rigid debris is not a particle - Simulation::TriggerExplosion's own
        // knockback/gib paths cover that (instanced-rendering.md's MeshInstance
        // route), not this file.
    }

    inline void SpawnGibBurst(ParticleSystem& vfx, math::Vec3 deathPos)
    {
        vfx.SpawnBurst(kGibBloodSpray, deathPos, { 0.0f, 1.0f, 0.0f });
    }

    inline void SpawnFlameJet(ParticleSystem& vfx, math::Vec3 origin, math::Vec3 dir)
    {
        vfx.SpawnBurst(kFlameJet, origin, dir);
    }
}
