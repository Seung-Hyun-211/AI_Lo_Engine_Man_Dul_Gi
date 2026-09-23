#pragma once

#include "core/NonCopyable.h"
#include "game/Card.h"
#include "game/CircularBalance.h"
#include "game/HitShape.h"
#include "game/MobField.h"
#include "game/Stats.h"
#include "math/Math2D.h"

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
    // How SnapshotBuilder draws an AttackVisual. Outline = the hit shape's
    // outline (what just got hit); Travel = a dot moving along the capsule's
    // segment (BOLT, a pierce hit) - purely cosmetic, the damage already landed.
    enum class VisualStyle : std::uint8_t { Outline, Travel };

    // A short-lived attack visual (docs/circular-combat.md §2.3). Carries the
    // very HitShape the hit used, so the drawn size is the hit size (x the
    // weapon's visual_scale). `defIndex` finds colour/sprite in the weapon row.
    struct AttackVisual
    {
        HitShape shape;
        VisualStyle style{ VisualStyle::Outline };
        std::uint8_t defIndex{ 0 };
        float ageLeft{ 0.0f };
        float life{ 1.0f };   // ageLeft/life -> 1 (just spawned) .. 0 (about to vanish)
    };

    // A real, physically-travelling shot (스태프/단검/트럼프 카드) - carries
    // gameplay state: its own position/velocity/remaining flight distance,
    // and it deals damage when it actually touches a mob (not on cast). What
    // it does on contact is its weapon's EffectSpec::onHit. Bounded pool
    // (kMaxProjectiles), swap-removed when spent.
    struct Projectile
    {
        math::Vec2 pos{};
        math::Vec2 vel{};              // px/s
        float rangeLeft{ 0.0f };       // px budget - despawns (no hit) when this runs out
        float damage{ 0.0f };          // the hit damage (Random: the roll's lower bound)
        float damageMax{ 0.0f };       // Random only: the roll's upper bound
        float explodeRadius{ 0.0f };   // Explode only: blast radius (attack_size applied)
        std::uint8_t piercesLeft{ 0 }; // Pierce only: hits left before it's spent
        std::uint8_t defIndex{ 0 };    // weapon row: onHit, colour, size
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
        static constexpr float kOutlineLife = 0.20f;        // seconds an Outline visual blinks
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
        [[nodiscard]] const std::vector<AttackVisual>& Visuals() const { return m_visuals; }
        [[nodiscard]] const std::vector<Projectile>& Projectiles() const { return m_projectiles; }

    private:
        // The single "damage landed" point (docs S7): kills + life_steal heal.
        [[nodiscard]] static CombatResult Hit(const StatBlock& stats, float damage, std::uint32_t kills);
        // Area attack: damage everything in `shape`, leave its outline.
        CombatResult HitArea(const HitShape& shape, float damage, std::uint8_t defIndex, const StatBlock& stats, MobField& mobs);

        std::vector<AttackVisual> m_visuals;
        std::vector<Projectile> m_projectiles;   // <= kMaxProjectiles
    };
}
