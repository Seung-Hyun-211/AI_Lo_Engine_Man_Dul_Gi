#pragma once

#include "core/NonCopyable.h"
#include "game/Card.h"
#include "game/CircularBalance.h"
#include "game/MobField.h"
#include "game/Stats.h"
#include "math/Math.h"

#include <cstddef>
#include <cstdint>
#include <random>
#include <vector>

// Weapon attacks for the 2D "Circular" scene (docs/circular-combat.md §2.0) -
// firing a weapon, the live projectiles it leaves behind and the short-lived
// attack visuals. Split out of Simulation so combat has one owner (SRP):
// Simulation keeps the deck cooldowns, XP and HP and only calls Fire/Step,
// then applies the returned CombatResult.
//
// Thread: main/sim thread only (it mutates MobField through its Damage*
// queries, same rule as MobField itself).
namespace engine::game
{
    // A blinking yellow ring showing PULSE's radius at the moment it fired -
    // render-only (no EffectPass2D glow - plain flat shapes only, docs/
    // circular-design.md §7). SnapshotBuilder draws it as a dashed ring of worldQuads.
    struct PulseRing
    {
        math::Vec2 pos{};
        float range{ 0.0f };
        float ageLeft{ 0.0f };
        float life{ 1.0f };   // ageLeft/life -> 1 (just spawned) .. 0 (about to vanish)
    };

    // A red square that visually travels from the caster to a BOLT hit -
    // render-only and purely cosmetic: the hit already landed (DamageNearest)
    // when this is queued, so it never affects gameplay timing, only how the
    // shot reads on screen.
    struct BoltShot
    {
        math::Vec2 start{};
        math::Vec2 end{};
        float ageLeft{ 0.0f };
        float life{ 1.0f };   // ageLeft/life -> 1 (just fired) .. 0 (arrived)
    };

    // Which weapon-specific thing a live Projectile does on its first hit each
    // step (docs/circular-design.md §3.2 무기 5종). CircularCombat::Step
    // switches on this - same "effect tag, not an if-chain" shape as CardEffect.
    enum class ProjectileKind : std::uint8_t { Piercing, Exploding, Random };

    // A real, physically-travelling shot (스태프/단검/트럼프 카드) - unlike
    // BoltShot above, this one carries gameplay state: its own position/
    // velocity/remaining flight distance, and it deals damage when it
    // actually touches a mob (not on cast). Bounded pool (kMaxProjectiles),
    // swap-removed when spent.
    struct Projectile
    {
        math::Vec2 pos{};
        math::Vec2 vel{};              // px/s
        float rangeLeft{ 0.0f };       // px budget - despawns (no hit) when this runs out
        float damage{ 0.0f };          // Piercing/Exploding: the hit damage. Random: the roll's lower bound.
        float damageMax{ 0.0f };       // Random only: the roll's upper bound (unused otherwise)
        float explodeRadius{ 0.0f };   // Exploding only: AoE radius on contact
        std::uint8_t piercesLeft{ 0 }; // Piercing only: hits left before it's spent
        ProjectileKind kind{ ProjectileKind::Piercing };
    };

    // What combat needs from the rest of the run for one call - values and
    // const views only, so combat never reaches into Simulation.
    struct CombatContext
    {
        const CircularBalance& balance;
        const StatBlock& stats;
        math::Vec2 playerCenter{};
        math::Vec2 facing{ 1.0f, 0.0f };   // last move direction: swing direction / aim fallback
    };

    // What a call changed that Simulation owns: kills (-> XP) and life_steal
    // healing (-> HP, clamped by the caller).
    struct CombatResult
    {
        std::uint32_t kills{ 0 };
        float heal{ 0.0f };

        CombatResult& operator+=(const CombatResult& other)
        {
            kills += other.kills;
            heal += other.heal;
            return *this;
        }
    };

    class CircularCombat final : private core::NonCopyable
    {
    public:
        static constexpr float kPulseRingLife = 0.20f;      // seconds the PULSE range ring blinks
        static constexpr float kBoltShotSpeed = 1200.0f;    // px/s the BOLT visual travels at
        static constexpr float kBoltShotMinLife = 0.05f;    // seconds - floor so a point-blank hit still reads
        static constexpr std::size_t kMaxProjectiles = 512;
        static constexpr float kProjectileHitReach = 6.0f;  // px added to a mob's radius for the touch test
        // A Piercing shot that lands a hit is nudged this far past the mob it
        // just hit (along its own velocity) so the NEXT step's touch test
        // doesn't immediately re-hit the same still-alive mob. [살] mitigation,
        // not exact geometry.
        static constexpr float kPierceClearDistance = 24.0f;

        // Drops every projectile and visual (scene reset / new run).
        void Reset();

        // One weapon whose cooldown just expired. Instant weapons damage now;
        // projectile weapons only spawn their shot (damage lands in Step).
        CombatResult Fire(const CardInstance& card, const CombatContext& context, MobField& mobs, std::mt19937& rng);

        // One fixed step: flies projectiles, applies their hits, ages visuals.
        CombatResult Step(float fixedDelta, const CombatContext& context, MobField& mobs, std::mt19937& rng);

        // --- reads for the snapshot builder ---
        [[nodiscard]] const std::vector<PulseRing>& PulseRings() const { return m_pulseRings; }
        [[nodiscard]] const std::vector<BoltShot>& BoltShots() const { return m_boltShots; }
        [[nodiscard]] const std::vector<Projectile>& Projectiles() const { return m_projectiles; }

    private:
        // life_steal (§2.6): heals a fraction of the blow's damage per kill.
        [[nodiscard]] static CombatResult Hit(const StatBlock& stats, float damage, std::uint32_t kills);

        std::vector<PulseRing> m_pulseRings;
        std::vector<BoltShot> m_boltShots;
        std::vector<Projectile> m_projectiles;   // <= kMaxProjectiles
    };
}
