#include "render/r2d/TextureAtlas.h"

#include <fstream>
#include <sstream>
#include <utility>

namespace engine::render
{
    bool AtlasIndex::Load(const std::string& atlasPath, std::uint32_t atlasId)
    {
        std::ifstream file;
        for (const std::string& prefix : { std::string{}, std::string{ "../../" }, std::string{ "../../../" } })
        {
            file.open(prefix + atlasPath);
            if (file.is_open()) break;
        }
        if (!file.is_open()) return false;

        std::unordered_map<std::string, SpriteRect> sprites;
        std::string line;
        while (std::getline(file, line))
        {
            if (line.empty() || line[0] == '#') continue;
            std::istringstream in(line);

            std::string name;
            SpriteRect rect;
            if (!(in >> name >> rect.page >> rect.u0 >> rect.v0 >> rect.u1 >> rect.v1
                     >> rect.pixelW >> rect.pixelH))
                continue;                       // malformed row - skip, keep parsing
            in >> rect.pivotX >> rect.pivotY;   // optional; leaves defaults if absent

            sprites.emplace(std::move(name), rect);
        }
        if (sprites.empty()) return false;

        m_sprites = std::move(sprites);
        m_atlasId = atlasId;
        return true;
    }

    const SpriteRect* AtlasIndex::Find(std::string_view name) const
    {
        const auto it = m_sprites.find(std::string(name));
        return it != m_sprites.end() ? &it->second : nullptr;
    }
}
