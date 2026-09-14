#pragma once

#include "game/vfx/ParticleEffectDef.h"

// Concrete effect tuning (docs/particle-system-research.md §4.2/§7). Add a new
// effect by adding a definition here - ParticleSystem/ParticlePass3D never
// change (OCP). No texture/sprite fields: v1 renders every particle as a
// procedural soft circle (assets/shaders/particle.hlsl), so "what it looks
// like" is entirely colour + blend + size + spin/stretch, no atlas dependency.
namespace engine::game::vfx
{
    // --- Muzzle (§7.1) ---
    inline constexpr ParticleEffectDef kMuzzleFlash{
        .blend = ParticleBlend::Additive, .kind = ParticleKind::Generic,
        .colorStart = 0xfff0b0ffu, .colorEnd = 0xffaa0000u,
        .sizeStart = 0.18f, .sizeEnd = 0.05f,
        .lifeMin = 0.03f, .lifeMax = 0.05f,
        .speedMin = 0.0f, .speedMax = 0.0f, .spreadDeg = 0.0f,
        .gravityScale = 0.0f, .dragPerSec = 0.0f, .angularVelMax = 0.0f,   // too short-lived to matter
        .burstMin = 1, .burstMax = 1 };

    inline constexpr ParticleEffectDef kMuzzleSmoke{
        .blend = ParticleBlend::AlphaBlend, .kind = ParticleKind::Generic,
        .colorStart = 0xccccccaau, .colorEnd = 0x88888800u,
        .sizeStart = 0.08f, .sizeEnd = 0.22f,
        .lifeMin = 0.4f, .lifeMax = 1.0f,
        .speedMin = 0.3f, .speedMax = 0.8f, .spreadDeg = 25.0f,
        .gravityScale = -0.15f /* buoyant */, .dragPerSec = 0.5f, .angularVelMax = 1.5f,
        .burstMin = 2, .burstMax = 4 };

    // --- Explosion (§7.2) ---
    // Sizes bumped well past what "looks right up close" - a mortar/mine hit
    // is typically seen from many metres away across the field, so it needs
    // to read at a distance, not just next to the camera (docs/particle-
    // system-research.md §12 구현 노트, playtest: explosion wasn't visible
    // at the original close-up sizes).
    inline constexpr ParticleEffectDef kExplosionFlash{
        .blend = ParticleBlend::Additive, .kind = ParticleKind::Generic,
        .colorStart = 0xffffffffu, .colorEnd = 0xffcc4400u,
        .sizeStart = 3.5f, .sizeEnd = 1.0f,
        .lifeMin = 0.08f, .lifeMax = 0.12f,
        .speedMin = 0.0f, .speedMax = 0.0f, .spreadDeg = 0.0f,
        .gravityScale = 0.0f, .dragPerSec = 0.0f, .angularVelMax = 0.0f,
        .burstMin = 1, .burstMax = 1 };

    inline constexpr ParticleEffectDef kExplosionFireball{
        .blend = ParticleBlend::Additive, .kind = ParticleKind::Generic,
        .colorStart = 0xffdd66ffu, .colorEnd = 0xaa220000u,
        .sizeStart = 1.5f, .sizeEnd = 4.0f,
        .lifeMin = 0.3f, .lifeMax = 0.5f,
        .speedMin = 3.0f, .speedMax = 6.0f, .spreadDeg = 60.0f,
        .gravityScale = 0.0f, .dragPerSec = 3.0f, .angularVelMax = 4.0f,
        .burstMin = 10, .burstMax = 16 };

    // Rises, then ParticleSystem::Step flips it sideways at kCapHeight to draw
    // a mushroom cap (§7.2) - ParticleKind::ExplosionSmokeStem is what tells
    // Step() to run that transition (and SnapshotBuilder to skip the motion-
    // stretch it applies to fast debris - smoke stays a tumbling round blob,
    // §12 구현 노트 "여전히 평면으로 보임"). More, smaller particles than the
    // original sketch so the cloud reads as overlapping volume rather than a
    // few individually-large flat discs.
    inline constexpr ParticleEffectDef kExplosionSmokeStem{
        .blend = ParticleBlend::AlphaBlend, .kind = ParticleKind::ExplosionSmokeStem,
        .colorStart = 0xddddddccu, .colorEnd = 0x22222200u,
        .sizeStart = 0.5f, .sizeEnd = 1.4f,
        .lifeMin = 1.5f, .lifeMax = 2.5f,
        .speedMin = 2.5f, .speedMax = 4.5f, .spreadDeg = 20.0f,
        .gravityScale = 0.0f, .dragPerSec = 0.8f, .angularVelMax = 1.0f,
        .burstMin = 16, .burstMax = 22 };

    inline constexpr ParticleEffectDef kExplosionEmbers{
        .blend = ParticleBlend::Additive, .kind = ParticleKind::Generic,
        .colorStart = 0xffcc44ffu, .colorEnd = 0xff220000u,
        .sizeStart = 0.2f, .sizeEnd = 0.06f,
        .lifeMin = 0.5f, .lifeMax = 1.0f,
        .speedMin = 4.0f, .speedMax = 8.0f, .spreadDeg = 70.0f,
        .gravityScale = 1.0f, .dragPerSec = 0.3f, .angularVelMax = 8.0f,
        .burstMin = 6, .burstMax = 10 };

    // --- Flamethrower jet (§7 - ApplyFlameCone) ---
    // Spawned every frame the trigger is held (~60/s via FireWeapon), not as
    // one big burst like muzzle/explosion - burstMin/Max stays small and the
    // "continuous jet" look comes from many short-lived particles overlapping,
    // not from a few long-lived ones. speed*lifeMax reaches roughly kFlameRange
    // (Simulation.h) so the visible flame roughly matches the damage cone.
    inline constexpr ParticleEffectDef kFlameJet{
        .blend = ParticleBlend::Additive, .kind = ParticleKind::Generic,
        .colorStart = 0xffffaa22u, .colorEnd = 0xaa441100u,
        .sizeStart = 0.35f, .sizeEnd = 0.15f,
        .lifeMin = 0.18f, .lifeMax = 0.3f,
        .speedMin = 6.0f, .speedMax = 10.0f, .spreadDeg = 16.0f,
        .gravityScale = -0.1f /* slight rise */, .dragPerSec = 1.0f, .angularVelMax = 3.0f,
        .burstMin = 2, .burstMax = 3 };

    // --- Zombie gib blood spray (§7.3, alongside Simulation::SpawnGibs's
    // existing strong-piece pieces) ---
    inline constexpr ParticleEffectDef kGibBloodSpray{
        .blend = ParticleBlend::AlphaBlend, .kind = ParticleKind::Generic,
        .colorStart = 0xaa1010eeu, .colorEnd = 0x550808aau,
        .sizeStart = 0.07f, .sizeEnd = 0.03f,
        .lifeMin = 0.25f, .lifeMax = 0.45f,
        .speedMin = 1.5f, .speedMax = 4.0f, .spreadDeg = 40.0f,
        .gravityScale = 1.0f, .dragPerSec = 0.2f, .angularVelMax = 6.0f,
        .burstMin = 6, .burstMax = 10 };
}
