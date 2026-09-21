#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

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

    struct CircularBalance
    {
        // balance.csv (key,value)
        float mobHealth{ 20.0f };
        float mobSpeed{ 90.0f };           // px/s
        float mobRadius{ 10.0f };          // px
        float mobXp{ 1.0f };               // XP granted per kill (before the player's xpGainMul)
        float spawnRadius{ 640.0f };       // px from the player
        float xpGrowthAfterTable{ 1.35f }; // XP needed keeps multiplying by this past the last levels.csv row

        // levels.csv: xpToNext[i] = XP to go from level i+1 to i+2
        std::vector<float> xpToNext;

        // spawn_curve.csv: sorted by time, linearly interpolated, last row held
        std::vector<SpawnPoint> spawnCurve;

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
    // spawn_curve.csv from `directory` (resolved with core::ResolveAsset).
    // `hardMaxAlive` = MobField capacity; a spawn_curve max_alive above it is
    // clamped with a warning.
    BalanceLoadReport LoadCircularBalance(CircularBalance& out, std::size_t hardMaxAlive,
                                          std::string_view directory = "assets/data/circular");
}
