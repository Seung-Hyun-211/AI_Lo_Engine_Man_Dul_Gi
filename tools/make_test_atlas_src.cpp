// Throwaway: writes a handful of 24-bit BMP fixtures to assets/src/ui/ so
// tools/atlas_pack has something to pack in a fresh checkout. Real UI art would
// be .png (stb decodes both identically into import::ImageData); BMP is used
// here only to avoid vendoring a PNG *writer*. Not part of any build.
//
//   Build: cl /nologo /std:c++20 /EHsc /O2 tools\make_test_atlas_src.cpp /Fe:build\tools\make_test_atlas_src.exe
//   Run  : build\tools\make_test_atlas_src.exe        (from the project root)

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <vector>

namespace fs = std::filesystem;

namespace
{
    void WriteBmp(const fs::path& path, int w, int h, const std::vector<std::uint8_t>& bgr)
    {
        const int rowRaw = w * 3;
        const int rowPad = (rowRaw + 3) & ~3;
        const std::uint32_t pixels = static_cast<std::uint32_t>(rowPad) * h;
        const std::uint32_t fileSize = 14 + 40 + pixels;

        std::uint8_t fh[14] = {};
        fh[0] = 'B'; fh[1] = 'M';
        std::memcpy(fh + 2, &fileSize, 4);
        const std::uint32_t dataOffset = 54;
        std::memcpy(fh + 10, &dataOffset, 4);

        std::uint8_t ih[40] = {};
        const std::uint32_t hdrSize = 40;
        std::memcpy(ih + 0, &hdrSize, 4);
        const std::int32_t iw = w, ihgt = h;
        std::memcpy(ih + 4, &iw, 4);
        std::memcpy(ih + 8, &ihgt, 4);
        const std::uint16_t planes = 1, bpp = 24;
        std::memcpy(ih + 12, &planes, 2);
        std::memcpy(ih + 14, &bpp, 2);
        std::memcpy(ih + 20, &pixels, 4);

        std::ofstream out(path, std::ios::binary);
        out.write(reinterpret_cast<const char*>(fh), 14);
        out.write(reinterpret_cast<const char*>(ih), 40);
        std::vector<std::uint8_t> row(rowPad, 0);
        for (int y = h - 1; y >= 0; --y)   // BMP is bottom-up
        {
            std::memcpy(row.data(), &bgr[static_cast<std::size_t>(y) * rowRaw], rowRaw);
            out.write(reinterpret_cast<const char*>(row.data()), rowPad);
        }
        std::printf("  wrote %s (%dx%d)\n", path.string().c_str(), w, h);
    }

    // fill helpers produce top-down BGR
    std::vector<std::uint8_t> Solid(int w, int h, std::uint8_t r, std::uint8_t g, std::uint8_t b, int border, std::uint8_t br, std::uint8_t bg, std::uint8_t bb)
    {
        std::vector<std::uint8_t> px(static_cast<std::size_t>(w) * h * 3);
        for (int y = 0; y < h; ++y)
            for (int x = 0; x < w; ++x)
            {
                const bool edge = x < border || y < border || x >= w - border || y >= h - border;
                std::uint8_t* p = &px[(static_cast<std::size_t>(y) * w + x) * 3];
                p[0] = edge ? bb : b; p[1] = edge ? bg : g; p[2] = edge ? br : r;
            }
        return px;
    }

    std::vector<std::uint8_t> Gradient(int w, int h)
    {
        std::vector<std::uint8_t> px(static_cast<std::size_t>(w) * h * 3);
        for (int y = 0; y < h; ++y)
            for (int x = 0; x < w; ++x)
            {
                std::uint8_t* p = &px[(static_cast<std::size_t>(y) * w + x) * 3];
                p[0] = static_cast<std::uint8_t>(255 * x / (w - 1));   // B
                p[1] = static_cast<std::uint8_t>(255 * y / (h - 1));   // G
                p[2] = 96;                                             // R
            }
        return px;
    }
}

int main()
{
    const fs::path dir = "assets/src/ui";
    fs::create_directories(dir);
    std::printf("make_test_atlas_src:\n");
    WriteBmp(dir / "panel_bg.bmp", 64, 64, Solid(64, 64, 40, 46, 60, 3, 120, 160, 200));
    WriteBmp(dir / "icon_play.bmp", 64, 64, Solid(64, 64, 60, 160, 90, 4, 220, 240, 220));
    WriteBmp(dir / "icon_settings.bmp", 64, 64, Solid(64, 64, 160, 120, 60, 4, 240, 220, 200));
    WriteBmp(dir / "bar_fill.bmp", 96, 24, Gradient(96, 24));
    std::printf("done. now run build\\tools\\atlas_pack.exe --group ui --force\n");
    return 0;
}
