#include "core/AssetPaths.h"

#if defined(_WIN32)
#include <Windows.h>
#else
// Non-Windows is only for the headless tools (tools/balance_sim.cpp) in Linux
// cloud sessions - the game itself is Win32 (CLAUDE.md "빌드 / 실행").
#include <filesystem>
#endif

#include <array>
#include <string>

namespace engine::core
{
    namespace
    {
#if defined(_WIN32)
        constexpr const char* kSep = "\\";

        bool DirectoryExists(const std::string& path)
        {
            const DWORD attributes = GetFileAttributesA(path.c_str());
            return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
        }

        std::string ExecutableDir()
        {
            char buffer[MAX_PATH]{};
            const DWORD length = GetModuleFileNameA(nullptr, buffer, MAX_PATH);
            std::string path(buffer, length);
            const std::size_t slash = path.find_last_of("/\\");
            return slash == std::string::npos ? std::string{} : path.substr(0, slash);
        }

        std::string CurrentDir()
        {
            char cwd[MAX_PATH]{};
            GetCurrentDirectoryA(MAX_PATH, cwd);
            return cwd;
        }
#else
        constexpr const char* kSep = "/";

        bool DirectoryExists(const std::string& path)
        {
            std::error_code error;
            return std::filesystem::is_directory(path, error);
        }

        std::string ExecutableDir()
        {
            std::error_code error;
            const std::filesystem::path exe = std::filesystem::read_symlink("/proc/self/exe", error);
            return error ? std::string{} : exe.parent_path().string();
        }

        std::string CurrentDir()
        {
            std::error_code error;
            return std::filesystem::current_path(error).string();
        }
#endif

        // Walk up `start`, checking `start/assets`, `start/../assets`, ... Returns
        // the folder that contains `assets/`, or "" if none within a few levels.
        std::string FindAssetsFrom(std::string start)
        {
            for (int level = 0; level < 6 && !start.empty(); ++level)
            {
                if (DirectoryExists(start + kSep + "assets")) return start + kSep + "assets";
                const std::size_t slash = start.find_last_of("/\\");
                if (slash == std::string::npos) break;
                start = start.substr(0, slash);
            }
            return {};
        }

        std::string LocateAssetRoot()
        {
            for (const std::string& start : { CurrentDir(), ExecutableDir() })
            {
                std::string found = FindAssetsFrom(start);
                if (!found.empty()) return found;
            }
            return {};
        }

        const std::string& CachedRoot()
        {
            static const std::string root = LocateAssetRoot();
            return root;
        }
    }

    std::string AssetRoot() { return CachedRoot(); }

    std::string ResolveAsset(std::string_view relative)
    {
        const std::string& root = CachedRoot();
        if (root.empty()) return std::string(relative);

        std::string tail(relative);
        // `assets/` prefix is implied by the root; strip it if the caller included it.
        if (tail.rfind("assets/", 0) == 0) tail.erase(0, 7);
        else if (tail.rfind("assets\\", 0) == 0) tail.erase(0, 7);

        return root + kSep + tail;
    }
}
