#pragma once

// Tunables for the debris pieces spawned on zombie death (docs/defense-combat-
// design.md §3 "기브 스폰"). Same constexpr-table convention as CrowdConfig.h.
// No mesh field here - game/ stays render-header-free; SnapshotBuilder picks
// the mesh (currently render::MeshId::Cube as a stand-in for real low-poly
// limb/head pieces - see that doc's §3 "1차: 임시 큐브").
namespace engine::game
{
    struct GibDef
    {
        int   countMin, countMax;   // pieces spawned per death
        float speedMin, speedMax;   // initial outward+upward speed, m/s
        float life;                 // seconds alive (flight + resting on the ground) before despawn
    };

    inline constexpr GibDef kZombieGibs{
        /*countMin*/ 3, /*countMax*/ 5,
        /*speedMin*/ 2.0f, /*speedMax*/ 5.0f,
        /*life*/ 1.5f };
}
