#include "game/CircularBalance.h"

#include "core/AssetPaths.h"
#include "core/CsvFile.h"
#include "game/Card.h"
#include "game/CircularConfig.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
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
        // stat starts high; the start weapon is a [살] pairing with the base
        // design's 5 weapons (§3.2, kCardDefs: 2=SWORD 3=WHIP 4=STAFF 5=DAGGER
        // 6=TRUMP) - by concept fit, not confirmed. TRUMP has no starting
        // owner (level-up pool only).
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
            character("magic_knight",    "MAGIC KNIGHT",    StatId::Vit, 2),   // SWORD
            character("skull_magician",  "SKULL MAGICIAN",  StatId::Int, 4),   // STAFF
            character("succubus",        "SUCCUBUS",        StatId::Cor, 3),   // WHIP
            character("assassin_lizard", "ASSASSIN LIZARD", StatId::Agi, 5),   // DAGGER
        };

        // Built-in accessories (docs/circular-design.md §3.3) - [살] proposals,
        // same "level N = N x amount" curve as weapons' per-level scaling.
        const auto accessory = [](const char* name, StatId stat, bool mul, float amount) {
            return AccessoryDef{ name, stat, mul, amount, 5 };
        };
        b.accessories = {
            accessory("AMULET",    StatId::MoveSpeed,   true,  0.08f),
            accessory("CHARM",     StatId::Luck,         false, 3.0f),
            accessory("RING",      StatId::MaxHp,        false, 10.0f),
            accessory("BLOODSTONE",StatId::LifeSteal,    false, 0.04f),
            accessory("GAUNTLET",  StatId::WeaponDamage, true,  0.08f),
            accessory("BELT",      StatId::StaminaMax,   false, 15.0f),
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

        // weapons.csv id: lower-case letters, digits, underscore (it becomes part of
        // image file names - docs/circular-art-guide.md naming rules).
        bool IsIdentifier(const std::string& text)
        {
            if (text.empty()) return false;
            return std::all_of(text.begin(), text.end(), [](char c) {
                return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_';
            });
        }

        // "RRGGBB" or "#RRGGBB" -> 0xRRGGBB.
        bool ParseHexColor(std::string text, std::uint32_t& out)
        {
            if (!text.empty() && text[0] == '#') text.erase(0, 1);
            if (text.size() != 6 || !std::all_of(text.begin(), text.end(), [](char c) { return std::isxdigit(static_cast<unsigned char>(c)) != 0; }))
                return false;
            out = static_cast<std::uint32_t>(std::strtoul(text.c_str(), nullptr, 16));
            return true;
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
                { "sprite_width",         &p.spriteWidth,        0.0f, false },
                { "sprite_height",        &p.spriteHeight,       0.0f, false },
                { "hitbox_width",         &p.hitboxWidth,        0.0f, false },
                { "hitbox_height",        &p.hitboxHeight,       0.0f, false },
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

        // ---- weapons.csv: one row = one weapon (docs/circular-combat.md §2.4 is the column list) ----
        // id,name,effect,cooldown,damage,range,base_targets,max_level,damage_per_level,
        // range_per_level,cooldown_scale,levels_per_extra_target are required; the
        // rest are optional (blank = the CardDef default, unused by that effect).
        {
            const std::string file = "weapons.csv";
            core::CsvTable table;
            if (loader.Open(dir, file, table))
            {
                const int idCol = table.Column("id");
                const int nameCol = table.Column("name");
                const int effectCol = table.Column("effect");
                const int cdCol = table.Column("cooldown");
                const int dmgCol = table.Column("damage");
                const int rangeCol = table.Column("range");
                const int baseTargetsCol = table.Column("base_targets");
                const int maxLevelCol = table.Column("max_level");
                const int dmgPerLevelCol = table.Column("damage_per_level");
                const int rangePerLevelCol = table.Column("range_per_level");
                const int cdScaleCol = table.Column("cooldown_scale");
                const int extraEveryCol = table.Column("levels_per_extra_target");
                const int overflowStatCol = table.Column("overflow_stat");
                const int overflowValueCol = table.Column("overflow_value");
                const int colorCol = table.Column("color");
                const int spriteCol = table.Column("sprite");
                const int fxHitCol = table.Column("fx_hit");
                const int pathCol = table.Column("path");
                const int anchorCol = table.Column("anchor");
                const int countCol = table.Column("count");

                bool headerOk = idCol >= 0 && nameCol >= 0 && effectCol >= 0 && cdCol >= 0 && dmgCol >= 0 && rangeCol >= 0 &&
                                baseTargetsCol >= 0 && maxLevelCol >= 0 && dmgPerLevelCol >= 0 &&
                                rangePerLevelCol >= 0 && cdScaleCol >= 0 && extraEveryCol >= 0;
                if (!headerOk)
                {
                    loader.Error(file, 1, "header must contain id,name,effect,cooldown,damage,range,base_targets,"
                                          "max_level,damage_per_level,range_per_level,cooldown_scale,"
                                          "levels_per_extra_target (the rest are optional)");
                }
                else
                {
                    std::vector<CardDef> weapons;
                    for (const core::CsvRow& row : table.rows)
                    {
                        CardDef def{};
                        def.kind = CardKind::Attack;
                        const std::string id = Lower(Cell(row, idCol));
                        const std::string name = Cell(row, nameCol);
                        if (!IsIdentifier(id) || name.empty())
                        {
                            loader.Error(file, row.line, "id (a-z 0-9 _) and name are required - row skipped");
                            continue;
                        }
                        const bool duplicate = std::any_of(weapons.begin(), weapons.end(),
                            [&](const CardDef& other) { return other.id == id; });
                        if (duplicate)
                        {
                            loader.Error(file, row.line, "duplicate id '" + id + "' - row skipped");
                            continue;
                        }

                        const std::string effectName = Lower(Cell(row, effectCol));
                        const EffectSpec* spec = nullptr;
                        for (const EffectSpec& candidate : kEffectSpecs)
                            if (effectName == candidate.name) spec = &candidate;
                        if (spec == nullptr)
                        {
                            loader.Error(file, row.line, "unknown effect '" + effectName + "' - row skipped");
                            continue;
                        }

                        bool valid = true;
                        float maxLevelF = 0.0f, baseTargetsF = 0.0f, extraEveryF = 0.0f;
                        struct Req { const char* label; int col; float* target; float minValue; bool minInclusive; };
                        const Req required[] = {
                            { "cooldown",                 cdCol,           &def.cooldown,              0.0f, false },
                            { "damage",                   dmgCol,          &def.damage,                0.0f, true  },
                            { "range",                    rangeCol,        &def.range,                 0.0f, false },
                            { "base_targets",              baseTargetsCol,  &baseTargetsF,              0.0f, true  },
                            { "max_level",                 maxLevelCol,     &maxLevelF,                 1.0f, true  },
                            { "damage_per_level",          dmgPerLevelCol,  &def.damagePerLevel,       -1e9f, true  },
                            { "range_per_level",           rangePerLevelCol,&def.rangePerLevel,        -1e9f, true  },
                            { "cooldown_scale",            cdScaleCol,      &def.cooldownScalePerLevel, 0.0f, false },
                            { "levels_per_extra_target",   extraEveryCol,   &extraEveryF,               0.0f, true  },
                        };
                        for (const Req& req : required)
                        {
                            if (!core::ParseFloat(Cell(row, req.col), *req.target))
                            {
                                loader.Error(file, row.line, std::string(req.label) + " must be a number - row skipped");
                                valid = false;
                                break;
                            }
                            const bool ok = req.minInclusive ? *req.target >= req.minValue : *req.target > req.minValue;
                            if (!ok)
                            {
                                loader.Error(file, row.line, std::string(req.label) + " out of range - row skipped");
                                valid = false;
                                break;
                            }
                        }
                        if (!valid) continue;

                        // Optional numbers - blank cell keeps the CardDef default.
                        struct Opt { const char* label; float* target; float minValue; };
                        const Opt optional[] = {
                            { "cone_half_angle_deg", &def.coneHalfAngleDeg, 0.0f },
                            { "line_half_width",     &def.lineHalfWidth,    0.0f },
                            { "projectile_speed",    &def.projectileSpeed,  0.0f },
                            { "explode_radius",      &def.explodeRadius,    0.0f },
                            { "damage_max",          &def.damageMax,        0.0f },
                            { "hit_radius",          &def.hitRadius,        0.0f },
                            { "visual_scale",        &def.visualScale,      0.0f },
                            { "lifetime",            &def.lifetime,         0.0f },
                            { "start_radius",        &def.startRadius,      0.0f },
                            { "radial_speed",        &def.radialSpeed,   -1e9f },
                            { "angular_speed_deg",   &def.angularSpeedDeg, -1e9f },
                            { "tick_interval",       &def.tickInterval,     0.0f },
                            { "rehit_interval",      &def.rehitInterval,    0.0f },
                        };
                        for (const Opt& opt : optional)
                        {
                            const int col = table.Column(opt.label);
                            const std::string cell = Cell(row, col);
                            if (col < 0 || cell.empty()) continue;
                            if (!core::ParseFloat(cell, *opt.target) || *opt.target < opt.minValue)
                            {
                                loader.Error(file, row.line, std::string(opt.label) + " must be a number (>= 0 unless it is a speed) - row skipped");
                                valid = false;
                                break;
                            }
                        }
                        if (!valid) continue;

                        // Movement (docs/circular-combat.md §2.5). Blank = CardDef default.
                        const std::string pathName = Lower(Cell(row, pathCol));
                        if (pathName == "straight") def.path = AttackPath::Straight;
                        else if (pathName == "polar") def.path = AttackPath::Polar;
                        else if (!pathName.empty() && pathName != "none")
                        {
                            loader.Error(file, row.line, "path must be none, straight or polar - row skipped");
                            continue;
                        }
                        const std::string anchorName = Lower(Cell(row, anchorCol));
                        if (anchorName == "player") def.anchor = PathAnchor::Player;
                        else if (!anchorName.empty() && anchorName != "cast")
                        {
                            loader.Error(file, row.line, "anchor must be cast or player - row skipped");
                            continue;
                        }
                        const std::string countCell = Cell(row, countCol);
                        float countF = 1.0f;
                        if (!countCell.empty() && (!core::ParseFloat(countCell, countF) || countF < 1.0f))
                        {
                            loader.Error(file, row.line, "count must be a number >= 1 - row skipped");
                            continue;
                        }
                        def.count = static_cast<int>(countF);

                        // Combinations the combat code can't run - rejected here, not guessed at.
                        def.effect = spec->effect;
                        const AttackForm form = spec->form;
                        const AttackPath path = EffectivePath(def);
                        const char* problem = nullptr;
                        if (form == AttackForm::Nearest && path != AttackPath::None) problem = "nearestbolt can't have a path";
                        else if (path == AttackPath::Straight && def.projectileSpeed <= 0.0f && def.lifetime <= 0.0f)
                            problem = "a straight path needs projectile_speed > 0 (or a lifetime)";
                        else if (path == AttackPath::Polar && def.lifetime <= 0.0f) problem = "a polar path needs lifetime > 0";
                        else if (form == AttackForm::Area && path != AttackPath::None && def.tickInterval <= 0.0f)
                            problem = "a moving area needs tick_interval > 0";
                        if (problem != nullptr)
                        {
                            loader.Error(file, row.line, std::string(problem) + " - row skipped");
                            continue;
                        }

                        const std::string colorCell = Cell(row, colorCol);
                        if (!colorCell.empty() && !ParseHexColor(colorCell, def.color))
                        {
                            loader.Error(file, row.line, "color must be RRGGBB hex - row skipped");
                            continue;
                        }

                        // Overflow (§3.4) - blank overflow_stat keeps overflowValue at 0
                        // (no bonus configured, so this weapon is never offered as an
                        // overflow choice once maxed). overflow_value alone without a
                        // recognised stat is an error - it would silently do nothing.
                        const std::string overflowStatName = Lower(Cell(row, overflowStatCol));
                        if (!overflowStatName.empty())
                        {
                            int overflowStatIndex = -1;
                            for (std::size_t i = 0; i < kStatCount; ++i)
                                if (overflowStatName == kStatDefs[i].id) overflowStatIndex = static_cast<int>(i);
                            if (overflowStatIndex < 0)
                            {
                                loader.Error(file, row.line, "unknown overflow_stat '" + overflowStatName + "' - row skipped");
                                continue;
                            }
                            def.overflowStat = static_cast<StatId>(overflowStatIndex);
                            const std::string overflowValueCell = Cell(row, overflowValueCol);
                            if (!core::ParseFloat(overflowValueCell, def.overflowValue))
                            {
                                loader.Error(file, row.line, "overflow_value must be a number when overflow_stat is set - row skipped");
                                continue;
                            }
                        }

                        def.id = id;
                        def.name = name;
                        def.effect = spec->effect;
                        def.baseTargets = static_cast<int>(baseTargetsF);
                        def.maxLevel = static_cast<int>(maxLevelF);
                        def.levelsPerExtraTarget = static_cast<int>(extraEveryF);
                        def.sprite = Lower(Cell(row, spriteCol));
                        def.fxHit = Lower(Cell(row, fxHitCol));
                        weapons.push_back(std::move(def));
                    }
                    if (weapons.empty()) loader.Warn(file, 0, "no valid rows - using the built-in weapons");
                    else out.weapons = std::move(weapons);
                }
            }
        }

        // ---- accessories.csv: name,stat,multiplicative,amount,max_level ----
        // No effect code (passive only, §3.3) - a stat + add/mul amount per level.
        {
            const std::string file = "accessories.csv";
            core::CsvTable table;
            if (loader.Open(dir, file, table))
            {
                const int nameCol = table.Column("name");
                const int statCol = table.Column("stat");
                const int mulCol = table.Column("multiplicative");
                const int amountCol = table.Column("amount");
                const int maxLevelCol = table.Column("max_level");
                if (nameCol < 0 || statCol < 0 || mulCol < 0 || amountCol < 0 || maxLevelCol < 0)
                {
                    loader.Error(file, 1, "header must contain name,stat,multiplicative,amount,max_level");
                }
                else
                {
                    std::vector<AccessoryDef> accessories;
                    for (const core::CsvRow& row : table.rows)
                    {
                        const std::string name = Cell(row, nameCol);
                        if (name.empty())
                        {
                            loader.Error(file, row.line, "name is required - row skipped");
                            continue;
                        }
                        const bool duplicate = std::any_of(accessories.begin(), accessories.end(),
                            [&](const AccessoryDef& other) { return Lower(other.name) == Lower(name); });
                        if (duplicate)
                        {
                            loader.Error(file, row.line, "duplicate name '" + name + "' - row skipped");
                            continue;
                        }

                        const std::string statName = Lower(Cell(row, statCol));
                        int statIndex = -1;
                        for (std::size_t i = 0; i < kStatCount; ++i)
                            if (statName == kStatDefs[i].id) statIndex = static_cast<int>(i);
                        if (statIndex < 0)
                        {
                            loader.Error(file, row.line, "unknown stat '" + statName + "' - row skipped");
                            continue;
                        }

                        const std::string mulCell = Lower(Cell(row, mulCol));
                        if (mulCell != "0" && mulCell != "1" && mulCell != "true" && mulCell != "false")
                        {
                            loader.Error(file, row.line, "multiplicative must be 0/1/true/false - row skipped");
                            continue;
                        }
                        const bool multiplicative = mulCell == "1" || mulCell == "true";

                        AccessoryDef def;
                        float maxLevelF = 0.0f;
                        if (!core::ParseFloat(Cell(row, amountCol), def.amount) ||
                            !core::ParseFloat(Cell(row, maxLevelCol), maxLevelF) || maxLevelF < 1.0f)
                        {
                            loader.Error(file, row.line, "amount must be a number and max_level >= 1 - row skipped");
                            continue;
                        }
                        def.name = name;
                        def.stat = static_cast<StatId>(statIndex);
                        def.multiplicative = multiplicative;
                        def.maxLevel = static_cast<int>(maxLevelF);
                        accessories.push_back(std::move(def));
                    }
                    if (accessories.empty()) loader.Warn(file, 0, "no valid rows - using the built-in accessories");
                    else out.accessories = std::move(accessories);
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
                        for (std::size_t i = 0; i < out.weapons.size(); ++i)
                            if (weaponName == out.weapons[i].id) weaponIndex = static_cast<int>(i);
                        if (weaponIndex < 0)
                        {
                            loader.Error(file, row.line, "start_weapon '" + weaponName + "' is not a known weapon id - row skipped");
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
