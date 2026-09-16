#pragma once

#include <cstddef>
#include <functional>
#include <string>
#include <string_view>
#include <unordered_map>

// One language's key -> UTF-8 value map, parsed from assets/loc/<code>.txt.
// Lookup only: which language is active, what to fall back to on a miss, and
// argument formatting all belong to core::Localization.
// Contract and file format: docs/localization-design.md §1, §3.
namespace engine::core
{
    namespace detail
    {
        // Transparent hash so Find() can take a string_view without building a
        // std::string per lookup - HUD strings are resolved every frame.
        struct StringViewHash
        {
            using is_transparent = void;

            [[nodiscard]] std::size_t operator()(std::string_view text) const noexcept
            {
                return std::hash<std::string_view>{}(text);
            }
        };
    }

    class StringTable
    {
    public:
        // Replaces the contents with the file's entries. Returns false only
        // when the file cannot be opened (the table is then empty), so the
        // caller can fall back to another language. Malformed lines are
        // skipped rather than thrown on - a broken translation must never
        // block startup, same policy as Settings::LoadOrDefault.
        bool LoadFromFile(const std::string& path);

        // The stored value, or nullptr when the key is absent. Valid until the
        // next LoadFromFile.
        [[nodiscard]] const std::string* Find(std::string_view key) const;

        [[nodiscard]] std::size_t Size() const { return m_entries.size(); }
        [[nodiscard]] bool Empty() const { return m_entries.empty(); }

    private:
        std::unordered_map<std::string, std::string, detail::StringViewHash, std::equal_to<>> m_entries;
    };
}
