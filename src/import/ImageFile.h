#pragma once

#include "import/ImageData.h"

#include <string>

// General still-image loader. Returns an ImageData (RGBA8, top-down, straight
// alpha) - the one currency for raw pixels across the texture path.
//
// LoadImageFromFile dispatches by file extension:
//   .tga            -> the hand-rolled LoadTga (no third-party dependency for
//                      the model pipeline's own textures)
//   .png .jpg .jpeg
//   .bmp .gif       -> stb_image (src/vendor/stb, MIT / public-domain)
//
// The stb_image types are confined to ImageFile.cpp; callers only ever see
// engine types (CLAUDE.md rule 9). Colour-space / dev-ship rules:
// docs/image-assets.md.
namespace engine::import
{
    [[nodiscard]] ImageData LoadImageFromFile(const std::string& path);
}
