#pragma once

#include <cstddef>
#include <cstdint>

// How the 2D "Circular" scene's mob swarm is sized (docs/circular-design.md).
// Built with the 2D baseline only - no ENGINE_WITH_3D dependency, unlike
// CrowdConfig.h's 3D crowd. Mirrors that file's "one preset, one knob" shape.
namespace engine::game
{
    struct MobConfig
    {
        std::size_t capacity;                // MobField slot count - hard cap on live mobs
        float       radius;                  // collision + render half-extent, pixels
        float       speed;                   // pixels/second, seek-toward-player
        float       health;
        int         xpValue;                 // experience granted on death (future use - no XP/level system yet)
        float       spawnsPerSecond;         // spawn rate while below capacity (a fixed step may spawn several)
        float       spawnRadius;             // spawn ring distance from the player, pixels
    };

    // v1: a single homogeneous mob type, matching docs/circular-design.md
    // §10 step 1's scope ("이동 + 쿨다운형 카드 자동발동 + 경험치/레벨업 기본
    // 루프") - no per-type table yet (YAGNI, same call as CrowdConfig.h's
    // kCrowdZombies being the only preset before a second type was needed).
    inline constexpr MobConfig kActiveMob{
        /*capacity*/ 4096, /*radius*/ 10.0f, /*speed*/ 90.0f, /*health*/ 20.0f,
        /*xpValue*/ 1, /*spawnsPerSecond*/ 100.0f, /*spawnRadius*/ 640.0f };

    // XP / level-up / deck limits (docs/circular-design.md §3/§10). XP needed
    // for level n -> n+1 is baseXp * xpGrowth^(n-1).
    struct ProgressionConfig
    {
        float         baseXp;           // level 1 -> 2
        float         xpGrowth;         // multiplier per level
        int           maxDeckSlots;     // docs §2: 6~8
        int           choiceCount;      // options offered per level-up (stat cards live in Card.h kStatCards)
        std::uint32_t rngSeed;         // level-up option RNG; fixed so runs replay the same given the same inputs
    };

    inline constexpr ProgressionConfig kProgression{
        /*baseXp*/ 30.0f, /*xpGrowth*/ 1.35f, /*maxDeckSlots*/ 6, /*choiceCount*/ 3,
        /*rngSeed*/ 0x51C0FFEEu };

    // Telegraphed charge pattern (docs/circular-design.md "돌진 패턴"): a red
    // square is marked where the player stands, the chosen mobs freeze and
    // flash for warnSeconds, then dash in a straight line at that square.
    // Mobs outside the square are the ones picked (chargeFraction of those at
    // least minDistance away) - the square is the *landing* zone, so the
    // player dodges by leaving it.
    struct ChargePatternConfig
    {
        float firstDelaySeconds;   // scene start -> first marking
        float intervalSeconds;     // end of one charge -> next marking
        float warnSeconds;         // red square visible (and picked mobs frozen) this long
        float zoneHalfSize;        // square half-extent, pixels
        float chargeFraction;      // 0..1 share of eligible mobs picked
        float minDistance;         // only mobs at least this far from the zone centre are eligible
        float chargeSpeed;         // pixels/second while charging
        float chargeDuration;      // seconds a charge lasts (overshoots the zone on purpose)
    };

    inline constexpr ChargePatternConfig kChargePattern{
        /*firstDelay*/ 4.0f, /*interval*/ 8.0f, /*warn*/ 1.5f, /*zoneHalfSize*/ 170.0f,
        /*fraction*/ 0.5f, /*minDistance*/ 220.0f, /*speed*/ 360.0f, /*duration*/ 2.0f };
}
