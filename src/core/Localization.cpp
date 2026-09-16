#include "core/Localization.h"

#include "core/AssetPaths.h"

namespace engine::core
{
    namespace
    {
        // The code becomes a file name, and it reaches us from a hand-editable
        // settings file, so keep it to the shape a real language tag has.
        [[nodiscard]] bool IsValidLanguageCode(std::string_view code)
        {
            if (code.empty() || code.size() > 16) return false;
            for (const char c : code)
            {
                const bool allowed = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')
                                  || (c >= '0' && c <= '9') || c == '-' || c == '_';
                if (!allowed) return false;
            }
            return true;
        }

        [[nodiscard]] std::string PathFor(std::string_view code)
        {
            std::string relative{ "loc/" };
            relative.append(code);
            relative.append(".txt");
            return ResolveAsset(relative);
        }
    }

    bool Localization::Load(std::string_view languageCode)
    {
        if (!IsValidLanguageCode(languageCode)) languageCode = kDefaultLanguage;

        // The fallback is the safety net for every lookup, so it is loaded once
        // and kept across language changes.
        if (!m_fallbackLoaded)
        {
            m_fallback.LoadFromFile(PathFor(kDefaultLanguage));
            m_fallbackLoaded = true;
        }

        m_activeCode.assign(languageCode);
        m_missedKeys.clear();   // each language has its own gaps

        if (languageCode == kDefaultLanguage)
        {
            m_active = {};      // lookups go straight to the fallback
            return !m_fallback.Empty();
        }

        if (m_active.LoadFromFile(PathFor(languageCode))) return true;

        // Unreadable: LoadFromFile already left the table empty, so every
        // lookup falls through to the default language.
        return false;
    }

    std::string_view Localization::Get(std::string_view key) const
    {
        if (const std::string* active = m_active.Find(key)) return *active;
        if (const std::string* fallback = m_fallback.Find(key)) return *fallback;

        m_missedKeys.emplace(key);
        return key;
    }

    std::string Localization::Substitute(std::string_view pattern, const std::vector<std::string>& values)
    {
        if (values.empty()) return std::string{ pattern };

        std::string out;
        out.reserve(pattern.size());

        for (std::size_t i = 0; i < pattern.size(); ++i)
        {
            if (pattern[i] != '{') { out.push_back(pattern[i]); continue; }

            std::size_t digits = i + 1;
            std::size_t index = 0;
            while (digits < pattern.size() && pattern[digits] >= '0' && pattern[digits] <= '9')
            {
                index = index * 10 + static_cast<std::size_t>(pattern[digits] - '0');
                ++digits;
                if (index > values.size()) break;   // out of range already; stop accumulating
            }

            // Anything that is not exactly `{<digits>}` with an argument behind
            // it is copied through, so a translator's stray brace or a stale
            // `{3}` stays visible instead of eating the rest of the line.
            const bool resolved = digits > i + 1 && digits < pattern.size()
                               && pattern[digits] == '}' && index < values.size();
            if (!resolved) { out.push_back(pattern[i]); continue; }

            out.append(values[index]);
            i = digits;   // the loop's ++i steps past '}'
        }

        return out;
    }
}
