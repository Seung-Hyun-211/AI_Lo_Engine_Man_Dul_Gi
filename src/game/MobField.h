#pragma once

#include "core/JobSystem.h"
#include "core/NonCopyable.h"
#include "game/HitShape.h"
#include "math/Math2D.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <span>
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
// swap-remove shape as core::ObjectPool). Each slot also has a generation,
// bumped when its mob dies, so a MobRef held across steps (a projectile's
// "already hit" memory, docs/circular-combat.md §2.2) goes stale instead of
// silently pointing at whatever respawned into the slot.
//
// Thread: main/sim thread only for Spawn/Kill/Damage*/Clear - same
// rule as core::ObjectPool and CollisionWorld (CLAUDE.md invariant 6). Step()
// itself runs a JobSystem::ParallelFor internally and blocks until it
// completes, so it is safe to call from the main thread like any other
// Simulation::Step sub-call.
namespace engine::game
{
    // Per-mob behaviour. Seek = chase the player (default); Windup = frozen
    // and flashing until LaunchCharge; Charge = straight-line dash that ends
    // back in Seek when its timer runs out.
    enum class MobState : std::uint8_t { Seek = 0, Windup = 1, Charge = 2 };

    // One specific mob across steps: slot + the generation it had when seen.
    struct MobRef
    {
        std::uint32_t slot{ 0 };
        std::uint32_t generation{ 0 };
        [[nodiscard]] bool operator==(const MobRef&) const = default;
    };

    // One mob found by MobField::Overlapping.
    struct MobHit
    {
        MobRef ref;
        math::Vec2 pos{};
    };

    // Per mob kind (indexed by the mob's type byte), handed in by the caller so
    // MobField stays ignorant of CSV/balance types. Behaviour is these numbers,
    // not a per-class code path (docs/circular-design.md §5.3).
    struct MobMotion
    {
        float speed{ 0.0f };          // px/s
        float keepDistance{ 0.0f };   // 0 = chase; > 0 = stop once this close to the target
    };
    struct MobAttackTiming
    {
        float cooldown{ 0.0f };       // s between casts; 0 = this kind never casts
        float range{ 0.0f };          // casts only while the target is within this
    };
    // One cast CollectCasts found due this step.
    struct MobCast
    {
        math::Vec2 pos{};
        std::uint8_t type{ 0 };
    };

    class MobField final : private core::NonCopyable
    {
    public:
        explicit MobField(std::size_t capacity);

        // Recycles a free slot at `pos` as a mob of kind `type` (an index the
        // caller's per-kind tables use). `attackDelay` = seconds before its
        // first cast. Returns false (silent drop, same "not fatal" convention
        // as core::ObjectPool::Acquire on a full pool) when the field is full.
        bool Spawn(math::Vec2 pos, float health, float radius, std::uint8_t type = 0, float attackDelay = 0.0f);

        // Steers every live mob toward `target` at its kind's speed - or holds
        // it once within its kind's keepDistance - and integrates position,
        // JobSystem::ParallelFor over contiguous ranges of the dense active-
        // index list (rule 6 - each job's range names a disjoint set of slot
        // indices, so no two workers ever write the same posX/posY/velX/velY
        // entry). A type past the end of `motion` uses entry 0 (an F5 reload
        // shrank the table). Blocks until the fence completes.
        void Step(core::JobSystem& jobs, math::Vec2 target, std::span<const MobMotion> motion, float fixedDelta);

        // Counts down every live mob's attack timer; each one that ran out while
        // `target` is inside its kind's range is appended to `out` (after
        // clearing it) and re-armed with the cooldown. A due mob out of range
        // waits, ready to cast the moment the target comes close. Kinds with
        // cooldown 0 never cast. Main thread only.
        void CollectCasts(float fixedDelta, math::Vec2 target, std::span<const MobAttackTiming> timing, std::vector<MobCast>& out);

        // Read-only: the largest `contactDamage[type]` among live mobs whose
        // disc touches `box` (the player's hitbox), 0 when none does. Main thread only.
        [[nodiscard]] float StrongestTouching(const math::Rect& box, std::span<const float> contactDamage) const;

        // Applies `amount` damage to every live mob overlapping `shape` - a
        // mob counts when any part of its disc (own radius) is inside
        // (docs/circular-combat.md §2.1). Linear scan over the dense active
        // list - same "fine until profiling says otherwise" call as
        // CollisionWorld2D's brute-force broadphase; a uniform grid goes here
        // first if this ever needs to scale past what a per-attack O(live mob
        // count) scan can afford. Main thread only. Returns how many mobs
        // this call killed so the caller can award kills/XP without a second pass.
        std::uint32_t DamageInShape(const HitShape& shape, float amount);

        // Read-only: every live mob overlapping `shape` (same rule as
        // DamageInShape), appended to `out` after clearing it. For attacks
        // that must pick among or remember the mobs they touch (projectiles:
        // pierce order, re-hit gating). Main thread only.
        void Overlapping(const HitShape& shape, std::vector<MobHit>& out) const;

        // Applies `amount` to one mob. A stale ref (that mob already died,
        // even if its slot was reused) is a no-op. Returns true if this killed it.
        bool Damage(MobRef ref, float amount);

        // Applies `amount` damage to the `count` (<= 8) live mobs nearest to
        // `center` within `range` (a single-target/multi-target card). Writes
        // each hit mob's position to hitOut[0..hitCount) for the caller's
        // visuals. Linear scan with a small sorted candidate list, like
        // DamageInShape. Main thread only. Returns how many mobs it killed.
        std::uint32_t DamageNearest(math::Vec2 center, float range, float amount, std::uint32_t count,
                                    math::Vec2* hitOut, std::uint32_t& hitCount);

        // Read-only: the closest live mob within `radius` of `center`, if any
        // (no damage). Used to aim a thrown projectile (docs §3.2 스태프/단검/
        // 트럼프 카드) without a damaging scan. Main thread only (consistent
        // with the Damage* queries, though this one doesn't mutate).
        [[nodiscard]] bool ClosestWithin(math::Vec2 center, float radius, math::Vec2& posOut) const;

        // Charge pattern, step 1 (docs/circular-design.md "돌진 패턴"): freezes
        // a deterministic subset of Seek mobs into Windup. Eligible = outside
        // `exclude` and at least `minDistance` from `center`; `fraction` of
        // those are picked by a slot-index hash salted with `salt` (no RNG -
        // same deterministic convention as the spawn angle), so the subset
        // differs pattern to pattern. Main thread only. Returns the count.
        std::uint32_t BeginWindup(math::Vec2 center, const math::Rect& exclude,
                                  float minDistance, float fraction, std::uint32_t salt);

        // Step 2: every Windup mob dashes straight at `target` (direction
        // fixed now, not re-aimed) at `speed` for `duration` seconds.
        void LaunchCharge(math::Vec2 target, float speed, float duration);

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
        [[nodiscard]] MobState State(std::uint32_t index) const { return static_cast<MobState>(m_state[index]); }
        [[nodiscard]] std::uint8_t Type(std::uint32_t index) const { return m_type[index]; }
        [[nodiscard]] const std::vector<std::uint32_t>& ActiveIndices() const { return m_active; }

        // Deaths per mob kind since the last ClearKillTally (every Damage* kill
        // lands here) - the caller turns them into XP by kind, then clears.
        [[nodiscard]] const std::vector<std::uint32_t>& KillTally() const { return m_killTally; }
        void ClearKillTally() { std::fill(m_killTally.begin(), m_killTally.end(), 0u); }

    private:
        // O(1) swap-remove out of the active list, same idiom as
        // core::ObjectPool::Release. Index must currently be live; a stale
        // or already-dead index is a no-op (safe to call twice).
        void Kill(std::uint32_t index);

        std::vector<float>         m_posX, m_posY;
        std::vector<float>         m_velX, m_velY;
        std::vector<float>         m_health;
        std::vector<float>         m_radius;
        std::vector<std::uint8_t>  m_state;       // MobState per slot
        std::vector<float>         m_stateTimer;  // seconds left in Charge
        std::vector<std::uint8_t>  m_type;        // mob kind per slot (index into the caller's tables)
        std::vector<float>         m_attackTimer; // seconds until this mob may cast again
        std::vector<std::uint32_t> m_killTally;   // deaths per kind since ClearKillTally (grown on demand)
        std::vector<std::uint8_t>  m_slotActive;  // per slot 0/1
        std::vector<std::uint32_t> m_generation;  // per slot, bumped on death (MobRef staleness)
        std::vector<std::uint32_t> m_activePos;   // slot index -> its position in m_active (valid while active)
        std::vector<std::uint32_t> m_active;      // dense live-slot indices
        std::vector<std::uint32_t> m_free;        // free-slot stack
        std::vector<std::uint32_t> m_deadScratch; // reused by DamageInShape/DamageNearest, avoids a per-call allocation
    };
}
