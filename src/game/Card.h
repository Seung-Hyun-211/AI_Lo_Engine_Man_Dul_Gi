#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

#include "game/Stats.h"

// Card / stat data for the 2D "Circular" scene (docs/circular-design.md §2/§3).
// Pure data + tiny evaluators - no simulation state, no 3D dependency. Adding
// a card = one CardDef row here + (only if it needs a brand-new behaviour) one
// CardEffect enumerator and its case in Simulation::ExecuteCard. The deck
// itself is a plain std::vector<CardInstance> in Simulation (§2: a deck is a
// homogeneous slot array, so entity-lifecycle-design.md §3A, not §3B).
namespace engine::game
{
    // Attack is the only kind implemented; Summon/Buff/Passive (docs §2) slot in
    // as new enumerators when their cards exist.
    enum class CardKind : std::uint8_t { Attack };

    // What a card does when its cooldown expires. Simulation::ExecuteCard
    // switches on this - the "effect tag table" docs §2 asks for instead of an
    // if-chain keyed on card identity.
    enum class CardEffect : std::uint8_t
    {
        RadialPulse,      // damage every mob within `range` of the player (PULSE - placeholder)
        NearestBolt,      // damage the N nearest mobs within `range` (BOLT - placeholder)
        // The base design's actual 5 weapons (docs/circular-design.md §3.2) -
        // PULSE/BOLT above are the placeholders they replace (§10.1).
        ArcSwing,         // 검: fan-shaped melee swing (MobField::DamageInArc)
        LineSwing,        // 채찍: straight-line melee swing (MobField::DamageInCapsule)
        ExplodingBolt,    // 스태프: thrown projectile, AoE on contact (Simulation::Projectile)
        PiercingShot,     // 단검: thrown projectile, pierces `baseTargets` mobs
        RandomDamageShot, // 트럼프 카드: thrown projectile, damage rolled in [damage, damageMax] on hit
    };

    struct CardDef
    {
        std::string name;                // shown in the level-up modal / HUD (font: A-Z 0-9 : - . %); weapons.csv's identity key
        CardKind    kind;
        CardEffect  effect;
        float       cooldown;            // seconds at level 1
        float       damage;              // per hit at level 1 (RandomDamageShot: roll lower bound)
        float       range;               // px at level 1 (pulse radius / bolt-BOLT reach / swing reach / projectile flight distance)
        int         baseTargets;         // NearestBolt: targets at level 1. PiercingShot: pierce count at level 1. Ignored otherwise.
        int         maxLevel;
        float       damagePerLevel;      // added per level above 1 (also added to damageMax, if used)
        float       rangePerLevel;       // added per level above 1
        float       cooldownScalePerLevel;   // multiplied once per level above 1 (<1 = faster)
        int         levelsPerExtraTarget;    // NearestBolt/PiercingShot: +1 target/pierce every N levels (0 = never)
        float       coneHalfAngleDeg;    // ArcSwing only: half-angle of the fan, degrees
        float       lineHalfWidth;       // LineSwing only: half-width of the line, px
        float       projectileSpeed;     // ExplodingBolt/PiercingShot/RandomDamageShot only: travel speed, px/s
        float       explodeRadius;       // ExplodingBolt only: AoE radius on contact
        float       damageMax;           // RandomDamageShot only: roll upper bound at level 1 (0 = unused)
        // Overflow (docs §3.4 "최대 레벨 시 '오버플로우' 스탯 소량 추가"): once
        // this weapon is at maxLevel, the level-up pool offers a flat add to
        // `overflowStat` instead of a level it no longer has.
        StatId      overflowStat{ StatId::WeaponDamage };
        float       overflowValue{ 0.0f };   // 0 = no bonus configured (weapon never offered as overflow)
    };

    // Compile-time fallback/default set - CircularBalance::weapons (loaded from
    // weapons.csv, overlaying this) is what the game actually reads at runtime.
    // Not constexpr: CardDef::name is std::string, which weapons.csv-loaded
    // entries need to own (a raw pointer can't survive past the CSV row).
    inline const std::array<CardDef, 7> kCardDefs{ {
        // name      kind              effect                          cd     dmg    range  base max dmg/L rng/L cdScale extraEvery coneDeg lineW projSpd explodeR dmgMax  overflowStat              overflowValue
        { "PULSE",  CardKind::Attack, CardEffect::RadialPulse,        0.6f,  12.0f, 120.0f, 0,   5,  6.0f, 14.0f, 0.95f, 0,         0.0f,   0.0f, 0.0f,   0.0f,   0.0f,  StatId::AttackSize,       0.05f },
        { "BOLT",   CardKind::Attack, CardEffect::NearestBolt,        0.9f,  22.0f, 320.0f, 1,   5,  8.0f, 20.0f, 0.93f, 2,         0.0f,   0.0f, 0.0f,   0.0f,   0.0f,  StatId::ExtraProjectiles, 1.0f  },
        { "SWORD",  CardKind::Attack, CardEffect::ArcSwing,           0.55f, 14.0f, 130.0f, 0,   5,  7.0f, 12.0f, 0.95f, 0,         70.0f,  0.0f, 0.0f,   0.0f,   0.0f,  StatId::AttackSize,       0.05f },
        { "WHIP",   CardKind::Attack, CardEffect::LineSwing,          0.70f, 10.0f, 170.0f, 0,   5,  5.0f, 16.0f, 0.94f, 0,         0.0f,   28.0f,0.0f,   0.0f,   0.0f,  StatId::AttackSize,       0.05f },
        { "STAFF",  CardKind::Attack, CardEffect::ExplodingBolt,      0.85f, 16.0f, 360.0f, 0,   5,  6.0f, 18.0f, 0.94f, 0,         0.0f,   0.0f, 620.0f, 70.0f,  0.0f,  StatId::WeaponDamage,     0.05f },
        { "DAGGER", CardKind::Attack, CardEffect::PiercingShot,       0.65f, 24.0f, 300.0f, 2,   5,  6.0f, 14.0f, 0.95f, 3,         0.0f,   0.0f, 900.0f, 0.0f,   0.0f,  StatId::CritChance,       0.02f },
        { "TRUMP",  CardKind::Attack, CardEffect::RandomDamageShot,   0.75f, 6.0f,  280.0f, 0,   5,  3.0f, 12.0f, 0.94f, 0,         0.0f,   0.0f, 760.0f, 0.0f,   34.0f, StatId::Luck,             2.0f  },
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
