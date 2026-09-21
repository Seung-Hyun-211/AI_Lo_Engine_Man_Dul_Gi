#pragma once

#include <cstddef>

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
        float       spawnIntervalSeconds;    // one mob spawned this often while below capacity
        float       spawnRadius;             // spawn ring distance from the player, pixels
    };

    // v1: a single homogeneous mob type, matching docs/circular-design.md
    // §10 step 1's scope ("이동 + 쿨다운형 카드 자동발동 + 경험치/레벨업 기본
    // 루프") - no per-type table yet (YAGNI, same call as CrowdConfig.h's
    // kCrowdZombies being the only preset before a second type was needed).
    inline constexpr MobConfig kActiveMob{
        /*capacity*/ 4096, /*radius*/ 10.0f, /*speed*/ 90.0f, /*health*/ 20.0f,
        /*xpValue*/ 1, /*spawnIntervalSeconds*/ 0.15f, /*spawnRadius*/ 640.0f };
}
