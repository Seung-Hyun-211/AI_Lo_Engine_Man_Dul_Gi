#include "game/CircularBalance.h"

#include "core/AssetPaths.h"
#include "core/CsvFile.h"
#include "game/CircularConfig.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace engine::game
{
    CircularBalance CircularBalance::Defaults()
    {
        CircularBalance b;
        b.mobHealth = kActiveMob.health;
        b.mobSpeed = kActiveMob.speed;
        b.mobRadius = kActiveMob.radius;
        b.mobXp = static_cast<float>(kActiveMob.xpValue);
        b.spawnRadius = kActiveMob.spawnRadius;
        b.xpGrowthAfterTable = kProgression.xpGrowth;
        // No levels.csv table: XpForLevel falls back to baseXp * growth^(n-1).
        b.spawnCurve.push_back({ 0.0f, kActiveMob.spawnsPerSecond, static_cast<float>(kActiveMob.capacity) });
        return b;
    }

    CircularBalance::SpawnRate CircularBalance::SpawnAt(float secondsIntoRun) const
    {
        if (spawnCurve.empty()) return { 0.0f, 0 };
        const auto toRate = [](float perSecond, float maxAlive) {
            return SpawnRate{ perSecond, static_cast<int>(maxAlive + 0.5f) };
        };

        if (secondsIntoRun <= spawnCurve.front().time)
            return toRate(spawnCurve.front().spawnsPerSecond, spawnCurve.front().maxAlive);
        if (secondsIntoRun >= spawnCurve.back().time)
            return toRate(spawnCurve.back().spawnsPerSecond, spawnCurve.back().maxAlive);

        for (std::size_t i = 1; i < spawnCurve.size(); ++i)
        {
            const SpawnPoint& a = spawnCurve[i - 1];
            const SpawnPoint& b = spawnCurve[i];
            if (secondsIntoRun > b.time) continue;
            const float span = b.time - a.time;
            const float t = span > 0.0f ? (secondsIntoRun - a.time) / span : 1.0f;
            return toRate(a.spawnsPerSecond + (b.spawnsPerSecond - a.spawnsPerSecond) * t,
                          a.maxAlive + (b.maxAlive - a.maxAlive) * t);
        }
        return toRate(spawnCurve.back().spawnsPerSecond, spawnCurve.back().maxAlive);
    }

    float CircularBalance::XpForLevel(int level) const
    {
        if (level < 1) level = 1;
        const std::size_t index = static_cast<std::size_t>(level - 1);
        if (index < xpToNext.size()) return xpToNext[index];
        if (xpToNext.empty())
            return kProgression.baseXp * std::pow(xpGrowthAfterTable, static_cast<float>(level - 1));
        return xpToNext.back() * std::pow(xpGrowthAfterTable, static_cast<float>(index - (xpToNext.size() - 1)));
    }

    namespace
    {
        struct Loader
        {
            BalanceLoadReport& report;

            void Error(const std::string& file, int line, const std::string& text)
            {
                ++report.errors;
                char head[32];
                std::snprintf(head, sizeof(head), ":%d: ", line);
                report.messages.push_back("ERROR " + file + head + text);
            }

            void Warn(const std::string& file, int line, const std::string& text)
            {
                ++report.warnings;
                char head[32];
                std::snprintf(head, sizeof(head), ":%d: ", line);
                report.messages.push_back("WARN  " + file + (line > 0 ? head : ": ") + text);
            }

            // Opens `file` under `dir`; false (with a warning) when it is absent.
            bool Open(const std::string& dir, const std::string& file, core::CsvTable& table)
            {
                std::string error;
                if (core::LoadCsv(core::ResolveAsset(dir + "/" + file), table, error)) return true;
                Warn(file, 0, error + " - using defaults");
                return false;
            }
        };

        // Cell `column` of `row`, or "" when the row is short.
        std::string Cell(const core::CsvRow& row, int column)
        {
            return column >= 0 && static_cast<std::size_t>(column) < row.cells.size() ? row.cells[static_cast<std::size_t>(column)] : std::string{};
        }
    }

    BalanceLoadReport LoadCircularBalance(CircularBalance& out, std::size_t hardMaxAlive, std::string_view directory)
    {
        out = CircularBalance::Defaults();
        BalanceLoadReport report;
        Loader loader{ report };
        const std::string dir(directory);

        // ---- balance.csv: key,value ----
        {
            const std::string file = "balance.csv";
            core::CsvTable table;
            if (loader.Open(dir, file, table))
            {
                const int keyCol = table.Column("key");
                const int valueCol = table.Column("value");
                if (keyCol < 0 || valueCol < 0)
                {
                    loader.Error(file, 1, "header must contain key,value");
                }
                else
                {
                    for (const core::CsvRow& row : table.rows)
                    {
                        const std::string key = Cell(row, keyCol);
                        float value = 0.0f;
                        if (!core::ParseFloat(Cell(row, valueCol), value))
                        {
                            loader.Error(file, row.line, "value for '" + key + "' is not a number");
                            continue;
                        }

                        // key -> (target, minimum allowed, inclusive)
                        struct Entry { const char* name; float* target; float minValue; bool minInclusive; };
                        const Entry entries[] = {
                            { "mob_health",           &out.mobHealth,          0.0f, false },
                            { "mob_speed",            &out.mobSpeed,           0.0f, true  },
                            { "mob_radius",           &out.mobRadius,          0.0f, false },
                            { "mob_xp",               &out.mobXp,              0.0f, true  },
                            { "spawn_radius",         &out.spawnRadius,        0.0f, false },
                            { "xp_growth_after_table",&out.xpGrowthAfterTable, 1.0f, true  },
                        };
                        bool known = false;
                        for (const Entry& entry : entries)
                        {
                            if (key != entry.name) continue;
                            known = true;
                            const bool ok = entry.minInclusive ? value >= entry.minValue : value > entry.minValue;
                            if (!ok) loader.Error(file, row.line, "'" + key + "' out of range - keeping default");
                            else *entry.target = value;
                        }
                        if (!known) loader.Warn(file, row.line, "unknown key '" + key + "' ignored");
                    }
                }
            }
        }

        // ---- levels.csv: level,xp_to_next ----
        {
            const std::string file = "levels.csv";
            core::CsvTable table;
            if (loader.Open(dir, file, table))
            {
                const int levelCol = table.Column("level");
                const int xpCol = table.Column("xp_to_next");
                if (levelCol < 0 || xpCol < 0)
                {
                    loader.Error(file, 1, "header must contain level,xp_to_next");
                }
                else
                {
                    std::vector<float> xp;
                    for (const core::CsvRow& row : table.rows)
                    {
                        float level = 0.0f;
                        float needed = 0.0f;
                        if (!core::ParseFloat(Cell(row, levelCol), level) || !core::ParseFloat(Cell(row, xpCol), needed))
                        {
                            loader.Error(file, row.line, "level / xp_to_next must be numbers");
                            continue;
                        }
                        if (static_cast<int>(level) != static_cast<int>(xp.size()) + 1)
                        {
                            loader.Error(file, row.line, "levels must run 1,2,3... in order (expected " +
                                                             std::to_string(xp.size() + 1) + ") - row skipped");
                            continue;
                        }
                        if (needed <= 0.0f)
                        {
                            loader.Error(file, row.line, "xp_to_next must be > 0 - row skipped");
                            continue;
                        }
                        xp.push_back(needed);
                    }
                    if (xp.empty()) loader.Warn(file, 0, "no valid rows - using the default XP formula");
                    else out.xpToNext = std::move(xp);
                }
            }
        }

        // ---- spawn_curve.csv: time_sec,spawns_per_sec,max_alive ----
        {
            const std::string file = "spawn_curve.csv";
            core::CsvTable table;
            if (loader.Open(dir, file, table))
            {
                const int timeCol = table.Column("time_sec");
                const int rateCol = table.Column("spawns_per_sec");
                const int capCol = table.Column("max_alive");
                if (timeCol < 0 || rateCol < 0 || capCol < 0)
                {
                    loader.Error(file, 1, "header must contain time_sec,spawns_per_sec,max_alive");
                }
                else
                {
                    std::vector<SpawnPoint> curve;
                    for (const core::CsvRow& row : table.rows)
                    {
                        SpawnPoint point;
                        if (!core::ParseFloat(Cell(row, timeCol), point.time) ||
                            !core::ParseFloat(Cell(row, rateCol), point.spawnsPerSecond) ||
                            !core::ParseFloat(Cell(row, capCol), point.maxAlive))
                        {
                            loader.Error(file, row.line, "time_sec / spawns_per_sec / max_alive must be numbers");
                            continue;
                        }
                        if (point.time < 0.0f || point.spawnsPerSecond < 0.0f || point.maxAlive < 0.0f)
                        {
                            loader.Error(file, row.line, "values must be >= 0 - row skipped");
                            continue;
                        }
                        if (!curve.empty() && point.time <= curve.back().time)
                        {
                            loader.Error(file, row.line, "time_sec must increase row to row - row skipped");
                            continue;
                        }
                        if (point.maxAlive > static_cast<float>(hardMaxAlive))
                        {
                            loader.Warn(file, row.line, "max_alive above MobField capacity " +
                                                            std::to_string(hardMaxAlive) + " - clamped");
                            point.maxAlive = static_cast<float>(hardMaxAlive);
                        }
                        curve.push_back(point);
                    }
                    if (curve.empty()) loader.Warn(file, 0, "no valid rows - using the default spawn rate");
                    else out.spawnCurve = std::move(curve);
                }
            }
        }

        return report;
    }
}
