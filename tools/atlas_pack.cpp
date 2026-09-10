// tools/atlas_pack — offline texture-atlas packer. NOT part of the engine build
// (CppWindowGame.vcxproj does not include this); build + run by hand or from CI,
// same status as tools/entity_memory_bench.cpp. Design: docs/atlas-build-pipeline.md.
//
//   Build: tools\build_atlas_pack.bat        (-> build\tools\atlas_pack.exe)
//   Run:   build\tools\atlas_pack.exe [--all | --group <name>] [--force]
//          (run from the project root; the .bat cd's there)
//
// Per group it reads every image under assets/src/<group>/ (recursively), packs
// them onto fixed-size square pages with an edge-extended gutter, builds box-
// filter mips, and writes:
//   assets/atlas/<base>.<page>.dds   uncompressed R8G8B8A8_UNORM_SRGB, DX10 header
//   assets/atlas/<base>.atlas        name page u0 v0 u1 v1 pixelW pixelH  (per sprite)
//   assets/atlas/<base>.cache        input hash for incremental builds (git-ignored)
// where <base> is the group name with '/' -> '_'.
//
// v1 scope (see docs): UNCOMPRESSED pages only - no BC7/BC4 encoder vendored.
// The runtime .dds loader (SpritePass2D) already accepts BC7_UNORM_SRGB /
// BC4_UNORM, so a later `--format bc7` path swaps only the "write page pixels"
// step; the .dds/.atlas contract and everything below is unchanged.
//
// The image decode reuses the engine's single seam import::LoadImageFromFile
// (src/import/ImageFile.cpp + ImageData.cpp + vendor/stb compiled into this
// tool) so loose PNG/JPG/BMP/GIF/TGA all normalise the same way as at runtime.

#include "import/ImageFile.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using engine::import::ImageData;
using engine::import::LoadImageFromFile;

namespace
{
    constexpr char kPackerVersion[] = "atlas_pack v1 (rgba8-srgb)";
    constexpr std::uint32_t kDxgiR8G8B8A8UnormSrgb = 29;

    struct GroupCfg
    {
        std::string name;
        int pageSize = 4096;   // {1024,2048,4096}
        int mips = 0;          // extra levels; -1 = full chain; 0 = none
        int gutter = 4;        // px of edge-extend around each sprite
    };

    struct Sprite
    {
        std::string name;
        ImageData img;
        int x = 0, y = 0;   // inner (pixel) top-left on its page
        int page = 0;
    };

    [[noreturn]] void Fail(const std::string& msg)
    {
        std::fprintf(stderr, "atlas_pack: error: %s\n", msg.c_str());
        std::exit(1);
    }

    std::string ToBase(std::string group)   // "char/unitychan" -> "char_unitychan"
    {
        for (char& c : group)
            if (c == '/' || c == '\\') c = '_';
        return group;
    }

    // ---- manifest -----------------------------------------------------------

    std::vector<GroupCfg> ParseGroups(const std::string& path)
    {
        std::ifstream in(path);
        if (!in) Fail("cannot open group manifest '" + path + "'");

        std::vector<GroupCfg> groups;
        std::string line;
        int lineNo = 0;
        while (std::getline(in, line))
        {
            ++lineNo;
            // strip comment + trailing whitespace
            if (const auto h = line.find('#'); h != std::string::npos) line.erase(h);
            while (!line.empty() && (line.back() == ' ' || line.back() == '\t' || line.back() == '\r'))
                line.pop_back();
            if (line.empty()) continue;

            // format: group <name> page <n> mips <n> gutter <n>   (the 3 pairs in any order)
            std::vector<std::string> tok;
            for (std::size_t i = 0; i < line.size();)
            {
                while (i < line.size() && (line[i] == ' ' || line[i] == '\t')) ++i;
                const std::size_t start = i;
                while (i < line.size() && line[i] != ' ' && line[i] != '\t') ++i;
                if (i > start) tok.push_back(line.substr(start, i - start));
            }
            if (tok.size() != 8 || tok[0] != "group")
                Fail(path + ":" + std::to_string(lineNo) + ": expected `group <name> page <n> mips <n> gutter <n>`");

            GroupCfg g;
            g.name = tok[1];
            for (int i = 2; i < 8; i += 2)
            {
                const std::string& k = tok[static_cast<std::size_t>(i)];
                const int v = std::atoi(tok[static_cast<std::size_t>(i) + 1].c_str());
                if (k == "page") g.pageSize = v;
                else if (k == "mips") g.mips = v;
                else if (k == "gutter") g.gutter = v;
                else Fail(path + ":" + std::to_string(lineNo) + ": unknown key '" + k + "'");
            }
            if (g.pageSize != 1024 && g.pageSize != 2048 && g.pageSize != 4096)
                Fail(g.name + ": page must be 1024, 2048 or 4096");
            if (g.gutter < 0 || g.gutter > 64) Fail(g.name + ": gutter out of range");
            groups.push_back(std::move(g));
        }
        if (groups.empty()) Fail(path + ": no groups");
        return groups;
    }

    // ---- inputs -----------------------------------------------------------

    bool IsImageExt(const fs::path& p)
    {
        std::string e = p.extension().string();
        for (char& c : e) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        return e == ".png" || e == ".jpg" || e == ".jpeg" || e == ".bmp" || e == ".gif" || e == ".tga";
    }

    std::vector<fs::path> CollectInputs(const std::string& group)
    {
        const fs::path root = fs::path("assets/src") / group;
        std::vector<fs::path> files;
        std::error_code ec;
        if (!fs::is_directory(root, ec))
        {
            std::printf("  (no source dir %s - skipping)\n", root.string().c_str());
            return files;
        }
        for (const auto& entry : fs::recursive_directory_iterator(root, ec))
            if (entry.is_regular_file() && IsImageExt(entry.path())) files.push_back(entry.path());
        std::sort(files.begin(), files.end(),
                  [](const fs::path& a, const fs::path& b) { return a.string() < b.string(); });
        return files;
    }

    std::vector<std::uint8_t> ReadFile(const fs::path& p)
    {
        std::ifstream f(p, std::ios::binary);
        return { std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>() };
    }

    std::uint64_t Fnv1a(const std::uint8_t* data, std::size_t n, std::uint64_t h = 1469598103934665603ull)
    {
        for (std::size_t i = 0; i < n; ++i) { h ^= data[i]; h *= 1099511628211ull; }
        return h;
    }
    std::uint64_t Fnv1aStr(const std::string& s, std::uint64_t h) { return Fnv1a(reinterpret_cast<const std::uint8_t*>(s.data()), s.size(), h); }

    // ---- packing --------------------------------------------------------

    // Shelf packer: tallest-first, left-to-right rows, new page on overflow.
    // Returns page count, or 0 if a single sprite (+gutter) exceeds a page.
    int ShelfPack(std::vector<Sprite>& sprites, int pageSize, int gutter)
    {
        std::stable_sort(sprites.begin(), sprites.end(),
                         [](const Sprite& a, const Sprite& b) { return a.img.height > b.img.height; });
        int page = 0, shelfY = 0, shelfH = 0, penX = 0;
        for (Sprite& s : sprites)
        {
            const int w = s.img.width + 2 * gutter;
            const int h = s.img.height + 2 * gutter;
            if (w > pageSize || h > pageSize) return 0;
            if (penX + w > pageSize) { penX = 0; shelfY += shelfH; shelfH = 0; }
            if (shelfY + h > pageSize) { ++page; shelfY = 0; shelfH = 0; penX = 0; }
            s.page = page;
            s.x = penX + gutter;
            s.y = shelfY + gutter;
            penX += w;
            shelfH = std::max(shelfH, h);
        }
        return page + 1;
    }

    // ---- page compose + mips + dds -------------------------------------

    using Rgba = std::vector<std::uint8_t>;   // w*h*4, top-down

    void BlitWithGutter(Rgba& page, int pageSize, const Sprite& s, int gutter)
    {
        const int w = s.img.width, h = s.img.height;
        const std::uint8_t* src = s.img.rgba.data();
        // For every texel in [-gutter, w+gutter) x [-gutter, h+gutter): the
        // sprite itself, or (in the gutter band) the clamped edge texel.
        for (int dy = -gutter; dy < h + gutter; ++dy)
        {
            const int py = s.y + dy;
            if (py < 0 || py >= pageSize) continue;
            const int sy = std::clamp(dy, 0, h - 1);
            for (int dx = -gutter; dx < w + gutter; ++dx)
            {
                const int px = s.x + dx;
                if (px < 0 || px >= pageSize) continue;
                const int sx = std::clamp(dx, 0, w - 1);
                std::memcpy(&page[(static_cast<std::size_t>(py) * pageSize + px) * 4],
                            &src[(static_cast<std::size_t>(sy) * w + sx) * 4], 4);
            }
        }
    }

    Rgba BoxDownsample(const Rgba& in, int w, int h, int& outW, int& outH)
    {
        outW = std::max(1, w / 2);
        outH = std::max(1, h / 2);
        Rgba out(static_cast<std::size_t>(outW) * outH * 4);
        for (int y = 0; y < outH; ++y)
            for (int x = 0; x < outW; ++x)
            {
                const int sx0 = std::min(x * 2, w - 1), sx1 = std::min(x * 2 + 1, w - 1);
                const int sy0 = std::min(y * 2, h - 1), sy1 = std::min(y * 2 + 1, h - 1);
                for (int c = 0; c < 4; ++c)
                {
                    const int a = in[(static_cast<std::size_t>(sy0) * w + sx0) * 4 + c];
                    const int b = in[(static_cast<std::size_t>(sy0) * w + sx1) * 4 + c];
                    const int cc = in[(static_cast<std::size_t>(sy1) * w + sx0) * 4 + c];
                    const int d = in[(static_cast<std::size_t>(sy1) * w + sx1) * 4 + c];
                    out[(static_cast<std::size_t>(y) * outW + x) * 4 + c] =
                        static_cast<std::uint8_t>((a + b + cc + d + 2) / 4);
                }
            }
        return out;
    }

    void WriteDds(const fs::path& path, int pageSize, const std::vector<Rgba>& mips)
    {
        std::uint32_t hdr[31] = {};
        hdr[0] = 124;                                   // dwSize
        hdr[1] = 0x1u | 0x2u | 0x4u | 0x1000u | 0x20000u; // CAPS|HEIGHT|WIDTH|PIXELFORMAT|MIPMAPCOUNT
        hdr[2] = static_cast<std::uint32_t>(pageSize);  // dwHeight
        hdr[3] = static_cast<std::uint32_t>(pageSize);  // dwWidth
        hdr[4] = static_cast<std::uint32_t>(pageSize) * 4; // dwPitchOrLinearSize (pitch)
        hdr[6] = static_cast<std::uint32_t>(mips.size()); // dwMipMapCount
        hdr[18] = 32;                                   // ddspf.dwSize
        hdr[19] = 0x4;                                  // ddspf.dwFlags = DDPF_FOURCC
        hdr[20] = 0x30315844u;                          // 'DX10'
        hdr[26] = 0x1000u | (mips.size() > 1 ? (0x400000u | 0x8u) : 0u); // TEXTURE | MIPMAP|COMPLEX

        const std::uint32_t dx10[5] = { kDxgiR8G8B8A8UnormSrgb, 3u /*TEXTURE2D*/, 0u, 1u /*arraySize*/, 0u };

        std::ofstream out(path, std::ios::binary);
        if (!out) Fail("cannot write " + path.string());
        out.write("DDS ", 4);
        out.write(reinterpret_cast<const char*>(hdr), sizeof(hdr));
        out.write(reinterpret_cast<const char*>(dx10), sizeof(dx10));
        for (const Rgba& m : mips) out.write(reinterpret_cast<const char*>(m.data()), static_cast<std::streamsize>(m.size()));
    }

    // ---- one group -----------------------------------------------------

    // returns true if it (re)packed, false if skipped via cache
    bool PackGroup(const GroupCfg& g, bool force)
    {
        const std::string base = ToBase(g.name);
        const fs::path outDir = "assets/atlas";
        const fs::path cachePath = outDir / (base + ".cache");
        const fs::path atlasPath = outDir / (base + ".atlas");

        const std::vector<fs::path> inputs = CollectInputs(g.name);

        // --- incremental: hash inputs + config, compare to <base>.cache ---
        std::uint64_t hash = Fnv1aStr(kPackerVersion, 1469598103934665603ull);
        hash = Fnv1aStr(std::to_string(g.pageSize) + "|" + std::to_string(g.mips) + "|" + std::to_string(g.gutter), hash);
        for (const fs::path& p : inputs)
        {
            hash = Fnv1aStr(p.generic_string(), hash);
            const std::vector<std::uint8_t> bytes = ReadFile(p);
            hash = Fnv1a(bytes.data(), bytes.size(), hash);
        }
        char hashHex[17];
        std::snprintf(hashHex, sizeof(hashHex), "%016llx", static_cast<unsigned long long>(hash));

        if (!force)
        {
            std::ifstream cf(cachePath);
            std::string prev;
            if (cf && std::getline(cf, prev) && prev == hashHex && fs::exists(atlasPath))
            {
                std::printf("  %s: up to date (%zu inputs)\n", g.name.c_str(), inputs.size());
                return false;
            }
        }

        if (inputs.empty())
        {
            std::printf("  %s: no inputs, nothing written\n", g.name.c_str());
            return false;
        }

        // --- load ---
        std::vector<Sprite> sprites;
        sprites.reserve(inputs.size());
        for (const fs::path& p : inputs)
        {
            Sprite s;
            s.name = p.stem().string();
            s.img = LoadImageFromFile(p.string());
            if (!s.img.ok || s.img.rgba.empty())
                Fail(g.name + ": failed to decode " + p.string());
            for (const Sprite& other : sprites)
                if (other.name == s.name)
                    Fail(g.name + ": duplicate sprite name '" + s.name + "' (" + p.string() + ")");
            sprites.push_back(std::move(s));
        }

        // --- pack ---
        const int pageCount = ShelfPack(sprites, g.pageSize, g.gutter);
        if (pageCount == 0)
            Fail(g.name + ": a sprite is larger than the " + std::to_string(g.pageSize) + "px page");

        fs::create_directories(outDir);
        // clear stale pages from a previous, larger run
        for (int p = 0; p < 256; ++p)
        {
            std::error_code ec;
            fs::remove(outDir / (base + "." + std::to_string(p) + ".dds"), ec);
        }

        // --- compose + mips + write each page ---
        for (int p = 0; p < pageCount; ++p)
        {
            Rgba page(static_cast<std::size_t>(g.pageSize) * g.pageSize * 4, 0);
            for (const Sprite& s : sprites)
                if (s.page == p) BlitWithGutter(page, g.pageSize, s, g.gutter);

            std::vector<Rgba> mips;
            mips.push_back(std::move(page));
            const int maxExtra = (g.mips < 0) ? 1 << 20 : g.mips;
            int w = g.pageSize, h = g.pageSize;
            while (static_cast<int>(mips.size()) <= maxExtra && (w > 1 || h > 1))
            {
                int nw = 0, nh = 0;
                Rgba next = BoxDownsample(mips.back(), w, h, nw, nh);
                mips.push_back(std::move(next));
                w = nw; h = nh;
            }
            WriteDds(outDir / (base + "." + std::to_string(p) + ".dds"), g.pageSize, mips);
        }

        // --- .atlas manifest ---
        std::vector<Sprite*> byName;
        for (Sprite& s : sprites) byName.push_back(&s);
        std::sort(byName.begin(), byName.end(), [](const Sprite* a, const Sprite* b) { return a->name < b->name; });

        std::ofstream man(atlasPath);
        if (!man) Fail("cannot write " + atlasPath.string());
        man << "# " << base << " atlas - generated by tools/atlas_pack (" << kPackerVersion << ")\n";
        man << "# name  page  u0 v0 u1 v1  pixelW pixelH\n";
        const double inv = 1.0 / g.pageSize;
        for (const Sprite* s : byName)
        {
            const double u0 = s->x * inv, v0 = s->y * inv;
            const double u1 = (s->x + s->img.width) * inv, v1 = (s->y + s->img.height) * inv;
            char row[256];
            std::snprintf(row, sizeof(row), "%s %d %.6f %.6f %.6f %.6f %d %d\n",
                          s->name.c_str(), s->page, u0, v0, u1, v1, s->img.width, s->img.height);
            man << row;
        }

        std::ofstream(cachePath) << hashHex << "\n";
        std::printf("  %s: %zu sprites, %d page(s) %dx%d -> %s.*.dds + %s.atlas\n",
                    g.name.c_str(), sprites.size(), pageCount, g.pageSize, g.pageSize, base.c_str(), base.c_str());
        return true;
    }
}

int main(int argc, char** argv)
{
    std::string only;
    bool force = false;
    for (int i = 1; i < argc; ++i)
    {
        const std::string a = argv[i];
        if (a == "--force") force = true;
        else if (a == "--all") only.clear();
        else if (a == "--group" && i + 1 < argc) only = argv[++i];
        else Fail("usage: atlas_pack [--all | --group <name>] [--force]");
    }

    const std::vector<GroupCfg> groups = ParseGroups("assets/atlas/atlas.groups");
    std::printf("atlas_pack: %s\n", kPackerVersion);

    int packed = 0, seen = 0;
    for (const GroupCfg& g : groups)
    {
        if (!only.empty() && g.name != only) continue;
        ++seen;
        if (PackGroup(g, force)) ++packed;
    }
    if (seen == 0) Fail("no group named '" + only + "' in assets/atlas/atlas.groups");
    std::printf("atlas_pack: done (%d/%d group(s) repacked)\n", packed, seen);
    return 0;
}
