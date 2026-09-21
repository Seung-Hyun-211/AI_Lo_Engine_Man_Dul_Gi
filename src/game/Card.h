#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

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
        RadialPulse,   // damage every mob within `range` of the player
        NearestBolt,   // damage the N nearest mobs within `range`
    };

    struct CardDef
    {
        const char* name;                // shown in the level-up modal / HUD (font: A-Z 0-9 : - . %)
        CardKind    kind;
        CardEffect  effect;
        float       cooldown;            // seconds at level 1
        float       damage;              // per hit at level 1
        float       range;               // px at level 1 (pulse radius / bolt reach)
        int         baseTargets;         // NearestBolt: targets at level 1 (RadialPulse ignores)
        int         maxLevel;
        float       damagePerLevel;      // added per level above 1
        float       rangePerLevel;       // added per level above 1
        float       cooldownScalePerLevel;   // multiplied once per level above 1 (<1 = faster)
        int         levelsPerExtraTarget;    // NearestBolt: +1 target every N levels (0 = never)
    };

    inline constexpr std::array<CardDef, 2> kCardDefs{ {
        // name     kind              effect                    cd    dmg   range base max  dmg/L rng/L cdScale extraEvery
        { "PULSE", CardKind::Attack, CardEffect::RadialPulse,   0.6f, 12.0f, 120.0f, 0,   5,  6.0f, 14.0f, 0.95f, 0 },
        { "BOLT",  CardKind::Attack, CardEffect::NearestBolt,   0.9f, 22.0f, 320.0f, 1,   5,  8.0f, 20.0f, 0.93f, 2 },
    } };

    // Upper bound for NearestBolt targets - MobField::DamageNearest's fixed
    // scratch size. A card's evaluated target count is clamped to this.
    inline constexpr int kMaxBoltTargets = 8;

    [[nodiscard]] inline float CardDamage(const CardDef& def, int level)
    {
        return def.damage + def.damagePerLevel * static_cast<float>(level - 1);
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

    [[nodiscard]] inline int CardTargets(const CardDef& def, int level)
    {
        int targets = def.baseTargets;
        if (def.levelsPerExtraTarget > 0) targets += (level - 1) / def.levelsPerExtraTarget;
        return targets < kMaxBoltTargets ? targets : kMaxBoltTargets;
    }

    // One owned card. `defIndex` indexes kCardDefs (stable across the run, no
    // pointer to invalidate).
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

    // One option in the level-up modal. `id` = kCardDefs index for the card
    // types, kStatCards index for Stat.
    struct LevelChoice
    {
        enum class Type : std::uint8_t { NewCard, UpgradeCard, Stat };
        Type         type{ Type::Stat };
        std::uint8_t id{ 0 };
    };
}
