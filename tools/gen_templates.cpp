// Regenerates assets/templates/**/*_template.png - the Circular image templates artists draw over
// (docs/circular-art-guide.md §4.1). Sizes come from the game's own CSV loader (LoadCircularBalance),
// so a template is always the size the game actually uses: player.csv body, one per mobs.csv row,
// one per weapons.csv row, plus the boss body below (until bosses.csv exists, M6).
//
// Build + run from the repo root:  tools\gen_templates.bat   (Linux: tools/gen_templates.sh)
// Writes PNG-32 with a tiny built-in deflate (fixed Huffman, run matches) - no image library needed.
//
// Legend (same on every template): magenta 1px border = the canvas; cyan = the hit area (hitbox /
// attack reach); yellow cross = the game position (pivot / attack origin); red arrow = facing right.

#include "game/CircularBalance.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <functional>
#include <string>
#include <vector>

using namespace engine::game;
namespace fs = std::filesystem;

namespace
{
    // Boss body [살] until bosses.csv (M6) owns it: collision disc radius, and the two canvases
    // (walk, and a bigger attack canvas so a swing/cast has room). Both stand on the same
    // bottom-centre pivot, so the body lines up between clips.
    constexpr float kBossRadius = 40.0f;
    constexpr int kBossWalkCanvas = 128;
    constexpr int kBossAttackCanvas = 192;

    const char* const kOut = "assets/templates";

    struct Rgba { std::uint8_t r, g, b, a; };
    constexpr Rgba kBorder{ 255, 0, 255, 255 };
    constexpr Rgba kHit{ 0, 220, 255, 70 };
    constexpr Rgba kHitEdge{ 0, 220, 255, 255 };
    constexpr Rgba kPivot{ 255, 230, 0, 255 };
    constexpr Rgba kArrow{ 255, 60, 60, 255 };
    constexpr Rgba kClear{ 0, 0, 0, 0 };

    // ---- PNG writer ---------------------------------------------------------
    std::uint32_t Crc32(const std::uint8_t* data, std::size_t size, std::uint32_t crc = 0)
    {
        static const std::array<std::uint32_t, 256> table = [] {
            std::array<std::uint32_t, 256> t{};
            for (std::uint32_t n = 0; n < 256; ++n)
            {
                std::uint32_t c = n;
                for (int k = 0; k < 8; ++k) c = (c & 1u) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
                t[n] = c;
            }
            return t;
        }();
        crc = ~crc;
        for (std::size_t i = 0; i < size; ++i) crc = table[(crc ^ data[i]) & 0xFFu] ^ (crc >> 8);
        return ~crc;
    }

    class BitWriter
    {
    public:
        std::vector<std::uint8_t> bytes;
        void Bits(std::uint32_t value, int count)   // LSB first (deflate bit order)
        {
            for (int i = 0; i < count; ++i)
            {
                if (m_used == 0) bytes.push_back(0);
                bytes.back() |= static_cast<std::uint8_t>(((value >> i) & 1u) << m_used);
                m_used = (m_used + 1) & 7;
            }
        }
        void Huffman(std::uint32_t code, int length)   // Huffman codes go MSB first
        {
            std::uint32_t reversed = 0;
            for (int i = 0; i < length; ++i) reversed |= ((code >> i) & 1u) << (length - 1 - i);
            Bits(reversed, length);
        }
    private:
        int m_used = 0;
    };

    // Fixed-Huffman deflate with matches at distance 1 and 4 (a repeated byte / pixel) - templates
    // are mostly long runs of one colour, so this is enough to keep them small.
    std::vector<std::uint8_t> Deflate(const std::vector<std::uint8_t>& in)
    {
        static const int kLengthBase[29] = { 3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31, 35, 43, 51, 59,
                                             67, 83, 99, 115, 131, 163, 195, 227, 258 };
        static const int kLengthExtra[29] = { 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3,
                                              4, 4, 4, 4, 5, 5, 5, 5, 0 };
        BitWriter out;
        out.Bits(1, 1);   // BFINAL
        out.Bits(1, 2);   // BTYPE = fixed Huffman
        const auto literalLength = [&](int symbol) {
            if (symbol < 144) out.Huffman(0x30u + static_cast<std::uint32_t>(symbol), 8);
            else if (symbol < 256) out.Huffman(0x190u + static_cast<std::uint32_t>(symbol - 144), 9);
            else if (symbol < 280) out.Huffman(static_cast<std::uint32_t>(symbol - 256), 7);
            else out.Huffman(0xC0u + static_cast<std::uint32_t>(symbol - 280), 8);
        };
        std::size_t i = 0;
        while (i < in.size())
        {
            int bestLength = 0, bestDistance = 0;
            for (const int distance : { 1, 4 })
            {
                if (i < static_cast<std::size_t>(distance)) continue;
                int length = 0;
                while (length < 258 && i + length < in.size() && in[i + length] == in[i + length - distance]) ++length;
                if (length > bestLength) { bestLength = length; bestDistance = distance; }
            }
            if (bestLength < 3)
            {
                literalLength(in[i]);
                ++i;
                continue;
            }
            int code = 28;
            while (kLengthBase[code] > bestLength) --code;
            literalLength(257 + code);
            out.Bits(static_cast<std::uint32_t>(bestLength - kLengthBase[code]), kLengthExtra[code]);
            out.Huffman(bestDistance == 1 ? 0u : 3u, 5);   // distance codes 0 (=1) and 3 (=4), no extra bits
            i += static_cast<std::size_t>(bestLength);
        }
        literalLength(256);   // end of block
        return out.bytes;
    }

    // ---- canvas -------------------------------------------------------------
    class Image
    {
    public:
        Image(int width, int height) : m_w(width), m_h(height), m_px(static_cast<std::size_t>(width * height), kClear) {}
        int Width() const { return m_w; }
        int Height() const { return m_h; }

        void Put(int x, int y, Rgba c)
        {
            if (x < 0 || y < 0 || x >= m_w || y >= m_h) return;
            Rgba& dst = m_px[static_cast<std::size_t>(y * m_w + x)];
            if (c.a == 255 || dst.a == 0) { dst = c; return; }
            const float t = c.a / 255.0f;   // simple "over"
            dst = { static_cast<std::uint8_t>(c.r * t + dst.r * (1 - t)), static_cast<std::uint8_t>(c.g * t + dst.g * (1 - t)),
                    static_cast<std::uint8_t>(c.b * t + dst.b * (1 - t)), std::max(c.a, dst.a) };
        }
        // Every pixel whose centre passes `inside` gets `fill`, or `edge` when a 4-neighbour doesn't.
        void Fill(const std::function<bool(float, float)>& inside, Rgba fill, Rgba edge)
        {
            for (int y = 0; y < m_h; ++y)
                for (int x = 0; x < m_w; ++x)
                {
                    const float cx = x + 0.5f, cy = y + 0.5f;
                    if (!inside(cx, cy)) continue;
                    const bool inner = inside(cx + 1, cy) && inside(cx - 1, cy) && inside(cx, cy + 1) && inside(cx, cy - 1);
                    Put(x, y, inner ? fill : edge);
                }
        }
        void Border()
        {
            for (int x = 0; x < m_w; ++x) { Put(x, 0, kBorder); Put(x, m_h - 1, kBorder); }
            for (int y = 0; y < m_h; ++y) { Put(0, y, kBorder); Put(m_w - 1, y, kBorder); }
        }
        void Cross(float x, float y, int n = 3)
        {
            for (int d = -n; d <= n; ++d) { Put(static_cast<int>(x) + d, static_cast<int>(y), kPivot); Put(static_cast<int>(x), static_cast<int>(y) + d, kPivot); }
        }
        void Arrow(float x, float y, float length)   // pointing right (+x) = default facing
        {
            for (int d = 0; d < static_cast<int>(length); ++d) Put(static_cast<int>(x) + d, static_cast<int>(y), kArrow);
            for (int k = 1; k <= 3; ++k)
            {
                Put(static_cast<int>(x + length) - k, static_cast<int>(y) - k, kArrow);
                Put(static_cast<int>(x + length) - k, static_cast<int>(y) + k, kArrow);
            }
        }

        bool Save(const std::string& relative) const
        {
            std::vector<std::uint8_t> raw;
            raw.reserve(static_cast<std::size_t>(m_h * (1 + m_w * 4)));
            for (int y = 0; y < m_h; ++y)
            {
                raw.push_back(0);   // filter: none
                for (int x = 0; x < m_w; ++x)
                {
                    const Rgba& p = m_px[static_cast<std::size_t>(y * m_w + x)];
                    raw.insert(raw.end(), { p.r, p.g, p.b, p.a });
                }
            }
            std::vector<std::uint8_t> zlib{ 0x78, 0x01 };
            const std::vector<std::uint8_t> deflated = Deflate(raw);
            zlib.insert(zlib.end(), deflated.begin(), deflated.end());
            std::uint32_t a = 1, b = 0;   // adler32
            for (const std::uint8_t v : raw) { a = (a + v) % 65521u; b = (b + a) % 65521u; }
            const std::uint32_t adler = (b << 16) | a;
            for (int shift = 24; shift >= 0; shift -= 8) zlib.push_back(static_cast<std::uint8_t>(adler >> shift));

            std::vector<std::uint8_t> png{ 0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n' };
            const auto chunk = [&](const char* type, const std::vector<std::uint8_t>& data) {
                const std::uint32_t size = static_cast<std::uint32_t>(data.size());
                for (int shift = 24; shift >= 0; shift -= 8) png.push_back(static_cast<std::uint8_t>(size >> shift));
                const std::size_t typeAt = png.size();
                png.insert(png.end(), type, type + 4);
                png.insert(png.end(), data.begin(), data.end());
                const std::uint32_t crc = Crc32(png.data() + typeAt, 4 + data.size());
                for (int shift = 24; shift >= 0; shift -= 8) png.push_back(static_cast<std::uint8_t>(crc >> shift));
            };
            std::vector<std::uint8_t> header;
            for (const std::uint32_t v : { static_cast<std::uint32_t>(m_w), static_cast<std::uint32_t>(m_h) })
                for (int shift = 24; shift >= 0; shift -= 8) header.push_back(static_cast<std::uint8_t>(v >> shift));
            header.insert(header.end(), { 8, 6, 0, 0, 0 });   // 8-bit RGBA, no interlace
            chunk("IHDR", header);
            chunk("IDAT", zlib);
            chunk("IEND", {});

            const fs::path path = fs::path(kOut) / relative;
            fs::create_directories(path.parent_path());
            std::ofstream file(path, std::ios::binary);
            file.write(reinterpret_cast<const char*>(png.data()), static_cast<std::streamsize>(png.size()));
            if (!file) { std::fprintf(stderr, "cannot write %s\n", path.string().c_str()); return false; }
            std::printf("%-48s %4dx%-4d\n", path.generic_string().c_str(), m_w, m_h);
            return true;
        }

    private:
        int m_w, m_h;
        std::vector<Rgba> m_px;
    };

    // A disc of radius `r` centred on a (2r)-square canvas, pivot in the middle (projectiles, areas, fx).
    Image CircleImage(float r)
    {
        const int d = std::max(2, static_cast<int>(std::lround(2.0f * r)));
        Image im(d, d);
        const float c = d / 2.0f;
        im.Fill([&](float x, float y) { return (x - c) * (x - c) + (y - c) * (y - c) <= r * r; }, kHit, kHitEdge);
        im.Border();
        im.Cross(c, c, std::min(3, d / 4));
        return im;
    }

    // A body standing on the canvas bottom: collision disc `r` touching the bottom edge, bottom-centre
    // pivot (mobs, bosses - docs/circular-art-guide.md §4 "원점").
    Image StandingDisc(int canvas, float r)
    {
        Image im(canvas, canvas);
        const float cx = canvas / 2.0f, cy = canvas - r;
        im.Fill([&](float x, float y) { return (x - cx) * (x - cx) + (y - cy) * (y - cy) <= r * r; }, kHit, kHitEdge);
        im.Border();
        im.Cross(cx, cy, 2);
        im.Arrow(cx, cy - r - 3, canvas / 2.0f - 3);
        return im;
    }

    // Drops every generated template first, so a row removed from a CSV doesn't leave a stale one behind.
    void RemoveOldTemplates()
    {
        if (!fs::exists(kOut)) return;
        std::vector<fs::path> old;
        for (const fs::directory_entry& entry : fs::recursive_directory_iterator(kOut))
        {
            const std::string name = entry.path().filename().string();
            if (entry.is_regular_file() && name.size() > 13 && name.compare(name.size() - 13, 13, "_template.png") == 0)
                old.push_back(entry.path());
        }
        for (const fs::path& path : old) fs::remove(path);
    }
}

int main()
{
    CircularBalance balance;
    const BalanceLoadReport report = LoadCircularBalance(balance, 4096);
    for (const std::string& message : report.messages) std::printf("[balance] %s\n", message.c_str());
    if (report.errors > 0) { std::fprintf(stderr, "fix the CSV errors above first - templates would not match the game\n"); return 1; }

    RemoveOldTemplates();
    bool ok = true;

    // --- player: sprite canvas, hitbox standing `hitbox_lift` above the bottom (player.csv) ---
    {
        const PlayerTuning& p = balance.player;
        const int sw = static_cast<int>(p.spriteWidth), sh = static_cast<int>(p.spriteHeight);
        const int hw = static_cast<int>(p.hitboxWidth), hh = static_cast<int>(p.hitboxHeight);
        const int x0 = (sw - hw) / 2, y0 = sh - hh - static_cast<int>(p.hitboxLift);
        Image im(sw, sh);
        im.Fill([&](float x, float y) { return x0 <= x && x < x0 + hw && y0 <= y && y < y0 + hh; }, kHit, kHitEdge);
        im.Border();
        im.Cross(sw / 2.0f, y0 + hh / 2.0f);
        im.Arrow(sw / 2.0f, y0 - 8.0f, sw / 2.0f - 4.0f);
        ok &= im.Save("char/char_template.png");
    }

    // --- mobs: one per mobs.csv row; square canvas, 32 px for the small ones, grown in 8 px steps ---
    for (const MobDef& mob : balance.mobs)
    {
        const int canvas = std::max(32, 8 * static_cast<int>(std::ceil((2.0f * mob.radius + 8.0f) / 8.0f)));
        ok &= StandingDisc(canvas, mob.radius).Save("mob/mob_" + mob.id + "_template.png");
    }

    // --- bosses: walk + attack canvases, same body ---
    ok &= StandingDisc(kBossWalkCanvas, kBossRadius).Save("boss/boss_walk_template.png");
    ok &= StandingDisc(kBossAttackCanvas, kBossRadius).Save("boss/boss_atk_template.png");

    // --- weapons: one per weapons.csv row, level-1 hit size (1 px = 1 world unit, attack_size 1) ---
    for (const CardDef& w : balance.weapons)
    {
        const EffectSpec& spec = SpecOf(w.effect);
        const bool moving = EffectivePath(w) != AttackPath::None;
        const std::string file = "weapon/prj_" + w.id + "_template.png";
        if (spec.form == AttackForm::Nearest || spec.form == AttackForm::Projectile)
            ok &= CircleImage(w.hitRadius).Save(file);   // the flying body (BOLT: the travelling dot)
        else if (spec.shape == HitShapeKind::Circle)
            ok &= CircleImage(moving ? w.hitRadius : w.range).Save(file);
        else if (spec.shape == HitShapeKind::Arc)
        {
            const float r = w.range, half = w.coneHalfAngleDeg * 3.14159265f / 180.0f;
            const int d = static_cast<int>(std::lround(2.0f * r));
            const float c = d / 2.0f;
            Image im(d, d);
            im.Fill([&](float x, float y) {
                const float dx = x - c, dy = y - c, dist = std::sqrt(dx * dx + dy * dy);
                return dist <= r && (dist < 1.0f || std::acos(std::clamp(dx / dist, -1.0f, 1.0f)) <= half);
            }, kHit, kHitEdge);
            im.Border();
            im.Cross(c, c);
            im.Arrow(c, c, r * 0.5f);
            ok &= im.Save(file);
        }
        else   // Capsule: the player at the left end, reaching right
        {
            const float length = w.range, halfWidth = w.lineHalfWidth;
            Image im(static_cast<int>(std::lround(length + 2.0f * halfWidth)), static_cast<int>(std::lround(2.0f * halfWidth)));
            const float ox = halfWidth, oy = halfWidth;
            im.Fill([&](float x, float y) { return std::hypot(x - std::clamp(x, ox, ox + length), y - oy) <= halfWidth; }, kHit, kHitEdge);
            im.Border();
            im.Cross(ox, oy);
            im.Arrow(ox, oy, length * 0.5f);
            ok &= im.Save(file);
        }
        if (spec.onHit == OnHit::Explode) ok &= CircleImage(w.explodeRadius).Save("fx/fx_" + w.id + "_explode_template.png");
    }

    // --- generic: weapon/accessory icon 48x48 (4 px safe margin), hit effect 32x32 ---
    {
        Image icon(48, 48);
        icon.Fill([](float x, float y) { return 4 <= x && x < 44 && 4 <= y && y < 44; }, kClear, kHitEdge);
        icon.Border();
        ok &= icon.Save("weapon/wpn_icon_template.png");
        Image hit(32, 32);
        hit.Border();
        hit.Cross(16, 16);
        ok &= hit.Save("fx/fx_hit_template.png");
    }
    return ok ? 0 : 1;
}
