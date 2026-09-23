#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "game/Card.h"
#include "game/Stats.h"

// Designer-tunable numbers for the Circular scene - mob stats, spawn pacing,
// XP per level - loaded from CSV files under assets/data/circular/
// (docs/circular-balance.md). Compile-time defaults come from CircularConfig.h,
// so a missing or broken file never leaves the game without numbers.
//
// Plain data + evaluators, no simulation state: Simulation owns one instance,
// reloads it on scene entry and on F5 (Application), and reads it every step.
namespace engine::game
{
    struct SpawnPoint
    {
        float time{ 0.0f };             // seconds since the run started
        float spawnsPerSecond{ 0.0f };
        float maxAlive{ 0.0f };         // soft cap on live mobs (clamped to MobField capacity)
    };

    // player.csv (key,value) - movement / stamina numbers (docs/circular-design.md §2.2).
    // Stat multipliers (move_speed, stamina_*, dash_*) from stats.csv apply on top.
    struct PlayerTuning
    {
        float walkSpeed{ 300.0f };          // px/s
        float runMul{ 1.6f };               // speed x while running
        float runCostPerSec{ 15.0f };       // stamina/s while running
        float runResumeStamina{ 10.0f };    // after running dry, run is locked until stamina reaches this
        float dashSpeedMul{ 3.5f };         // speed x during the dash
        float dashDuration{ 0.18f };        // s of dash movement = s of invulnerability
        float dashCost{ 30.0f };            // stamina per dash (before chain penalty / dash_cost_mul)
        float dashCooldown{ 0.5f };         // s between dash starts (x dash_cooldown_mul)
        float dashChainWindow{ 2.0f };      // s: another dash inside this window counts as a chain
        float dashChainPenalty{ 0.5f };     // extra cost fraction per earlier dash in the chain
        float staminaRegenPerSec{ 30.0f };
        float staminaRegenDelay{ 0.8f };    // s after the last spend before regen starts
        // Body (world units). The hitbox is the player in gameplay terms - movement box,
        // collision, attack origin, what mobs chase. The sprite is only drawn, centred
        // horizontally, its bottom edge `hitboxLift` below the hitbox's bottom edge (the
        // collider sits that far up from the image's bottom - docs/circular-art-guide.md §4).
        float spriteWidth{ 64.0f };
        float spriteHeight{ 128.0f };
        float hitboxWidth{ 48.0f };
        float hitboxHeight{ 72.0f };
        float hitboxLift{ 8.0f };
        // After taking a hit the player can't be hurt again for this long (s) -
        // a swarm of touching mobs costs one hit per window, not one per mob.
        float hurtInvuln{ 0.5f };
    };

    // mobs.csv - one row = one mob kind (docs/circular-design.md §5.3). Behaviour
    // is data, not a per-class code path: keep_distance 0 = chase (melee/tank),
    // > 0 = stop that far out (ranged/caster); an `attack` row makes it cast.
    // `mobClass` is the base design's category (§5.1) - stage mob pools (M6) and
    // placeholder colours read it; the movement/attack code never does.
    enum class MobClass : std::uint8_t { Melee, Tank, Ranged, Caster };

    struct MobDef
    {
        std::string id;                   // lower-case identity (image names mob_<id>_NN, M7)
        std::string name;                 // ASCII
        MobClass    mobClass{ MobClass::Melee };
        float       health{ 20.0f };
        float       speed{ 90.0f };       // px/s
        float       radius{ 10.0f };      // px: collision + drawn half-size
        float       contactDamage{ 0.0f };   // per touch (player hurt i-frames gate the rate)
        float       xp{ 1.0f };           // per kill, before xp_gain
        float       weight{ 1.0f };       // share of the ring spawn mix (0 = never spawns there)
        std::uint32_t color{ 0xD04050 };  // 0xRRGGBB placeholder until sprites (M7)
        float       keepDistance{ 0.0f }; // px: 0 = chase the player, > 0 = hold at this range
        std::string attackId;             // mob_attacks.csv id, "" = no attack
        int         attack{ -1 };         // attackId resolved into CircularBalance::mobAttacks, -1 = none
        float       attackRange{ 0.0f };  // px: casts only while the player is this close
    };

    // characters.csv - one row = one playable character (docs/circular-design.md §2.3).
    struct CharacterDef
    {
        std::string id;
        std::string name;                    // ASCII: the HUD font has no Hangul yet
        StatId      mainStat{ StatId::Vit }; // one of the four base stats
        std::array<float, kBaseStatCount> start{};   // starting vit / int / cor / agi
        std::uint8_t startWeapon{ 0 };       // index into CircularBalance::weapons
    };

    // accessories.csv - one row = one passive accessory (docs/circular-design.md
    // §3.3). No effect code, unlike a weapon - a pure StatModifiers contribution
    // (add or mul, same shape as the level-up stat cards it replaces in the
    // level-up pool once owned). `amount` applies once per level (level N = N x
    // amount), simplest "more levels = more of it" curve.
    struct AccessoryDef
    {
        std::string name;                 // shown in the level-up modal / HUD (font: A-Z 0-9 : - . %)
        StatId      stat{ StatId::Luck };
        bool        multiplicative{ false };
        float       amount{ 0.0f };       // per level: flat add in the stat's unit, or a fraction if multiplicative
        int         maxLevel{ 5 };
    };

    // One owned accessory. `defIndex` indexes CircularBalance::accessories.
    struct AccessoryInstance
    {
        std::uint8_t defIndex{ 0 };
        int          level{ 1 };
    };

    struct CircularBalance
    {
        // balance.csv (key,value) - run-wide values only; per-mob numbers live in mobs.csv
        float spawnRadius{ 640.0f };       // px from the player
        float xpGrowthAfterTable{ 1.35f }; // XP needed keeps multiplying by this past the last levels.csv row

        // levels.csv: xpToNext[i] = XP to go from level i+1 to i+2
        std::vector<float> xpToNext;

        // spawn_curve.csv: sorted by time, linearly interpolated, last row held
        std::vector<SpawnPoint> spawnCurve;

        PlayerTuning player;                          // player.csv
        StatDefTable stats{ kStatDefs };              // stats.csv overlays the defaults in Stats.h
        std::vector<CharacterDef> characters;         // characters.csv (never empty after Defaults/Load)
        std::vector<CardDef> weapons{ kCardDefs.begin(), kCardDefs.end() };   // weapons.csv (never empty)
        std::vector<AccessoryDef> accessories;        // accessories.csv (may be empty - accessories are optional content)
        std::vector<CardDef> mobAttacks;              // mob_attacks.csv - enemy attacks, weapons.csv's columns (docs/circular-combat.md §5)
        std::vector<MobDef> mobs;                     // mobs.csv (never empty; index = MobField type, <= 255 rows)

        struct SpawnRate
        {
            float perSecond{ 0.0f };
            int   maxAlive{ 0 };
        };

        // Rate and live-mob cap at `secondsIntoRun`.
        [[nodiscard]] SpawnRate SpawnAt(float secondsIntoRun) const;

        // XP needed to leave `level` (1-based). Past the table it grows by
        // xpGrowthAfterTable per level so the curve never ends.
        [[nodiscard]] float XpForLevel(int level) const;

        // Which mob kind a ring spawn is, by `weight`: `u01` in [0,1) walks the
        // cumulative weights. Returns 0 when every weight is 0.
        [[nodiscard]] std::uint8_t PickSpawnMob(float u01) const;

        // The CircularConfig.h constants as a balance set.
        [[nodiscard]] static CircularBalance Defaults();
    };

    // What LoadCircularBalance found. Missing files are warnings (defaults
    // stay); malformed rows/values are errors (that row/value is skipped,
    // the rest still applies).
    struct BalanceLoadReport
    {
        int errors{ 0 };
        int warnings{ 0 };
        std::vector<std::string> messages;   // ASCII, "file:line: text"
        [[nodiscard]] bool Clean() const { return errors == 0 && warnings == 0; }
    };

    // Resets `out` to Defaults() and overlays balance.csv / levels.csv /
    // spawn_curve.csv / player.csv / stats.csv / weapons.csv / accessories.csv /
    // characters.csv / mob_attacks.csv / mobs.csv from `directory` (resolved with core::ResolveAsset).
    // `hardMaxAlive` = MobField capacity; a spawn_curve max_alive above it is
    // clamped with a warning.
    BalanceLoadReport LoadCircularBalance(CircularBalance& out, std::size_t hardMaxAlive,
                                          std::string_view directory = "assets/data/circular");
}
