#pragma once

#include <cstdint>
#include <string>
#include <vector>

// Minimal TGA reader: uncompressed true-color (type 2), 24 or 32 bpp. Enough for
// the demo character's textures. RLE (type 10) and color-mapped TGAs are not
// handled - add them here if an asset needs them.
namespace engine::import
{
    struct TgaImage
    {
        bool ok{ false };
        int width{ 0 };
        int height{ 0 };
        std::vector<std::uint8_t> rgba;   // width*height*4, top-down, straight alpha
    };

    [[nodiscard]] TgaImage LoadTga(const std::string& path);
}
