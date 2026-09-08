#include "import/CreaseLines.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <unordered_map>
#include <utility>

namespace engine::import
{
    namespace
    {
        constexpr float kPi = 3.14159265358979323846f;

        struct Tri
        {
            std::uint32_t c0, c1, c2;   // original corner indices into mesh.vertices
            math::Vec3 normal;          // geometric face normal
            math::Color color;          // centroid surface colour
        };

        // Packs a quantised position into one key. Range ~±100 m at the default
        // weld epsilon; distant verts could collide but not within one model.
        std::int64_t WeldKey(const math::Vec3& p, float inv)
        {
            auto pack = [](long long v) { return static_cast<std::uint64_t>(v + (1 << 20)) & 0x1FFFFFull; };
            return static_cast<std::int64_t>(
                (pack(std::llround(p.x * inv)) << 42) |
                (pack(std::llround(p.y * inv)) << 21) |
                 pack(std::llround(p.z * inv)));
        }

        math::Color SampleTexture(const TgaImage* tex, math::Vec2 uv)
        {
            if (tex == nullptr || !tex->ok || tex->rgba.empty() || tex->width <= 0 || tex->height <= 0)
                return { 1.0f, 1.0f, 1.0f, 1.0f };

            float u = uv.x - std::floor(uv.x);
            float v = (1.0f - uv.y);
            v -= std::floor(v);
            const int x = std::clamp(static_cast<int>(u * static_cast<float>(tex->width)), 0, tex->width - 1);
            const int y = std::clamp(static_cast<int>(v * static_cast<float>(tex->height)), 0, tex->height - 1);
            const std::uint8_t* p = &tex->rgba[(static_cast<std::size_t>(y) * tex->width + x) * 4];
            return { p[0] / 255.0f, p[1] / 255.0f, p[2] / 255.0f, 1.0f };
        }

        math::Color Desaturate(math::Color c, float saturationScale, float valueScale)
        {
            const float luma = c.r * 0.299f + c.g * 0.587f + c.b * 0.114f;
            return {
                (luma + (c.r - luma) * saturationScale) * valueScale,
                (luma + (c.g - luma) * saturationScale) * valueScale,
                (luma + (c.b - luma) * saturationScale) * valueScale,
                1.0f,
            };
        }
    }

    std::vector<CreaseVertex> BuildCreaseLines(const ModelMesh& mesh, const TgaImage* texture,
                                              const ModelMaterial* material, const CreaseOptions& options)
    {
        std::vector<CreaseVertex> out;
        if (mesh.indices.size() < 3) return out;

        const math::Color materialColor = material != nullptr ? material->baseColor : math::Color{ 1, 1, 1, 1 };
        const float invWeld = options.weldEpsilon > 0.0f ? 1.0f / options.weldEpsilon : 1.0e4f;

        // 1) weld vertex ids by position
        std::unordered_map<std::int64_t, std::uint32_t> weldMap;
        weldMap.reserve(mesh.vertices.size());
        std::vector<std::uint32_t> weldOf(mesh.vertices.size(), 0);
        for (std::size_t i = 0; i < mesh.vertices.size(); ++i)
        {
            const std::int64_t key = WeldKey(mesh.vertices[i].position, invWeld);
            const auto it = weldMap.try_emplace(key, static_cast<std::uint32_t>(weldMap.size())).first;
            weldOf[i] = it->second;
        }

        // 2) per-triangle geometric normal + centroid colour
        const std::size_t triCount = mesh.indices.size() / 3;
        std::vector<Tri> tris;
        tris.reserve(triCount);
        for (std::size_t t = 0; t < triCount; ++t)
        {
            const std::uint32_t i0 = mesh.indices[t * 3 + 0];
            const std::uint32_t i1 = mesh.indices[t * 3 + 1];
            const std::uint32_t i2 = mesh.indices[t * 3 + 2];
            const math::Vec3 p0 = mesh.vertices[i0].position;
            const math::Vec3 p1 = mesh.vertices[i1].position;
            const math::Vec3 p2 = mesh.vertices[i2].position;

            math::Vec3 normal = math::Cross(p1 - p0, p2 - p0);
            const float len = math::Length(normal);
            normal = len > 1.0e-12f ? normal * (1.0f / len) : math::Vec3{ 0.0f, 1.0f, 0.0f };

            const math::Vec2 uv{
                (mesh.vertices[i0].uv.x + mesh.vertices[i1].uv.x + mesh.vertices[i2].uv.x) / 3.0f,
                (mesh.vertices[i0].uv.y + mesh.vertices[i1].uv.y + mesh.vertices[i2].uv.y) / 3.0f,
            };
            math::Color faceColor = SampleTexture(texture, uv);
            faceColor.r *= materialColor.r;
            faceColor.g *= materialColor.g;
            faceColor.b *= materialColor.b;

            tris.push_back({ i0, i1, i2, normal, faceColor });
        }

        // 3) edge (welded) -> up to two triangles
        struct EdgeRef { std::uint32_t tri; std::uint32_t a; std::uint32_t b; };
        std::unordered_map<std::uint64_t, std::array<EdgeRef, 2>> edges;
        std::unordered_map<std::uint64_t, int> edgeCount;
        edges.reserve(triCount * 2);
        edgeCount.reserve(triCount * 2);

        auto edgeKey = [](std::uint32_t x, std::uint32_t y) {
            if (x > y) std::swap(x, y);
            return (static_cast<std::uint64_t>(x) << 32) | y;
        };

        for (std::uint32_t t = 0; t < static_cast<std::uint32_t>(tris.size()); ++t)
        {
            const std::uint32_t corner[3] = { tris[t].c0, tris[t].c1, tris[t].c2 };
            for (int e = 0; e < 3; ++e)
            {
                const std::uint32_t ca = corner[e];
                const std::uint32_t cb = corner[(e + 1) % 3];
                const std::uint64_t key = edgeKey(weldOf[ca], weldOf[cb]);
                int& count = edgeCount[key];
                if (count < 2) edges[key][static_cast<std::size_t>(count)] = { t, ca, cb };
                ++count;
            }
        }

        // 4) emit a ribbon for each 2-triangle edge over the threshold
        const float degPerRad = 180.0f / kPi;
        for (const auto& [key, count] : edgeCount)
        {
            if (count != 2) continue;
            const std::array<EdgeRef, 2>& pair = edges[key];
            const Tri& ta = tris[pair[0].tri];
            const Tri& tb = tris[pair[1].tri];

            const float cosAngle = math::Clamp(math::Dot(ta.normal, tb.normal), -1.0f, 1.0f);
            const float angleDeg = std::acos(cosAngle) * degPerRad;
            if (angleDeg <= options.thresholdDegrees) continue;

            const float span = std::max(1.0f, 180.0f - options.thresholdDegrees);
            const float lerpT = math::Clamp((angleDeg - options.thresholdDegrees) / span, 0.0f, 1.0f);
            const float halfWidth = options.minHalfWidth + (options.maxHalfWidth - options.minHalfWidth) * lerpT;

            const math::Color line = Desaturate({
                (ta.color.r + tb.color.r) * 0.5f,
                (ta.color.g + tb.color.g) * 0.5f,
                (ta.color.b + tb.color.b) * 0.5f,
                1.0f,
            }, options.saturationScale, options.valueScale);

            const math::Vec3 edgeA = mesh.vertices[pair[0].a].position;
            const math::Vec3 edgeB = mesh.vertices[pair[0].b].position;
            math::Vec3 edgeDir = edgeB - edgeA;
            const float edgeLen = math::Length(edgeDir);
            if (edgeLen < 1.0e-9f) continue;
            edgeDir = edgeDir * (1.0f / edgeLen);

            math::Vec3 fold = ta.normal + tb.normal;
            const float foldLen = math::Length(fold);
            fold = foldLen > 1.0e-9f ? fold * (1.0f / foldLen) : ta.normal;

            math::Vec3 side = math::Cross(edgeDir, fold);
            const float sideLen = math::Length(side);
            if (sideLen < 1.0e-9f) continue;
            side = side * (1.0f / sideLen);

            const math::Vec3 offset = fold * options.surfaceOffset;
            const math::Vec3 a0 = edgeA + offset - side * halfWidth;
            const math::Vec3 a1 = edgeA + offset + side * halfWidth;
            const math::Vec3 b0 = edgeB + offset - side * halfWidth;
            const math::Vec3 b1 = edgeB + offset + side * halfWidth;

            for (const math::Vec3& p : { a0, a1, b1, a0, b1, b0 })
                out.push_back({ p, line });

            if (out.size() >= options.maxVertices) break;
        }

        return out;
    }

    std::vector<math::Vec3> BuildSmoothNormals(const ModelMesh& mesh, float weldEpsilon)
    {
        std::vector<math::Vec3> result(mesh.vertices.size(), math::Vec3{ 0.0f, 1.0f, 0.0f });
        if (mesh.indices.size() < 3) return result;

        const float invWeld = weldEpsilon > 0.0f ? 1.0f / weldEpsilon : 1.0e4f;

        std::unordered_map<std::int64_t, std::uint32_t> weldMap;
        weldMap.reserve(mesh.vertices.size());
        std::vector<std::uint32_t> weldOf(mesh.vertices.size(), 0);
        for (std::size_t i = 0; i < mesh.vertices.size(); ++i)
        {
            const std::int64_t key = WeldKey(mesh.vertices[i].position, invWeld);
            weldOf[i] = weldMap.try_emplace(key, static_cast<std::uint32_t>(weldMap.size())).first->second;
        }

        // Accumulate un-normalised (area-weighted) face normals per welded id.
        std::vector<math::Vec3> accum(weldMap.size(), math::Vec3{});
        const std::size_t triCount = mesh.indices.size() / 3;
        for (std::size_t t = 0; t < triCount; ++t)
        {
            const std::uint32_t i0 = mesh.indices[t * 3 + 0];
            const std::uint32_t i1 = mesh.indices[t * 3 + 1];
            const std::uint32_t i2 = mesh.indices[t * 3 + 2];
            const math::Vec3 faceNormal = math::Cross(
                mesh.vertices[i1].position - mesh.vertices[i0].position,
                mesh.vertices[i2].position - mesh.vertices[i0].position);
            accum[weldOf[i0]] = accum[weldOf[i0]] + faceNormal;
            accum[weldOf[i1]] = accum[weldOf[i1]] + faceNormal;
            accum[weldOf[i2]] = accum[weldOf[i2]] + faceNormal;
        }

        for (std::size_t i = 0; i < mesh.vertices.size(); ++i)
        {
            const math::Vec3 n = accum[weldOf[i]];
            const float len = math::Length(n);
            result[i] = len > 1.0e-9f ? n * (1.0f / len) : mesh.vertices[i].normal;
        }
        return result;
    }
}
