#include "core/Settings.h"

#include <fstream>
#include <stdexcept>

namespace engine::core
{
    namespace
    {
        [[nodiscard]] bool ParseBool(const std::string& value) { return value == "1"; }
    }

    Settings Settings::LoadOrDefault(const std::string& path)
    {
        Settings settings;   // defaults

        std::ifstream file(path);
        if (!file) return settings;

        std::string line;
        while (std::getline(file, line))
        {
            const std::size_t eq = line.find('=');
            if (eq == std::string::npos) continue;
            const std::string key = line.substr(0, eq);
            const std::string value = line.substr(eq + 1);

            try
            {
                if (key == "masterVolume") settings.masterVolume = std::stof(value);
                else if (key == "musicVolume") settings.musicVolume = std::stof(value);
                else if (key == "sfxVolume") settings.sfxVolume = std::stof(value);
                else if (key == "mouseSensitivity") settings.mouseSensitivity = std::stof(value);
                else if (key == "invertMouseY") settings.invertMouseY = ParseBool(value);
                else if (key == "vsync") settings.vsync = ParseBool(value);
                else if (key == "resolutionIndex") settings.resolutionIndex = std::stoi(value);
            }
            catch (const std::exception&)
            {
                // Malformed value for this one field - keep its default and
                // keep reading the rest of the file.
            }
        }

        if (settings.resolutionIndex < 0 || settings.resolutionIndex >= static_cast<int>(kResolutionPresets.size()))
            settings.resolutionIndex = 1;

        return settings;
    }

    void Settings::Save(const std::string& path) const
    {
        std::ofstream file(path, std::ios::trunc);
        if (!file) return;

        file << "masterVolume=" << masterVolume << '\n'
             << "musicVolume=" << musicVolume << '\n'
             << "sfxVolume=" << sfxVolume << '\n'
             << "mouseSensitivity=" << mouseSensitivity << '\n'
             << "invertMouseY=" << (invertMouseY ? 1 : 0) << '\n'
             << "vsync=" << (vsync ? 1 : 0) << '\n'
             << "resolutionIndex=" << resolutionIndex << '\n';
    }
}
