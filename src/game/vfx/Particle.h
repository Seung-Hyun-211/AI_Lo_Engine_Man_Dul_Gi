#pragma once

#include "math/Math3D.h"

#include <cstdint>

// VFX particles - muzzle flash/smoke, explosion, zombie gib blood spray
// (docs/particle-system-research.md). Built only with the 3D module, same as
// the rest of game/ that touches math/Math3D.
namespace engine::game::vfx
{
    enum class ParticleBlend : std::uint8_t { Additive, AlphaBlend };

    // Which per-step special case a particle needs in ParticleSystem::Step.
    // Everything is Generic (plain gravity/drag integration) except the
    // explosion's smoke-stem particles, which flip from rising to spreading
    // sideways at a height threshold to draw a mushroom cap (§7.2).
    enum class ParticleKind : std::uint8_t { Generic, ExplosionSmokeStem };

    // Mushroom-cap state for ParticleKind::ExplosionSmokeStem; unused (stays
    // Rising) for every other kind.
    enum class MushroomPhase : std::uint8_t { Rising, Capping };

    // One VFX particle. Deliberately carries its own physics params
    // (gravityScale/dragPerSec) copied from the ParticleEffectDef at spawn
    // time, rather than an effectId indirection into a shared table - simpler
    // for a v1 with a handful of effect kinds (see particle-system-research.md
    // §4.1's note on this being a size/simplicity tradeoff). Render-side
    // (colour/size at this instant) is derived from age/life by SnapshotBuilder,
    // not stored here.
    struct Particle
    {
        math::Vec3 pos{};
        math::Vec3 vel{};
        float age{ 0.0f };
        float life{ 1.0f };            // age >= life => despawn
        float sizeStart{ 0.15f };
        float sizeEnd{ 0.15f };
        std::uint32_t colorStart{ 0xffffffffu };   // 8:8:8:8 RGBA
        std::uint32_t colorEnd{ 0x00000000u };
        float gravityScale{ 0.0f };    // 0 = ignore gravity (smoke), >0 = embers/blood
        float dragPerSec{ 0.0f };      // velocity decay per second
        float spawnY{ 0.0f };          // world Y at spawn - ExplosionSmokeStem's rise-height test
        float rotation{ 0.0f };        // screen-plane roll, radians - random at spawn, driven by angularVel
        float angularVel{ 0.0f };      // radians/sec; used only while SnapshotBuilder isn't overriding
                                        // rotation with a velocity-aligned stretch (see BuildVfxParticles)
        ParticleBlend blend{ ParticleBlend::AlphaBlend };
        ParticleKind kind{ ParticleKind::Generic };
        MushroomPhase phase{ MushroomPhase::Rising };

        // Self-Release fields (same pattern as GibPiece/PlacedOrdnance,
        // docs/defense-combat-design.md §3/§4 "self-referential incomplete
        // type" note) - ParticleSystem::Step despawns from a serial scan over
        // ActiveIndices(), which gives an index but not the generation
        // core::ObjectPool<Particle>::Release needs, so each particle carries
        // its own handle fields, filled in at Acquire time.
        std::uint32_t selfIndex{ 0 };
        std::uint32_t selfGeneration{ 0 };

        void Reset() { *this = Particle{}; }   // core::ObjectPool contract
    };
}
