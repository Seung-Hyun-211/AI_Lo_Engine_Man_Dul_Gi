#pragma once

#include "core/JobSystem.h"
#include "core/NonCopyable.h"
#include "math/Math2D.h"

#include <cstddef>
#include <cstdint>
#include <vector>

// Structure-of-arrays mob swarm for the 2D "Circular" scene (docs/circular-
// design.md), designed from the start for mass mob hunting - hundreds to
// thousands of homogeneous mobs stepped every fixed tick. Peer of the 3D
// module's core::ObjectPool<SimAgent> (an array-of-struct pool: docs/entity-
// lifecycle-design.md §3A), but SoA - each field is its own contiguous
// vector, so JobSystem::ParallelFor's per-range steering pass only touches
// the fields it needs (pos/vel) instead of dragging health/radius through
// cache alongside them the way one big Mob struct would. This is the
// AgentStore shape docs/horde-design.md §6.2 left unimplemented for the 3D
// crowd, realized here for 2D instead - 2D mobs have no per-instance
// animation/mesh state to justify AoS the way SimAgent's VAT clip time does.
//
// Recycling is a free-list of slot indices plus a dense active list (same
// swap-remove shape as core::ObjectPool), but no per-slot generation: nothing
// in this scene holds a Handle across frames yet (SnapshotBuilder reads
// ActiveIndices() fresh every frame). Add a generation-checked Handle later
// if something needs to reference one specific mob across steps (e.g. a
// homing projectile lock-on) - YAGNI for now.
//
// Thread: main/sim thread only for Spawn/Kill/DamageInRadius/Clear - same
// rule as core::ObjectPool and CollisionWorld (CLAUDE.md invariant 6). Step()
// itself runs a JobSystem::ParallelFor internally and blocks until it
// completes, so it is safe to call from the main thread like any other
// Simulation::Step sub-call.
namespace engine::game
{
    class MobField final : private core::NonCopyable
    {
    public:
        explicit MobField(std::size_t capacity);

        // Recycles a free slot at `pos`. Returns false (silent drop, same
        // "not fatal" convention as core::ObjectPool::Acquire on a full pool)
        // when the field is already at capacity.
        bool Spawn(math::Vec2 pos, float health, float radius);

        // Steers every live mob toward `target` at `speed` and integrates
        // position, JobSystem::ParallelFor over contiguous ranges of the
        // dense active-index list (rule 6 - each job's range names a disjoint
        // set of slot indices, so no two workers ever write the same posX/
        // posY/velX/velY entry). Blocks until the fence completes.
        void Step(core::JobSystem& jobs, math::Vec2 target, float speed, float fixedDelta);

        // Applies `amount` damage to every live mob within `radius` of
        // `center` (a card/weapon attack). Linear scan over the dense active
        // list - same "fine until profiling says otherwise" call as
        // CollisionWorld2D's brute-force broadphase (that file's own
        // comment); a uniform grid goes here first if this ever needs to
        // scale past what a per-attack O(live mob count) scan can afford.
        // Main thread only. Returns how many mobs this call killed so the
        // caller can award kills/XP without a second pass.
        std::uint32_t DamageInRadius(math::Vec2 center, float radius, float amount);

        // Empties the field back to "all slots free" (scene reset).
        void Clear();

        [[nodiscard]] std::size_t Capacity() const { return m_posX.size(); }
        [[nodiscard]] std::size_t LiveCount() const { return m_active.size(); }
        [[nodiscard]] bool Full() const { return m_free.empty(); }

        // Raw SoA arrays, index-aligned, Capacity() entries; only indices
        // in ActiveIndices() are live. SnapshotBuilder reads these directly
        // (read-only, main thread) to emit Quads - no per-mob struct exists
        // to copy out of, by design (SoA all the way to the render boundary).
        [[nodiscard]] const std::vector<float>& PosX() const { return m_posX; }
        [[nodiscard]] const std::vector<float>& PosY() const { return m_posY; }
        [[nodiscard]] const std::vector<float>& Radius() const { return m_radius; }
        [[nodiscard]] const std::vector<float>& Health() const { return m_health; }
        [[nodiscard]] const std::vector<std::uint32_t>& ActiveIndices() const { return m_active; }

    private:
        // O(1) swap-remove out of the active list, same idiom as
        // core::ObjectPool::Release. Index must currently be live; a stale
        // or already-dead index is a no-op (safe to call twice).
        void Kill(std::uint32_t index);

        std::vector<float>         m_posX, m_posY;
        std::vector<float>         m_velX, m_velY;
        std::vector<float>         m_health;
        std::vector<float>         m_radius;
        std::vector<std::uint8_t>  m_slotActive;  // per slot 0/1
        std::vector<std::uint32_t> m_activePos;   // slot index -> its position in m_active (valid while active)
        std::vector<std::uint32_t> m_active;      // dense live-slot indices
        std::vector<std::uint32_t> m_free;        // free-slot stack
        std::vector<std::uint32_t> m_deadScratch; // reused by DamageInRadius, avoids a per-call allocation
    };
}
