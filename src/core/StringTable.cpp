#include "core/StringTable.h"

#include <fstream>

namespace engine::core
{
    namespace
    {
        constexpr std::string_view kUtf8Bom{ "\xEF\xBB\xBF" };

        [[nodiscard]] std::string_view TrimAscii(std::string_view text)
        {
            const auto isBlank = [](char c) { return c == ' ' || c == '\t'; };
            while (!text.empty() && isBlank(text.front())) text.remove_prefix(1);
            while (!text.empty() && isBlank(text.back())) text.remove_suffix(1);
            return text;
        }

        // `\n` and `\\` only. Any other backslash pair is copied byte for byte
        // so an unknown sequence stays visible in-game instead of silently
        // losing characters (docs/localization-design.md §3).
        [[nodiscard]] std::string Unescape(std::string_view value)
        {
            std::string out;
            out.reserve(value.size());
            for (std::size_t i = 0; i < value.size(); ++i)
            {
                if (value[i] != '\\' || i + 1 == value.size())
                {
                    out.push_back(value[i]);
                    continue;
                }

                const char next = value[i + 1];
                if (next == 'n')       { out.push_back('\n'); ++i; }
                else if (next == '\\') { out.push_back('\\'); ++i; }
                else                     out.push_back(value[i]);
            }
            return out;
        }
    }

    bool StringTable::LoadFromFile(const std::string& path)
    {
        m_entries.clear();

        std::ifstream file(path);
        if (!file) return false;

        std::string line;
        bool firstLine = true;
        while (std::getline(file, line))
        {
            std::string_view text{ line };

            if (firstLine)
            {
                firstLine = false;
                if (text.starts_with(kUtf8Bom)) text.remove_prefix(kUtf8Bom.size());
            }

            // A CRLF file can reach us with the '\r' still attached; strip it
            // here rather than relying on the stream's translation mode.
            if (!text.empty() && text.back() == '\r') text.remove_suffix(1);

            const std::string_view probe = TrimAscii(text);
            if (probe.empty() || probe.front() == '#') continue;

            const std::size_t equals = text.find('=');
            if (equals == std::string_view::npos) continue;   // not key=value

            const std::string_view key = TrimAscii(text.substr(0, equals));
            if (key.empty()) continue;

            // Only the key is trimmed: spacing inside a value can be
            // deliberate, so it survives verbatim. Splitting on the first '='
            // also leaves any later '=' as part of the value.
            const std::string_view value = text.substr(equals + 1);

            // Last one wins, matching the "later overrides earlier" rule a
            // per-language override file would need.
            m_entries.insert_or_assign(std::string{ key }, Unescape(value));
        }

        return true;
    }

    const std::string* StringTable::Find(std::string_view key) const
    {
        const auto entry = m_entries.find(key);
        return entry != m_entries.end() ? &entry->second : nullptr;
    }
}
