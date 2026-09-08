#pragma once

#include <string>
#include <string_view>

// Resolves paths relative to the project's `assets/` folder regardless of the
// process working directory (F5 runs from the repo root; the raw exe runs from
// x64/Debug/). The root is located once - by walking up from the working
// directory and from the executable's directory looking for an `assets/` folder
// - then cached.
namespace engine::core
{
    // Returns an absolute (or best-effort relative) path to `<assetRoot>/relative`.
    // If no asset root is found, returns `relative` unchanged so callers still
    // get a sensible attempt.
    [[nodiscard]] std::string ResolveAsset(std::string_view relative);

    // The located assets root ("" if not found). Mostly for logging.
    [[nodiscard]] std::string AssetRoot();
}
