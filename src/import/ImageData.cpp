#include "import/ImageData.h"

#include <cstdio>

namespace engine::import
{
    ImageData LoadTga(const std::string& path)
    {
        ImageData image;

        std::FILE* file = nullptr;
        if (fopen_s(&file, path.c_str(), "rb") != 0 || file == nullptr) return image;

        std::uint8_t header[18]{};
        if (std::fread(header, 1, sizeof(header), file) != sizeof(header)) { std::fclose(file); return image; }

        const std::uint8_t idLength = header[0];
        const std::uint8_t imageType = header[2];
        const int width = header[12] | (header[13] << 8);
        const int height = header[14] | (header[15] << 8);
        const std::uint8_t bpp = header[16];
        const std::uint8_t descriptor = header[17];
        const bool topDown = (descriptor & 0x20) != 0;   // bit 5: 0 = bottom-left origin

        if (imageType != 2 || (bpp != 24 && bpp != 32) || width <= 0 || height <= 0)
        {
            std::fclose(file);
            return image;
        }

        std::fseek(file, idLength, SEEK_CUR);   // skip image id field

        const int channels = bpp / 8;
        std::vector<std::uint8_t> raw(static_cast<std::size_t>(width) * height * channels);
        if (std::fread(raw.data(), 1, raw.size(), file) != raw.size()) { std::fclose(file); return image; }
        std::fclose(file);

        image.rgba.resize(static_cast<std::size_t>(width) * height * 4);
        for (int y = 0; y < height; ++y)
        {
            const int srcRow = topDown ? y : (height - 1 - y);   // flip to top-down
            for (int x = 0; x < width; ++x)
            {
                const std::uint8_t* s = &raw[(static_cast<std::size_t>(srcRow) * width + x) * channels];
                std::uint8_t* d = &image.rgba[(static_cast<std::size_t>(y) * width + x) * 4];
                d[0] = s[2];                       // TGA is BGRA -> RGBA
                d[1] = s[1];
                d[2] = s[0];
                d[3] = channels == 4 ? s[3] : 255;
            }
        }

        image.ok = true;
        image.width = width;
        image.height = height;
        return image;
    }
}
