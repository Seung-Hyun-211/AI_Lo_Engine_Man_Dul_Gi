#!/usr/bin/env bash
# Linux (cloud session) twin of gen_templates.bat: builds tools/gen_templates.cpp with g++ and regenerates
# assets/templates/**/*_template.png from the current CSVs (docs/circular-art-guide.md 4.1). Run from anywhere.
set -euo pipefail
cd "$(dirname "$0")/.."
mkdir -p build/tools
g++ -std=c++20 -O2 -Wall -Wextra -I src -o build/tools/gen_templates \
    tools/gen_templates.cpp src/game/CircularBalance.cpp src/core/CsvFile.cpp src/core/AssetPaths.cpp
build/tools/gen_templates
