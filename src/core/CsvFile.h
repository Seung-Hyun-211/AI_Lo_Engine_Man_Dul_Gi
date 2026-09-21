#pragma once

#include <string>
#include <string_view>
#include <vector>

// Minimal CSV reader for designer-editable data tables (docs/circular-balance.md).
// Excel / Google Sheets / a text editor can all write what it reads.
//
// Format: UTF-8 (BOM tolerated), LF or CRLF, one record per line, comma
// separated, optional "double quotes" around a cell (needed only when it holds
// a comma; no embedded newlines). Blank lines and lines whose first non-space
// character is '#' are skipped. The first remaining line is the header.
// Cells are trimmed. No locale: numbers use '.' as the decimal point.
namespace engine::core
{
    struct CsvRow
    {
        int line{ 0 };                       // 1-based file line, for error messages
        std::vector<std::string> cells;
    };

    struct CsvTable
    {
        std::vector<std::string> header;     // lower-cased, trimmed
        std::vector<CsvRow> rows;

        // Index of the header column named `name` (case-insensitive), or -1.
        [[nodiscard]] int Column(std::string_view name) const;
    };

    // Reads `path` (an absolute/relative filesystem path - resolve assets with
    // core::ResolveAsset first). Returns false and sets `error` when the file
    // cannot be opened or has no header line.
    [[nodiscard]] bool LoadCsv(const std::string& path, CsvTable& out, std::string& error);

    // Locale-independent float parse of a whole cell. False on empty/garbage/
    // trailing junk/NaN/inf.
    [[nodiscard]] bool ParseFloat(std::string_view text, float& out);
}
