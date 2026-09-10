#include "import/ImageFile.h"

// Declarations only - the implementation lives in vendor/stb/stb_image_impl.cpp.
// STBI_NO_STDIO here too so the header does not even declare the stdio entry
// points we deliberately did not compile.
#define STBI_NO_STDIO
#include "vendor/stb/stb_image.h"

#include <cctype>
#include <cstdint>
#include <cstdio>
#include <vector>

namespace engine::import
{
    namespace
    {
        std::string LowerExtension(const std::string& path)
        {
            const std::size_t dot = path.find_last_of('.');
            if (dot == std::string::npos) return {};
            std::string ext = path.substr(dot + 1);
            for (char& c : ext) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            return ext;
        }

        std::vector<std::uint8_t> ReadWholeFile(const std::string& path)
        {
            std::vector<std::uint8_t> bytes;
            std::FILE* file = nullptr;
            if (fopen_s(&file, path.c_str(), "rb") != 0 || file == nullptr) return bytes;

            std::fseek(file, 0, SEEK_END);
            const long size = std::ftell(file);
            std::fseek(file, 0, SEEK_SET);
            if (size > 0)
            {
                bytes.resize(static_cast<std::size_t>(size));
                if (std::fread(bytes.data(), 1, bytes.size(), file) != bytes.size()) bytes.clear();
            }
            std::fclose(file);
            return bytes;
        }
    }

    ImageData LoadImageFromFile(const std::string& path)
    {
        if (LowerExtension(path) == "tga") return LoadTga(path);

        ImageData image;
        const std::vector<std::uint8_t> file = ReadWholeFile(path);
        if (file.empty()) return image;

        int width = 0;
        int height = 0;
        int channelsInFile = 0;
        stbi_uc* pixels = stbi_load_from_memory(file.data(), static_cast<int>(file.size()),
                                                &width, &height, &channelsInFile, 4);   // force RGBA
        if (pixels == nullptr || width <= 0 || height <= 0)
        {
            if (pixels != nullptr) stbi_image_free(pixels);
            return image;
        }

        image.width = width;
        image.height = height;
        image.rgba.assign(pixels, pixels + static_cast<std::size_t>(width) * height * 4);   // top-down, straight alpha
        image.ok = true;
        stbi_image_free(pixels);
        return image;
    }
}
