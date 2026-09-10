#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>

// 2D render module. CPU-side atlas manifest: maps a sprite name to its sub-rect
// on an atlas page. Loaded once at startup on the main thread; the UI / snapshot
// builder reads it to fill SpriteDraw::u0..v1 before the value crosses to the
// render thread (which only needs SpriteDraw::atlasId to bind the page's SRV).
//
// The page pixels (.dds) are loaded separately by SpritePass2D on the render
// thread. Build / bundle / format rules: docs/atlas-build-pipeline.md.
namespace engine::render
{
    // atlasId 0 is reserved for SpritePass2D's built-in 1x1 white texture, so a
    // solid tinted rect is a white sprite. The first real atlas gets id 1.
    inline constexpr std::uint32_t kNoAtlas = 0;
    inline constexpr std::uint32_t kUiAtlasId = 1;

    struct SpriteRect
    {
        int   page{ 0 };
        float u0{ 0.0f }, v0{ 0.0f }, u1{ 1.0f }, v1{ 1.0f };
        float pixelW{ 0.0f }, pixelH{ 0.0f };
        float pivotX{ 0.5f }, pivotY{ 0.5f };
    };

    class AtlasIndex final
    {
    public:
        // Parses a text ".atlas" manifest (one sprite per line:
        //   name  page  u0 v0 u1 v1  pixelW pixelH  [pivotX pivotY]
        // '#' and blank lines ignored). Tries the path as given, then a couple of
        // parent-dir prefixes. Returns false and stays empty on failure.
        bool Load(const std::string& atlasPath, std::uint32_t atlasId);

        [[nodiscard]] const SpriteRect* Find(std::string_view name) const;
        [[nodiscard]] std::uint32_t AtlasId() const { return m_atlasId; }
        [[nodiscard]] bool Loaded() const { return !m_sprites.empty(); }

    private:
        std::unordered_map<std::string, SpriteRect> m_sprites;
        std::uint32_t m_atlasId{ kNoAtlas };
    };
}
