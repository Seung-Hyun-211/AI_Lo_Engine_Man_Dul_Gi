// Headless balance simulator for the Circular scene (docs/circular-balance.md).
//
// Runs the real game::Simulation - the same spawn curve, mob stats, cards and
// XP table the game uses - for N simulated seconds with no window and no GPU,
// far faster than real time, and writes two CSVs you can chart in Excel:
//   <out>_timeline.csv  one row per --interval seconds (mobs alive, kills, level, XP ...)
//   <out>_levels.csv    the second each level was reached (the XP curve, as felt)
//
// The stand-in player never moves and, by default, takes no damage (god mode -
// it measures pacing, and a bot standing in a swarm would just die). --mortal
// turns damage on: the run stops at death and reports when. Every level-up takes
// the first card option offered (else the first stat). Good for tuning pacing
// curves, not a substitute for playing.
//
// Build/run: tools\run_balance_sim.bat [--seconds N] [--interval S] [--out PREFIX] [--mortal]
// It reads assets/data/circular/*.csv from wherever it finds an `assets` folder
// (run it from the repo root).

#include "game/Simulation.h"

#include "core/JobSystem.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

using namespace engine;
using namespace engine::game;

namespace
{
    struct LevelReached { int level; float time; int kills; };

    std::string DeckText(const Simulation& sim)
    {
        std::string text;
        for (const CardInstance& card : sim.Deck())
        {
            if (!text.empty()) text += ' ';
            text += sim.Balance().weapons[card.defIndex].name + std::to_string(card.level);
        }
        return text;
    }
}

int main(int argc, char** argv)
{
    float seconds = 600.0f;
    float interval = 10.0f;
    std::string outPrefix = "build/tools/balance";
    bool mortal = false;
    for (int i = 1; i < argc; ++i)
    {
        const bool hasValue = i + 1 < argc;
        if (!std::strcmp(argv[i], "--seconds") && hasValue) seconds = static_cast<float>(std::atof(argv[++i]));
        else if (!std::strcmp(argv[i], "--interval") && hasValue) interval = static_cast<float>(std::atof(argv[++i]));
        else if (!std::strcmp(argv[i], "--out") && hasValue) outPrefix = argv[++i];
        else if (!std::strcmp(argv[i], "--mortal")) mortal = true;
        else
        {
            std::fprintf(stderr, "usage: balance_sim [--seconds N] [--interval S] [--out PREFIX] [--mortal]\n");
            return 2;
        }
    }
    if (seconds <= 0.0f || interval <= 0.0f)
    {
        std::fprintf(stderr, "--seconds and --interval must be > 0\n");
        return 2;
    }

    core::JobSystem jobs(core::RecommendedWorkerCount());
    Simulation sim(jobs, 1280, 720);
    sim.SetGodMode(!mortal);
    sim.EnterScene(DemoScene::Circular);   // loads the CSVs

    const BalanceLoadReport& report = sim.BalanceReport();
    for (const std::string& message : report.messages) std::printf("[balance] %s\n", message.c_str());
    std::printf("[balance] %s  (errors %d, warnings %d)\n", report.Clean() ? "CSV OK" : "CSV HAS PROBLEMS", report.errors, report.warnings);
    if (report.errors > 0) std::printf("[balance] rows with errors are skipped - the run below uses the rest + defaults\n");

    std::filesystem::path timelinePath = outPrefix + "_timeline.csv";
    std::filesystem::path levelsPath = outPrefix + "_levels.csv";
    if (timelinePath.has_parent_path()) std::filesystem::create_directories(timelinePath.parent_path());
    std::FILE* timeline = std::fopen(timelinePath.string().c_str(), "w");
    std::FILE* levels = std::fopen(levelsPath.string().c_str(), "w");
    if (!timeline || !levels)
    {
        std::fprintf(stderr, "cannot write %s / %s\n", timelinePath.string().c_str(), levelsPath.string().c_str());
        return 1;
    }
    std::fprintf(timeline, "time_sec,mobs_alive,kills_total,kills_per_sec,level,xp,xp_to_next,spawns_per_sec,max_alive,deck\n");
    std::fprintf(levels, "level,time_sec,kills_total,seconds_since_prev\n");

    std::printf("\n%8s %8s %8s %8s %6s %10s %8s %8s  %s\n", "time", "mobs", "kills", "kills/s", "level", "xp", "rate", "cap", "deck");

    const float dt = 1.0f / 60.0f;
    const PlayerIntent idle{};
    std::vector<LevelReached> reached;
    float nextReport = 0.0f;
    float lastReportTime = 0.0f;
    int lastReportKills = 0;
    float prevLevelTime = 0.0f;
    int simulatedSteps = 0;

    while (sim.RunTime() < seconds)
    {
        if (sim.RunOver())
        {
            std::printf("\nplayer died at %.1fs (--mortal)\n", sim.RunTime());
            break;
        }
        if (sim.LevelUpPending())
        {
            // Bot policy: first card option (new or upgrade), else whatever is first.
            std::size_t pick = 0;
            for (std::size_t i = 0; i < sim.LevelUpChoiceCount(); ++i)
            {
                const std::string label = sim.LevelUpChoiceLabel(i);
                if (label.rfind("MOVE", 0) != 0 && label.rfind("XP", 0) != 0) { pick = i; break; }
            }
            sim.ChooseLevelUpOption(pick);
            reached.push_back({ sim.PlayerLevel(), sim.RunTime(), sim.MobKillCount() });
            std::fprintf(levels, "%d,%.1f,%d,%.1f\n", sim.PlayerLevel(), sim.RunTime(), sim.MobKillCount(), sim.RunTime() - prevLevelTime);
            prevLevelTime = sim.RunTime();
            continue;
        }

        sim.Step(dt, idle);
        ++simulatedSteps;

        if (sim.RunTime() >= nextReport)
        {
            const CircularBalance::SpawnRate rate = sim.Balance().SpawnAt(sim.RunTime());
            const float span = std::max(sim.RunTime() - lastReportTime, 1e-3f);
            const float killsPerSec = static_cast<float>(sim.MobKillCount() - lastReportKills) / span;
            const std::string deck = DeckText(sim);
            std::printf("%8.1f %8zu %8d %8.1f %6d %5.0f/%-5.0f %8.0f %8d  %s\n", sim.RunTime(), sim.Mobs().LiveCount(),
                        sim.MobKillCount(), killsPerSec, sim.PlayerLevel(), sim.XpCurrent(), sim.XpNeeded(),
                        rate.perSecond, rate.maxAlive, deck.c_str());
            std::fprintf(timeline, "%.1f,%zu,%d,%.1f,%d,%.1f,%.1f,%.1f,%d,%s\n", sim.RunTime(), sim.Mobs().LiveCount(),
                         sim.MobKillCount(), killsPerSec, sim.PlayerLevel(), sim.XpCurrent(), sim.XpNeeded(),
                         rate.perSecond, rate.maxAlive, deck.c_str());
            lastReportTime = sim.RunTime();
            lastReportKills = sim.MobKillCount();
            nextReport += interval;
        }
    }

    std::fclose(timeline);
    std::fclose(levels);
    std::printf("\nsimulated %.0fs (%d steps): level %d, %d kills, %zu mobs alive\n", sim.RunTime(), simulatedSteps,
                sim.PlayerLevel(), sim.MobKillCount(), sim.Mobs().LiveCount());
    std::printf("level timeline -> %s\nrun timeline   -> %s\n", levelsPath.string().c_str(), timelinePath.string().c_str());
    return report.errors > 0 ? 1 : 0;
}
