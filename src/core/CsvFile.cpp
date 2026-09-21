#include "core/CsvFile.h"

#include <cctype>
#include <charconv>
#include <cmath>
#include <fstream>
#include <system_error>

namespace engine::core
{
    namespace
    {
        std::string Trim(std::string_view text)
        {
            std::size_t begin = 0;
            std::size_t end = text.size();
            while (begin < end && std::isspace(static_cast<unsigned char>(text[begin]))) ++begin;
            while (end > begin && std::isspace(static_cast<unsigned char>(text[end - 1]))) --end;
            return std::string(text.substr(begin, end - begin));
        }

        std::string Lower(std::string text)
        {
            for (char& c : text) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            return text;
        }

        // Splits one record. Supports "quoted, cells" and "" as an escaped quote.
        std::vector<std::string> SplitLine(const std::string& line)
        {
            std::vector<std::string> cells;
            std::string cell;
            bool quoted = false;
            for (std::size_t i = 0; i < line.size(); ++i)
            {
                const char c = line[i];
                if (quoted)
                {
                    if (c == '"')
                    {
                        if (i + 1 < line.size() && line[i + 1] == '"') { cell.push_back('"'); ++i; }
                        else quoted = false;
                    }
                    else cell.push_back(c);
                }
                else if (c == '"') quoted = true;
                else if (c == ',') { cells.push_back(Trim(cell)); cell.clear(); }
                else cell.push_back(c);
            }
            cells.push_back(Trim(cell));
            return cells;
        }
    }

    int CsvTable::Column(std::string_view name) const
    {
        const std::string wanted = Lower(std::string(name));
        for (std::size_t i = 0; i < header.size(); ++i)
            if (header[i] == wanted) return static_cast<int>(i);
        return -1;
    }

    bool LoadCsv(const std::string& path, CsvTable& out, std::string& error)
    {
        out = {};
        std::ifstream file(path, std::ios::binary);
        if (!file)
        {
            error = "cannot open " + path;
            return false;
        }

        std::string line;
        int lineNumber = 0;
        bool haveHeader = false;
        while (std::getline(file, line))
        {
            ++lineNumber;
            if (lineNumber == 1 && line.size() >= 3 &&
                static_cast<unsigned char>(line[0]) == 0xEF && static_cast<unsigned char>(line[1]) == 0xBB &&
                static_cast<unsigned char>(line[2]) == 0xBF)
                line.erase(0, 3);   // UTF-8 BOM (Excel "CSV UTF-8" writes one)
            if (!line.empty() && line.back() == '\r') line.pop_back();

            const std::string trimmed = Trim(line);
            if (trimmed.empty() || trimmed.front() == '#') continue;

            std::vector<std::string> cells = SplitLine(line);
            if (!haveHeader)
            {
                for (std::string& cell : cells) cell = Lower(std::move(cell));
                out.header = std::move(cells);
                haveHeader = true;
            }
            else
            {
                out.rows.push_back({ lineNumber, std::move(cells) });
            }
        }

        if (!haveHeader)
        {
            error = path + ": no header line";
            return false;
        }
        return true;
    }

    bool ParseFloat(std::string_view text, float& out)
    {
        while (!text.empty() && std::isspace(static_cast<unsigned char>(text.front()))) text.remove_prefix(1);
        while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back()))) text.remove_suffix(1);
        if (text.empty()) return false;
        if (text.front() == '+') text.remove_prefix(1);   // from_chars rejects a leading '+'

        float value = 0.0f;
        const auto [end, ec] = std::from_chars(text.data(), text.data() + text.size(), value);
        if (ec != std::errc{} || end != text.data() + text.size() || !std::isfinite(value)) return false;
        out = value;
        return true;
    }
}
