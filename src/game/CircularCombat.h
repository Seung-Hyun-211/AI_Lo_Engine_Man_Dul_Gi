#pragma once

#include "core/NonCopyable.h"
#include "game/Card.h"
#include "game/CircularBalance.h"
#include "game/HitShape.h"
#include "game/MobField.h"
#include "game/Stats.h"
#include "math/Math2D.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <random>
#include <vector>

// Attacks for the 2D "Circular" scene (docs/circular-combat.md §2.0) - firing
// a weapon or an enemy attack, the live attacks it leaves behind and the
// short-lived attack visuals. Split out of Simulation so combat has one owner
// (SRP): Simulation keeps the deck cooldowns, XP and HP and only calls
// Fire/FireEnemy/Step, then applies the returned CombatResult.
//
// Thread: main/sim thread only (it mutates MobField through its Damage*
// queries, same rule as MobField itself).
namespace engine::game
{
    // Who an attack belongs to (docs/circular-combat.md W7). The one switch on
    // it picks the target: Player attacks hit the mob field, Enemy attacks hit
    // the player's hitbox. Same effects, paths, shapes and visuals either way.
    enum class Team : std::uint8_t { Player, Enemy };

    // The row table an attack's defIndex points into: weapons.csv for the
    // player, mob_attacks.csv for enemies. The one place that mapping lives.
    [[nodiscard]] inline const std::vector<CardDef>& AttackTable(const CircularBalance& balance, Team team)
    {
        return team == Team::Player ? balance.weapons : balance.mobAttacks;
    }

    // How SnapshotBuilder draws an AttackVisual. Outline = the hit shape's
    // outline (what just got hit); Travel = a dot moving along the capsule's
    // segment (BOLT, a pierce hit) - purely cosmetic, the damage already landed;
    // Telegraph = the area a delayed attack is about to hit (fills in as it nears).
    enum class VisualStyle : std::uint8_t { Outline, Travel, Telegraph };

    // A short-lived attack visual (docs/circular-combat.md §2.3). Carries the
    // very HitShape the hit used, so the drawn size is the hit size (x the
    // attack's visual_scale). `defIndex` + `team` find colour/sprite in its row.
    struct AttackVisual
    {
        HitShape shape;
        VisualStyle style{ VisualStyle::Outline };
        std::uint8_t defIndex{ 0 };
        float ageLeft{ 0.0f };
        float life{ 1.0f };   // ageLeft/life -> 1 (just spawned) .. 0 (about to vanish)
        Team team{ Team::Player };
    };

    // A mob this attack already hit, and when (CircularCombat's clock).
    struct HitMemory
    {
        MobRef ref;
        float time{ 0.0f };
    };

    // A live attack (docs/circular-combat.md §2.2/§2.5): a projectile
    // (스태프/단검/트럼프 카드, orbiting blades, an archer's arrow ...), a
    // moving area (the lurker-style spike line) or a delayed area waiting to
    // land (a caster's hex). Where it is comes from its row's path evaluated at
    // `t` - never integrated - and every tuning number is read from the row
    // through `team` + `defIndex`; only what was fixed at fire time lives here.
    // Bounded pool (kMaxInstances), swap-removed when done.
    struct AttackInstance
    {
        static constexpr std::size_t kHitMemory = 32;   // ring: oldest hit forgotten first

        Team team{ Team::Player };
        std::uint8_t defIndex{ 0 };    // row in AttackTable(team): form, onHit, path, sizes, colour
        std::uint8_t piercesLeft{ 0 }; // Pierce on a range-limited path: hits left before it's spent
        float t{ 0.0f };               // seconds since fired
        float damage{ 0.0f };          // crit/weapon_damage applied (Random: the roll's lower bound)
        float damageMax{ 0.0f };       // Random only: the roll's upper bound
        float scale{ 1.0f };           // attack_size at fire time (sizes, travel, orbit radius); 1 for enemies
        float range{ 0.0f };           // Straight: travel budget px. Delayed area: its size
        float theta0{ 0.0f };          // Polar: start angle, radians
        float tickLeft{ 0.0f };        // moving Area: seconds to the next hit
        math::Vec2 origin{};           // caster position at fire time (delayed area: where it lands)
        math::Vec2 dir{ 1.0f, 0.0f };  // aim at fire time (normalized)
        math::Vec2 pos{};
        math::Vec2 prevPos{};          // last step's pos - the swept contact runs prevPos -> pos
        std::uint8_t memoryCount{ 0 };
        std::uint8_t memoryNext{ 0 };
        std::array<HitMemory, kHitMemory> memory{};
    };

    // What combat needs from the rest of the run for one call - values and
    // const views only, so combat never reaches into Simulation.
    struct CombatContext
    {
        const CircularBalance& balance;
        const StatBlock& stats;
        math::Vec2 playerCenter{};
        math::Vec2 facing{ 1.0f, 0.0f };   // last move direction: swing direction / aim fallback
        math::Rect playerBox{};            // the player's hitbox - what Enemy attacks test against
        bool playerHittable{ true };       // false while dashing: enemy projectiles fly through, areas miss
    };

    // What a call changed that Simulation owns: kills (-> XP), life_steal
    // healing (-> HP, clamped by the caller) and damage aimed at the player
    // (the largest single hit - the caller's hurt i-frames allow one per window).
    struct CombatResult
    {
        std::uint32_t kills{ 0 };
        float heal{ 0.0f };
        float playerDamage{ 0.0f };

        CombatResult& operator+=(const CombatResult& other)
        {
            kills += other.kills;
            heal += other.heal;
            if (other.playerDamage > playerDamage) playerDamage = other.playerDamage;
            return *this;
        }
    };

    class CircularCombat final : private core::NonCopyable
    {
    public:
        static constexpr float kOutlineLife = 0.20f;        // seconds an Outline visual blinks
        static constexpr float kTravelSpeed = 1200.0f;      // px/s a Travel visual (BOLT) flies at
        static constexpr float kTravelMinLife = 0.05f;      // seconds - floor so a point-blank hit still reads
        static constexpr float kTargetSearch = 540.0f;      // px: how far a weapon with origin=target looks for a mob (half the view height)
        static constexpr std::size_t kMaxInstances = 1024;  // player + enemy attacks share the pool

        // Drops every live attack and visual (scene reset / new run).
        void Reset();

        // One weapon whose cooldown just expired. Instant weapons damage now;
        // moving and delayed ones only spawn - damage lands in Step.
        CombatResult Fire(const CardInstance& card, const CombatContext& context, MobField& mobs, std::mt19937& rng);

        // One enemy attack (mob_attacks.csv row `attackIndex`) cast by a mob at
        // `caster` at the player (docs/circular-combat.md W7). Level 1 numbers,
        // no player stats. (`mobs` is only passed through - enemy attacks never damage mobs.)
        CombatResult FireEnemy(std::uint8_t attackIndex, math::Vec2 caster, const CombatContext& context, MobField& mobs);

        // One fixed step: moves every live attack along its path, applies its
        // hits (mobs or the player, by team), ages visuals.
        CombatResult Step(float fixedDelta, const CombatContext& context, MobField& mobs, std::mt19937& rng);

        // --- reads for the snapshot builder ---
        [[nodiscard]] const std::vector<AttackVisual>& Visuals() const { return m_visuals; }
        [[nodiscard]] const std::vector<AttackInstance>& Instances() const { return m_instances; }

    private:
        // The single "damage landed on mobs" point (docs S7): kills + life_steal heal.
        [[nodiscard]] static CombatResult Hit(const StatBlock& stats, float damage, std::uint32_t kills);
        // An area lands now: Player team damages every mob in `shape`, Enemy team
        // hits the player if the shape overlaps the hitbox. Leaves its outline.
        CombatResult LandArea(Team team, const HitShape& shape, float damage, std::uint8_t defIndex,
                              const CombatContext& context, MobField& mobs);
        // An Area without a path: lands now, or - with a delay - spawns a
        // waiting instance and a telegraph of the area it will hit.
        CombatResult CastArea(Team team, const CardDef& def, std::uint8_t defIndex, math::Vec2 center, math::Vec2 facing,
                              float size, float scale, float damage, const CombatContext& context, MobField& mobs);

        // Spawns the moving attacks of one cast (`count` of them on a Polar path).
        void SpawnMoving(Team team, const CardDef& def, std::uint8_t defIndex, int pierces, math::Vec2 origin, math::Vec2 aim,
                         float scale, int count, float damage, float damageMax, float range, math::Vec2 playerCenter);
        // Projectile contact for one step: sweep prevPos -> pos, apply onHit.
        // Returns true when the attack is spent. Player team: mobs.
        bool ResolveContact(AttackInstance& attack, const CardDef& def, const CombatContext& context, MobField& mobs,
                            std::mt19937& rng, CombatResult& result);
        // Enemy team: the player's hitbox (flies through while it's not hittable).
        bool ResolveEnemyContact(AttackInstance& attack, const CardDef& def, const CombatContext& context, MobField& mobs,
                                 std::mt19937& rng, CombatResult& result);

        float m_time{ 0.0f };                  // combat clock (HitMemory times)
        std::vector<AttackVisual> m_visuals;
        std::vector<AttackInstance> m_instances;   // <= kMaxInstances
        std::vector<MobHit> m_touchScratch;    // reused by ResolveContact, avoids a per-step allocation
    };
}
