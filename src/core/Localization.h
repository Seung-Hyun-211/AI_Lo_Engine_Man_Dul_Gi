#pragma once

#include "core/NonCopyable.h"
#include "core/StringTable.h"

#include <set>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

// Active language + fallback chain on top of core::StringTable. Main thread
// only (screen builders and SnapshotBuilder); the render thread and job
// workers only ever see already-resolved text inside a RenderSnapshot.
// Contract: docs/localization-design.md §1, §6, §7.
namespace engine::core
{
    // The language every lookup falls back to, and the one shipped complete.
    inline constexpr std::string_view kDefaultLanguage{ "en" };

    namespace detail
    {
        [[nodiscard]] inline std::string ToText(const std::string& value) { return value; }
        [[nodiscard]] inline std::string ToText(std::string_view value) { return std::string{ value }; }
        [[nodiscard]] inline std::string ToText(const char* value) { return value != nullptr ? std::string{ value } : std::string{}; }

        // Numbers only - a float lands as std::to_string's "12.500000". Callers
        // that want other digits format it themselves and pass the string; this
        // is deliberately not a formatting mini-language.
        template <typename T>
            requires std::is_arithmetic_v<T>
        [[nodiscard]] std::string ToText(T value) { return std::to_string(value); }
    }

    // Non-copyable: a copy would duplicate both tables and hand out Get()
    // views into the copy, which the original's next Load then has no say
    // over. One owner (Application) is the whole intent.
    class Localization final : private NonCopyable
    {
    public:
        Localization() = default;

        // Loads assets/loc/<code>.txt as the active language and, once, the
        // default language as the fallback. Returns false when the requested
        // language cannot be read - lookups then resolve against the default
        // language, so a missing translation never blocks startup or blanks
        // the screen. `code` must come from the engine's language list
        // (Settings maps an unknown code back to the default before calling).
        bool Load(std::string_view languageCode);

        // Active language, else default language, else the key itself so the
        // gap is visible in-game rather than silently blank.
        // The result points into the loaded table and stays valid until the
        // next Load; on a miss it aliases `key`, so pass literals or strings
        // that outlive the call. Never hold it across frames.
        [[nodiscard]] std::string_view Get(std::string_view key) const;

        // Get() with `{0}`, `{1}`... replaced by the arguments. Numbered, not
        // positional, because word order differs per language - never build a
        // sentence by concatenating translated fragments.
        template <typename... Args>
        [[nodiscard]] std::string Format(std::string_view key, const Args&... args) const
        {
            const std::vector<std::string> values{ detail::ToText(args)... };
            return Substitute(Get(key), values);
        }

        [[nodiscard]] const std::string& ActiveLanguage() const { return m_activeCode; }

        // Distinct keys that resolved to nothing since the last Load, so a
        // debug build can dump what still needs translating.
        [[nodiscard]] const std::set<std::string>& MissedKeys() const { return m_missedKeys; }

    private:
        [[nodiscard]] static std::string Substitute(std::string_view pattern,
                                                    const std::vector<std::string>& values);

        StringTable m_active;      // empty when the active language IS the default
        StringTable m_fallback;    // the default language
        bool m_fallbackLoaded{ false };
        std::string m_activeCode{ kDefaultLanguage };

        // Mutable: Get() is a read, but recording a gap must not force every
        // caller to hold a non-const reference.
        mutable std::set<std::string> m_missedKeys;
    };
}
