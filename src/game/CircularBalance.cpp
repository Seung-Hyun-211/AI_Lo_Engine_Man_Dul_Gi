#include "game/CircularBalance.h"

#include "core/AssetPaths.h"
#include "core/CsvFile.h"
#include "game/Card.h"
#include "game/CircularConfig.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <iterator>

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

        // The four base-doc characters (docs/circular-design.md §2.3). The main
        // stat starts high; the start weapon is a placeholder from the two
        // weapons that exist (kCardDefs: 0 = PULSE, 1 = BOLT).
        const auto character = [](const char* id, const char* name, StatId main, std::uint8_t weapon) {
            CharacterDef c;
            c.id = id;
            c.name = name;
            c.mainStat = main;
            c.start = { 3.0f, 3.0f, 3.0f, 3.0f };
            c.start[static_cast<std::size_t>(main)] = 8.0f;
            c.startWeapon = weapon;
            return c;
        };
        b.characters = {
            character("magic_knight",    "MAGIC KNIGHT",    StatId::Vit, 0),
            character("skull_magician",  "SKULL MAGICIAN",  StatId::Int, 1),
            character("succubus",        "SUCCUBUS",        StatId::Cor, 0),
            character("assassin_lizard", "ASSASSIN LIZARD", StatId::Agi, 1),
        };
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

        std::string Lower(std::string text)
        {
            for (char& c : text) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            return text;
        }

        // key -> (target, minimum allowed, inclusive)
        struct KvEntry { const char* name; float* target; float minValue; bool minInclusive; };

        // A `key,value` file (balance.csv, player.csv). Unknown keys warn, bad
        // or out-of-range values are errors that keep the default.
        void LoadKeyValueFile(Loader& loader, const std::string& dir, const std::string& file,
                              const KvEntry* entries, std::size_t entryCount)
        {
            core::CsvTable table;
            if (!loader.Open(dir, file, table)) return;
            const int keyCol = table.Column("key");
            const int valueCol = table.Column("value");
            if (keyCol < 0 || valueCol < 0)
            {
                loader.Error(file, 1, "header must contain key,value");
                return;
            }
            for (const core::CsvRow& row : table.rows)
            {
                const std::string key = Cell(row, keyCol);
                float value = 0.0f;
                if (!core::ParseFloat(Cell(row, valueCol), value))
                {
                    loader.Error(file, row.line, "value for '" + key + "' is not a number");
                    continue;
                }
                bool known = false;
                for (std::size_t i = 0; i < entryCount; ++i)
                {
                    const KvEntry& entry = entries[i];
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

    BalanceLoadReport LoadCircularBalance(CircularBalance& out, std::size_t hardMaxAlive, std::string_view directory)
    {
        out = CircularBalance::Defaults();
        BalanceLoadReport report;
        Loader loader{ report };
        const std::string dir(directory);

        // ---- balance.csv: key,value ----
        {
            const KvEntry entries[] = {
                { "mob_health",            &out.mobHealth,          0.0f, false },
                { "mob_speed",             &out.mobSpeed,           0.0f, true  },
                { "mob_radius",            &out.mobRadius,          0.0f, false },
                { "mob_xp",                &out.mobXp,              0.0f, true  },
                { "spawn_radius",          &out.spawnRadius,        0.0f, false },
                { "xp_growth_after_table", &out.xpGrowthAfterTable, 1.0f, true  },
            };
            LoadKeyValueFile(loader, dir, "balance.csv", entries, std::size(entries));
        }

        // ---- player.csv: key,value ----
        {
            PlayerTuning& p = out.player;
            const KvEntry entries[] = {
                { "walk_speed",           &p.walkSpeed,          0.0f, false },
                { "run_mul",              &p.runMul,             1.0f, true  },
                { "run_cost_per_sec",     &p.runCostPerSec,      0.0f, true  },
                { "run_resume_stamina",   &p.runResumeStamina,   0.0f, true  },
                { "dash_speed_mul",       &p.dashSpeedMul,       1.0f, true  },
                { "dash_duration",        &p.dashDuration,       0.0f, false },
                { "dash_cost",            &p.dashCost,           0.0f, true  },
                { "dash_cooldown",        &p.dashCooldown,       0.0f, true  },
                { "dash_chain_window",    &p.dashChainWindow,    0.0f, true  },
                { "dash_chain_penalty",   &p.dashChainPenalty,   0.0f, true  },
                { "stamina_regen_per_sec",&p.staminaRegenPerSec, 0.0f, true  },
                { "stamina_regen_delay",  &p.staminaRegenDelay,  0.0f, true  },
            };
            LoadKeyValueFile(loader, dir, "player.csv", entries, std::size(entries));
        }

        // ---- stats.csv: stat_id + optional base_value,min,max,from_vit,from_int,from_cor,from_agi ----
        {
            const std::string file = "stats.csv";
            core::CsvTable table;
            if (loader.Open(dir, file, table))
            {
                const int idCol = table.Column("stat_id");
                if (idCol < 0)
                {
                    loader.Error(file, 1, "header must contain stat_id");
                }
                else
                {
                    // Column name -> which StatDef field it overwrites (absent column = keep default).
                    struct Column { const char* name; float StatDef::* field; int fromIndex; };
                    const Column columns[] = {
                        { "base_value", &StatDef::baseValue, -1 }, { "min", &StatDef::minValue, -1 },
                        { "max", &StatDef::maxValue, -1 },
                        { "from_vit", nullptr, 0 }, { "from_int", nullptr, 1 },
                        { "from_cor", nullptr, 2 }, { "from_agi", nullptr, 3 },
                    };
                    for (const core::CsvRow& row : table.rows)
                    {
                        const std::string id = Lower(Cell(row, idCol));
                        StatDef* def = nullptr;
                        for (StatDef& candidate : out.stats)
                            if (id == candidate.id) def = &candidate;
                        if (def == nullptr)
                        {
                            loader.Warn(file, row.line, "unknown stat_id '" + id + "' ignored");
                            continue;
                        }
                        StatDef next = *def;
                        bool valid = true;
                        for (const Column& column : columns)
                        {
                            const int col = table.Column(column.name);
                            const std::string cell = Cell(row, col);
                            if (col < 0 || cell.empty()) continue;
                            float value = 0.0f;
                            if (!core::ParseFloat(cell, value))
                            {
                                loader.Error(file, row.line, std::string(column.name) + " for '" + id + "' is not a number - row skipped");
                                valid = false;
                                break;
                            }
                            if (column.fromIndex >= 0) next.from[column.fromIndex] = value;
                            else next.*(column.field) = value;
                        }
                        if (!valid) continue;
                        if (next.minValue > next.maxValue)
                        {
                            loader.Error(file, row.line, "min > max for '" + id + "' - row skipped");
                            continue;
                        }
                        *def = next;
                    }
                }
            }
        }

        // ---- characters.csv: id,name,main_stat,start_vit,start_int,start_cor,start_agi,start_weapon ----
        {
            const std::string file = "characters.csv";
            core::CsvTable table;
            if (loader.Open(dir, file, table))
            {
                const int idCol = table.Column("id");
                const int nameCol = table.Column("name");
                const int mainCol = table.Column("main_stat");
                const int weaponCol = table.Column("start_weapon");
                const int startCols[kBaseStatCount] = { table.Column("start_vit"), table.Column("start_int"),
                                                        table.Column("start_cor"), table.Column("start_agi") };
                bool headerOk = idCol >= 0 && nameCol >= 0 && mainCol >= 0 && weaponCol >= 0;
                for (int col : startCols) headerOk = headerOk && col >= 0;
                if (!headerOk)
                {
                    loader.Error(file, 1, "header must contain id,name,main_stat,start_vit,start_int,start_cor,start_agi,start_weapon");
                }
                else
                {
                    std::vector<CharacterDef> characters;
                    for (const core::CsvRow& row : table.rows)
                    {
                        CharacterDef c;
                        c.id = Cell(row, idCol);
                        c.name = Cell(row, nameCol);
                        if (c.id.empty() || c.name.empty())
                        {
                            loader.Error(file, row.line, "id and name are required - row skipped");
                            continue;
                        }
                        const bool duplicate = std::any_of(characters.begin(), characters.end(),
                            [&](const CharacterDef& other) { return other.id == c.id; });
                        if (duplicate)
                        {
                            loader.Error(file, row.line, "duplicate id '" + c.id + "' - row skipped");
                            continue;
                        }

                        const std::string mainName = Lower(Cell(row, mainCol));
                        int mainIndex = -1;
                        for (std::size_t i = 0; i < kBaseStatCount; ++i)
                            if (mainName == kStatDefs[i].id) mainIndex = static_cast<int>(i);
                        if (mainIndex < 0)
                        {
                            loader.Error(file, row.line, "main_stat must be vit, int, cor or agi - row skipped");
                            continue;
                        }
                        c.mainStat = static_cast<StatId>(mainIndex);

                        bool valid = true;
                        for (std::size_t i = 0; i < kBaseStatCount; ++i)
                        {
                            if (!core::ParseFloat(Cell(row, startCols[i]), c.start[i]) || c.start[i] < 0.0f)
                            {
                                loader.Error(file, row.line, "start_* must be numbers >= 0 - row skipped");
                                valid = false;
                                break;
                            }
                        }
                        if (!valid) continue;

                        const std::string weaponName = Lower(Cell(row, weaponCol));
                        int weaponIndex = -1;
                        for (std::size_t i = 0; i < kCardDefs.size(); ++i)
                            if (weaponName == Lower(kCardDefs[i].name)) weaponIndex = static_cast<int>(i);
                        if (weaponIndex < 0)
                        {
                            loader.Error(file, row.line, "start_weapon '" + weaponName + "' is not a known weapon - row skipped");
                            continue;
                        }
                        c.startWeapon = static_cast<std::uint8_t>(weaponIndex);
                        characters.push_back(std::move(c));
                    }
                    if (characters.empty()) loader.Warn(file, 0, "no valid rows - using the built-in characters");
                    else out.characters = std::move(characters);
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
