#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <string>

#include "game/HitShape.h"
#include "game/Stats.h"

// Card / stat data for the 2D "Circular" scene (docs/circular-design.md §2/§3,
// docs/circular-combat.md §2.4). Pure data + tiny evaluators - no simulation
// state, no 3D dependency. Adding a weapon = one weapons.csv row (this file's
// kCardDefs is only the fallback). A brand-new effect = one CardEffect
// enumerator + one kEffectSpecs row; CircularCombat never switches on
// CardEffect, only on the spec's form / onHit. The deck itself is a plain
// std::vector<CardInstance> in Simulation (§2: a deck is a homogeneous slot
// array, so entity-lifecycle-design.md §3A, not §3B).
namespace engine::game
{
    // Attack is the only kind implemented; Summon/Buff/Passive (docs §2) slot in
    // as new enumerators when their cards exist.
    enum class CardKind : std::uint8_t { Attack };

    // How an attack reaches mobs (docs/circular-combat.md §2.5).
    enum class AttackForm : std::uint8_t
    {
        Area,         // every mob inside a HitShape - once at the caster, or each tick along a path
        Nearest,      // the N nearest mobs within range (BOLT)
        Projectile,   // flies along a path, OnHit when it touches a mob
    };

    // What a Projectile does on contact.
    enum class OnHit : std::uint8_t
    {
        None,      // not a projectile
        Pierce,    // damage, keep flying until the pierce count (or lifetime) runs out
        Explode,   // damage everything in explode_radius around the mob it touched, then vanish
        Random,    // damage rolled in [damage, damage_max], then vanish
    };

    // weapons.csv's `effect` column. The name is the identity; what it does is
    // entirely its kEffectSpecs row.
    enum class CardEffect : std::uint8_t
    {
        RadialPulse, NearestBolt, ArcSwing, LineSwing, ExplodingBolt, PiercingShot, RandomDamageShot, Count
    };

    struct EffectSpec
    {
        CardEffect   effect;
        const char*  name;    // weapons.csv spelling (lower case)
        AttackForm   form;
        HitShapeKind shape;   // Area: the shape. Explode: the blast. Otherwise unused (Circle).
        OnHit        onHit;
    };

    // The one place an effect name turns into behaviour (docs/circular-combat.md
    // §2.4) - the loader, the fallback weapons and CircularCombat all read it.
    inline constexpr EffectSpec kEffectSpecs[] = {
        { CardEffect::RadialPulse,      "radialpulse",      AttackForm::Area,       HitShapeKind::Circle,  OnHit::None    },
        { CardEffect::NearestBolt,      "nearestbolt",      AttackForm::Nearest,    HitShapeKind::Circle,  OnHit::None    },
        { CardEffect::ArcSwing,         "arcswing",         AttackForm::Area,       HitShapeKind::Arc,     OnHit::None    },
        { CardEffect::LineSwing,        "lineswing",        AttackForm::Area,       HitShapeKind::Capsule, OnHit::None    },
        { CardEffect::ExplodingBolt,    "explodingbolt",    AttackForm::Projectile, HitShapeKind::Circle,  OnHit::Explode },
        { CardEffect::PiercingShot,     "piercingshot",     AttackForm::Projectile, HitShapeKind::Circle,  OnHit::Pierce  },
        { CardEffect::RandomDamageShot, "randomdamageshot", AttackForm::Projectile, HitShapeKind::Circle,  OnHit::Random  },
    };
    static_assert(std::size(kEffectSpecs) == static_cast<std::size_t>(CardEffect::Count), "one spec per CardEffect");
    constexpr bool EffectSpecsInOrder()
    {
        for (std::size_t i = 0; i < std::size(kEffectSpecs); ++i)
            if (static_cast<std::size_t>(kEffectSpecs[i].effect) != i) return false;
        return true;
    }
    static_assert(EffectSpecsInOrder(), "kEffectSpecs rows must follow CardEffect order");

    [[nodiscard]] constexpr const EffectSpec& SpecOf(CardEffect effect)
    {
        return kEffectSpecs[static_cast<std::size_t>(effect)];
    }

    // Where an attack is over time (docs/circular-combat.md §2.5).
    enum class AttackPath : std::uint8_t
    {
        None,       // at the caster, instantly (Area/Nearest). A Projectile with None flies Straight.
        Straight,   // origin + aim * projectile_speed * t, ends after `range` px
        Polar,      // anchor + polar(start_radius + radial_speed*t, aim angle + angular_speed*t); radial_speed 0 = orbit
        Count
    };

    // Polar path centre: the caster's spot at fire time, or the player every step.
    enum class PathAnchor : std::uint8_t { Cast, Player };

    struct CardDef
    {
        std::string id;                  // lower-case identity: weapons.csv / characters.csv key, image names wpn_<id>_icon / prj_<id>_NN
        std::string name;                // shown in the level-up modal / HUD (font: A-Z 0-9 : - . %)
        CardKind    kind;
        CardEffect  effect;
        float       cooldown;            // seconds at level 1
        float       damage;              // per hit at level 1 (RandomDamageShot: roll lower bound)
        float       range;               // px at level 1 (area reach / bolt reach / Straight travel distance)
        int         baseTargets;         // NearestBolt: targets at level 1. Pierce: pierce count at level 1. Ignored otherwise.
        int         maxLevel;
        float       damagePerLevel;      // added per level above 1 (also added to damageMax, if used)
        float       rangePerLevel;       // added per level above 1
        float       cooldownScalePerLevel;   // multiplied once per level above 1 (<1 = faster)
        int         levelsPerExtraTarget;    // NearestBolt/Pierce: +1 target/pierce every N levels (0 = never)
        float       coneHalfAngleDeg;    // Arc shape: half-angle of the fan, degrees
        float       lineHalfWidth;       // Capsule shape: half-width of the line, px
        float       projectileSpeed;     // Straight path: travel speed, px/s
        float       explodeRadius;       // Explode: blast radius
        float       damageMax;           // Random: roll upper bound at level 1 (0 = unused)
        // Overflow (docs §3.4 "최대 레벨 시 '오버플로우' 스탯 소량 추가"): once
        // this weapon is at maxLevel, the level-up pool offers a flat add to
        // `overflowStat` instead of a level it no longer has.
        StatId      overflowStat{ StatId::WeaponDamage };
        float       overflowValue{ 0.0f };   // 0 = no bonus configured (weapon never offered as overflow)

        // --- look (docs/circular-combat.md §2.3/§2.4) ---
        std::uint32_t color{ 0xFFFFFF }; // 0xRRGGBB - outline / projectile placeholder colour until sprites (M7)
        float       hitRadius{ 6.0f };   // moving attack's own radius px (projectile size, moving-area shape size)
        float       visualScale{ 1.0f }; // drawn size = hit shape x this (1 = what you see is what hits)
        std::string sprite{};            // frame prefix while moving (M7), "" = shape placeholder
        std::string fxHit{};             // hit effect prefix (M7), "" = none

        // --- movement (docs/circular-combat.md §2.5) ---
        AttackPath  path{ AttackPath::None };
        PathAnchor  anchor{ PathAnchor::Cast };
        int         count{ 1 };          // Polar: attacks per cast, spread evenly (+ extra_projectiles)
        float       lifetime{ 0.0f };    // s; > 0 ends the attack by time instead of by range (required for Polar)
        float       startRadius{ 0.0f }; // Polar
        float       radialSpeed{ 0.0f }; // Polar, px/s
        float       angularSpeedDeg{ 0.0f };   // Polar, deg/s
        float       tickInterval{ 0.0f };      // moving Area: s between hits along the path
        float       rehitInterval{ 0.0f };     // Projectile: s before the same mob can be hit again (0 = never)
    };

    // A Projectile always moves; everything else only moves when told to.
    [[nodiscard]] inline AttackPath EffectivePath(const CardDef& def)
    {
        if (def.path == AttackPath::None && SpecOf(def.effect).form == AttackForm::Projectile) return AttackPath::Straight;
        return def.path;
    }

    // Compile-time fallback/default set - CircularBalance::weapons (loaded from
    // weapons.csv, overlaying this) is what the game actually reads at runtime.
    // Not constexpr: CardDef has std::string members, which weapons.csv-loaded
    // entries need to own. Keep these numbers equal to weapons.csv's rows
    // (docs/circular-combat.md S10 - the one allowed duplicate).
    inline const std::array<CardDef, 7> kCardDefs{ {
        // id       name      kind              effect                          cd     dmg    range  base max dmg/L rng/L cdScale extraEvery coneDeg lineW projSpd explodeR dmgMax  overflowStat              overflowValue color     hitR
        { "pulse",  "PULSE",  CardKind::Attack, CardEffect::RadialPulse,      0.6f,  12.0f, 120.0f, 0,   5,  6.0f, 14.0f, 0.95f, 0,         0.0f,   0.0f, 0.0f,   0.0f,   0.0f,  StatId::AttackSize,       0.05f, 0xFFD91A, 0.0f },
        { "bolt",   "BOLT",   CardKind::Attack, CardEffect::NearestBolt,      0.9f,  22.0f, 320.0f, 1,   5,  8.0f, 20.0f, 0.93f, 2,         0.0f,   0.0f, 0.0f,   0.0f,   0.0f,  StatId::ExtraProjectiles, 1.0f,  0xF21F1A, 7.0f },
        { "sword",  "SWORD",  CardKind::Attack, CardEffect::ArcSwing,         0.55f, 14.0f, 130.0f, 0,   5,  7.0f, 12.0f, 0.95f, 0,         70.0f,  0.0f, 0.0f,   0.0f,   0.0f,  StatId::AttackSize,       0.05f, 0xFFD91A, 0.0f },
        { "whip",   "WHIP",   CardKind::Attack, CardEffect::LineSwing,        0.70f, 10.0f, 170.0f, 0,   5,  5.0f, 16.0f, 0.94f, 0,         0.0f,   28.0f,0.0f,   0.0f,   0.0f,  StatId::AttackSize,       0.05f, 0xF21F1A, 0.0f },
        { "staff",  "STAFF",  CardKind::Attack, CardEffect::ExplodingBolt,    0.85f, 16.0f, 360.0f, 0,   5,  6.0f, 18.0f, 0.94f, 0,         0.0f,   0.0f, 620.0f, 70.0f,  0.0f,  StatId::WeaponDamage,     0.05f, 0xFF8C26, 6.0f },
        { "dagger", "DAGGER", CardKind::Attack, CardEffect::PiercingShot,     0.65f, 24.0f, 300.0f, 2,   5,  6.0f, 14.0f, 0.95f, 3,         0.0f,   0.0f, 900.0f, 0.0f,   0.0f,  StatId::CritChance,       0.02f, 0xCCCCD9, 6.0f },
        { "trump",  "TRUMP",  CardKind::Attack, CardEffect::RandomDamageShot, 0.75f, 6.0f,  280.0f, 0,   5,  3.0f, 12.0f, 0.94f, 0,         0.0f,   0.0f, 760.0f, 0.0f,   34.0f, StatId::Luck,             2.0f,  0xBF59F2, 6.0f },
    } };

    // Upper bound for NearestBolt targets - MobField::DamageNearest's fixed
    // scratch size. A card's evaluated target count is clamped to this.
    inline constexpr int kMaxBoltTargets = 8;

    [[nodiscard]] inline float CardDamage(const CardDef& def, int level)
    {
        return def.damage + def.damagePerLevel * static_cast<float>(level - 1);
    }

    // RandomDamageShot only - the roll's upper bound (CardDamage is the lower
    // bound). damagePerLevel shifts both up together, keeping the spread.
    [[nodiscard]] inline float CardDamageMax(const CardDef& def, int level)
    {
        return def.damageMax + def.damagePerLevel * static_cast<float>(level - 1);
    }

    [[nodiscard]] inline float CardRange(const CardDef& def, int level)
    {
        return def.range + def.rangePerLevel * static_cast<float>(level - 1);
    }

    [[nodiscard]] inline float CardCooldown(const CardDef& def, int level)
    {
        float cooldown = def.cooldown;
        for (int i = 1; i < level; ++i) cooldown *= def.cooldownScalePerLevel;
        return cooldown;
    }

    // NearestBolt: target count. PiercingShot: pierce count (same formula,
    // reused rather than duplicated - both are "baseTargets + 1 per N levels").
    [[nodiscard]] inline int CardTargets(const CardDef& def, int level)
    {
        int targets = def.baseTargets;
        if (def.levelsPerExtraTarget > 0) targets += (level - 1) / def.levelsPerExtraTarget;
        return targets < kMaxBoltTargets ? targets : kMaxBoltTargets;
    }

    // One owned card. `defIndex` indexes CircularBalance::weapons. F5 (mid-run
    // reload) re-parses weapons.csv - if that edit reorders/removes rows, an
    // already-owned card's defIndex can end up pointing at a different weapon
    // for the rest of that run (docs/circular-balance.md - known caveat, not
    // guarded against; F6 restarts clean).
    struct CardInstance
    {
        std::uint8_t defIndex{ 0 };
        int          level{ 1 };
        float        cooldownLeft{ 0.0f };
    };

    // Growth that is not a weapon (docs §3): a stat card piles `amount` onto one
    // stat - `multiplicative` = a fraction of the final value (0.10 = +10%),
    // otherwise a flat add in the stat's own unit. Simulation::RecomputeStats
    // turns the picked cards into StatModifiers (game/Stats.h).
    struct StatCardDef
    {
        const char* label;          // level-up button text (font: A-Z 0-9 : - . %)
        StatId      stat;
        bool        multiplicative;
        float       amount;
    };

    inline constexpr std::array<StatCardDef, 9> kStatCards{ {
        { "MOVE SPEED",    StatId::MoveSpeed,    true,  0.10f },
        { "XP GAIN",       StatId::XpGain,       true,  0.15f },
        { "ATTACK SPEED",  StatId::AttackSpeed,  true,  0.08f },
        { "ATTACK SIZE",   StatId::AttackSize,   true,  0.10f },
        { "WEAPON DAMAGE", StatId::WeaponDamage, true,  0.10f },
        { "VIT",           StatId::Vit,          false, 2.0f  },
        { "INT",           StatId::Int,          false, 2.0f  },
        { "COR",           StatId::Cor,          false, 2.0f  },
        { "AGI",           StatId::Agi,          false, 2.0f  },
    } };

    // One option in the level-up modal. `id` = weapon index (Balance().weapons)
    // for the Card/OverflowWeapon types, accessory index (Balance().accessories)
    // for the Accessory/OverflowAccessory types, kStatCards index for Stat.
    // Overflow (docs §3.4): offered instead of Upgrade* once that weapon/
    // accessory is at max level - a flat stat add, same "permanent bucket" as
    // a Stat pick (Simulation::m_cardMods), since the source item has no more
    // levels to track it against.
    struct LevelChoice
    {
        enum class Type : std::uint8_t
        {
            NewCard, UpgradeCard, NewAccessory, UpgradeAccessory, Stat, OverflowWeapon, OverflowAccessory
        };
        Type         type{ Type::Stat };
        std::uint8_t id{ 0 };
    };
}
