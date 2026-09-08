#pragma once

#include "import/Model.h"

#include <cstddef>
#include <string>

// FBX -> engine Model, backed by the vendored ufbx library (src/vendor/ufbx).
// ufbx is fully contained in ModelImporter.cpp; callers only see engine types.
//
// Status: compiles against ufbx 0.23.0 and extracts meshes, materials, the
// skeleton, and animation clips baked to fixed-rate keyframes. Not yet
// runtime-verified against real assets (no sample FBX in-tree) - winding,
// handedness, and weight normalisation may need a pass once a model is loaded.
// See docs/model-animation-research.md.
namespace engine::import
{
    struct ImportOptions
    {
        // Bake animation to this many keys per second.
        float animationSampleRate{ 30.0f };
        // Scale applied to all positions (FBX files vary wildly in unit).
        float scale{ 1.0f };
        // Load without animation curves (faster, meshes + skeleton only).
        bool skipAnimation{ false };
    };

    struct ImportResult
    {
        bool ok{ false };
        std::string error;   // human-readable when !ok
        Model model;
    };

    [[nodiscard]] ImportResult LoadModelFromFile(const std::string& path, const ImportOptions& options = {});
    [[nodiscard]] ImportResult LoadModelFromMemory(const void* data, std::size_t size, const ImportOptions& options = {});
}
