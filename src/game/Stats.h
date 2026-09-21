#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

// The Circular player's stat system (docs/circular-design.md §2.6): four base
// stats (vit/int/cor/agi) plus derived stats that weapons, movement and the
// dash read *by id* - no gameplay code hard-codes a number that a stat can own.
//
//   base stat   = (base_value + adds) * (1 + muls), clamped
//   derived     = (base_value + sum(from_x * base stat x) + adds) * (1 + muls), clamped
//
// "adds" / "muls" come from the character's starting values, level-up stat
// cards and (later) accessories / weapon overflow. Definitions (base value,
// clamp, per-base-stat coefficients) are the defaults below, overlaid by
// assets/data/circular/stats.csv. Pure data + evaluator, header-only.
namespace engine::game
{
    enum class StatId : std::uint8_t
    {
        // base four (order is load-bearing: StatDef::from[] is indexed by it)
        Vit, Int, Cor, Agi,
        // derived
        Luck, AttackSize, ExtraProjectiles, AttackSpeed, WeaponDamage, MoveSpeed,
        MaxHp, HpRegen, DamageReduction, CritChance, CritDamage, XpGain,
        StaminaMax, StaminaRegen, DashCostMul, DashChainPenaltyMul, DashCooldownMul,
        UltimateChargeMul, CorruptionPower,
        Count
    };

    inline constexpr std::size_t kStatCount = static_cast<std::size_t>(StatId::Count);
    inline constexpr std::size_t kBaseStatCount = 4;

    struct StatDef
    {
        const char* id;          // CSV key, lower-case
        const char* label;       // HUD text (font: A-Z 0-9 : - . %)
        float baseValue;
        float minValue;
        float maxValue;
        float from[kBaseStatCount];   // per point of vit / int / cor / agi
    };

    // Defaults [살] - the numbers are proposals; stats.csv overrides them.
    inline constexpr std::array<StatDef, kStatCount> kStatDefs{ {
        // id                       label          base   min    max     vit     int     cor     agi
        { "vit",                    "VIT",          0.0f, 0.0f, 999.0f, { 0.0f,   0.0f,   0.0f,   0.0f   } },
        { "int",                    "INT",          0.0f, 0.0f, 999.0f, { 0.0f,   0.0f,   0.0f,   0.0f   } },
        { "cor",                    "COR",          0.0f, 0.0f, 999.0f, { 0.0f,   0.0f,   0.0f,   0.0f   } },
        { "agi",                    "AGI",          0.0f, 0.0f, 999.0f, { 0.0f,   0.0f,   0.0f,   0.0f   } },
        { "luck",                   "LUCK",         0.0f, 0.0f, 100.0f, { 0.0f,   0.0f,   0.0f,   0.0f   } },
        { "attack_size",            "ATK SIZE",     1.0f, 0.25f, 4.0f,  { 0.0f,   0.0f,   0.0f,   0.0f   } },
        { "extra_projectiles",      "PROJ",         0.0f, 0.0f, 7.0f,   { 0.0f,   0.0f,   0.0f,   0.0f   } },
        { "attack_speed",           "ATK SPD",      1.0f, 0.25f, 4.0f,  { 0.0f,   0.005f, 0.0f,   0.005f } },
        { "weapon_damage",          "DAMAGE",       1.0f, 0.1f, 10.0f,  { 0.0f,   0.02f,  0.0f,   0.0f   } },
        { "move_speed",             "MOVE SPD",     1.0f, 0.5f, 3.0f,   { 0.0f,   0.0f,   0.0f,   0.01f  } },
        { "max_hp",                 "MAX HP",      50.0f, 1.0f, 9999.0f, { 6.0f,   0.0f,   0.0f,   0.0f   } },
        { "hp_regen",               "HP REGEN",     0.0f, 0.0f, 100.0f, { 0.05f,  0.0f,   0.0f,   0.0f   } },
        { "damage_reduction",       "DMG RED",      0.0f, 0.0f, 0.8f,   { 0.004f, 0.0f,   0.0f,   0.0f   } },
        { "crit_chance",            "CRIT",         0.05f, 0.0f, 1.0f,  { 0.0f,   0.0f,   0.0f,   0.0f   } },
        { "crit_damage",            "CRIT DMG",     1.5f, 1.0f, 5.0f,   { 0.0f,   0.0f,   0.0f,   0.0f   } },
        { "xp_gain",                "XP GAIN",      1.0f, 0.1f, 10.0f,  { 0.0f,   0.0f,   0.0f,   0.0f   } },
        { "stamina_max",            "STAMINA",    100.0f, 10.0f, 500.0f, { 0.0f,   0.0f,   0.0f,   2.0f   } },
        { "stamina_regen",          "STA REGEN",    1.0f, 0.1f, 5.0f,   { 0.0f,   0.0f,   0.0f,   0.01f  } },
        { "dash_cost_mul",          "DASH COST",    1.0f, 0.2f, 3.0f,   { 0.0f,   0.0f,   0.0f,  -0.005f } },
        { "dash_chain_penalty_mul", "DASH CHAIN",   1.0f, 0.0f, 3.0f,   { 0.0f,   0.0f,   0.0f,   0.0f   } },
        { "dash_cooldown_mul",      "DASH CD",      1.0f, 0.2f, 3.0f,   { 0.0f,   0.0f,   0.0f,   0.0f   } },
        { "ultimate_charge_mul",    "ULT CHARGE",   1.0f, 0.1f, 5.0f,   { 0.0f,   0.02f,  0.0f,   0.0f   } },
        { "corruption_power",       "CORRUPTION",   1.0f, 0.1f, 10.0f,  { 0.0f,   0.0f,   0.03f,  0.0f   } },
    } };

    using StatDefTable = std::array<StatDef, kStatCount>;

    // What the sources (character, cards, ...) pile onto each stat. `add` is
    // in the stat's own unit, `mul` is a fraction (0.10 = +10%).
    struct StatModifiers
    {
        std::array<float, kStatCount> add{};
        std::array<float, kStatCount> mul{};
    };

    // The evaluated numbers. Recomputed only when a source changes (dirty on
    // level-up / character pick / CSV reload), never per step.
    struct StatBlock
    {
        std::array<float, kStatCount> value{};
        [[nodiscard]] float operator[](StatId id) const { return value[static_cast<std::size_t>(id)]; }
    };

    [[nodiscard]] inline StatBlock ComputeStats(const StatDefTable& defs, const StatModifiers& mods)
    {
        StatBlock out;
        const auto clampTo = [](float v, const StatDef& def) {
            return v < def.minValue ? def.minValue : (v > def.maxValue ? def.maxValue : v);
        };
        float base[kBaseStatCount];
        for (std::size_t i = 0; i < kBaseStatCount; ++i)
        {
            base[i] = clampTo((defs[i].baseValue + mods.add[i]) * (1.0f + mods.mul[i]), defs[i]);
            out.value[i] = base[i];
        }
        for (std::size_t i = kBaseStatCount; i < kStatCount; ++i)
        {
            float v = defs[i].baseValue + mods.add[i];
            for (std::size_t b = 0; b < kBaseStatCount; ++b) v += defs[i].from[b] * base[b];
            out.value[i] = clampTo(v * (1.0f + mods.mul[i]), defs[i]);
        }
        return out;
    }
}
