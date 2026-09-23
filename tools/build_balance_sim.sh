#!/usr/bin/env bash
# Linux (cloud session) twin of build_balance_sim.bat: builds the headless Circular simulator with g++
# and runs it with any args (--seconds N --interval S --out PREFIX). Run from anywhere; output in build/tools/.
# Also the quickest compile check of the 2D game code when MSBuild isn't available (see CLAUDE.md).
set -euo pipefail
cd "$(dirname "$0")/.."
mkdir -p build/tools
g++ -std=c++20 -O2 -Wall -Wextra -DENGINE_WITH_2D -I src -o build/tools/balance_sim \
    tools/balance_sim.cpp \
    src/game/Simulation.cpp src/game/MobField.cpp src/game/CircularBalance.cpp src/game/CircularCombat.cpp \
    src/core/JobSystem.cpp src/core/CsvFile.cpp src/core/AssetPaths.cpp \
    src/physics/p2d/CollisionWorld2D.cpp -lpthread
build/tools/balance_sim "$@"
