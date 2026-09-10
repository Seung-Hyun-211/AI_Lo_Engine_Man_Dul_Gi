#pragma once

#include <cstdint>
#include <string>
#include <vector>

// The engine's decoded-image value type + the built-in TGA decoder.
//
// `ImageData` is the single currency for raw pixels across the whole texture
// path (ModelMeshPass3D texture cache, the future UI atlas, tools/atlas_pack):
// RGBA8, top-down rows, straight (non-premultiplied) alpha. No colour-space
// transform - the consumer picks the SRV format (see docs/image-assets.md).
//
// LoadTga handles uncompressed true-color (type 2), 24 or 32 bpp - enough for
// the demo character's textures. RLE (type 10) and color-mapped TGAs are not
// handled; add them here if an asset needs them. The general loader for
// PNG/JPEG/BMP/GIF is engine::import::LoadImageFromFile (import/ImageFile.h).
namespace engine::import
{
    struct ImageData
    {
        bool ok{ false };
        int width{ 0 };
        int height{ 0 };
        std::vector<std::uint8_t> rgba;   // width*height*4, top-down, straight alpha
    };

    [[nodiscard]] ImageData LoadTga(const std::string& path);
}
