#pragma once

#include <cstddef>
#include <fstream>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

/// @file doc_table.hpp
/// @brief Bounded, single-pass Markdown helpers shared by the docs-drift unit
/// tests (subtasks 16/17).
///
/// Every scan here is a linear walk with explicit bounds and deliberately uses
/// no `std::regex` (and no nested quantifiers), so the readers cannot blow up
/// on hostile input. They never see hostile input - the inputs are committed
/// docs - but the discipline keeps the parsers auditable and the same shape as
/// the mirror helpers in `03_command_catalog_matches_registry.cpp`.

namespace doc_table
{

/// @brief Replaces every CRLF with a single LF.
///
/// Lets a Windows (autocrlf) checkout of a committed Markdown file be compared
/// on the same footing as an LF checkout.
///
/// @param text Raw file bytes.
/// @return @p text with every `"\r\n"` collapsed to `"\n"`; lone `\r`
/// characters (none appear in the committed docs) are preserved.
inline std::string normalize_crlf(const std::string& text)
{
    std::string out;
    out.reserve(text.size());
    for (std::size_t i = 0; i < text.size(); ++i) {
        if (text[i] == '\r' && i + 1 < text.size() && text[i + 1] == '\n') {
            continue;
        }
        out += text[i];
    }
    return out;
}

/// @brief Reads an entire file in binary mode.
///
/// @param path Absolute path to read.
/// @return The file bytes on success; `std::nullopt` when the file cannot be
/// opened, which the caller turns into a required-failure (never a silent
/// pass).
inline std::optional<std::string> read_file(const std::string& path)
{
    std::ifstream in(path, std::ios::binary);
    if (!in.good()) {
        return std::nullopt;
    }
    std::ostringstream buffer;
    buffer << in.rdbuf();
    return buffer.str();
}

/// @brief Trims ASCII spaces and tabs from both ends of a view.
///
/// @param s View to trim.
/// @return A copy of @p s with leading and trailing spaces/tabs removed.
inline std::string trim(std::string_view s)
{
    std::size_t begin = 0;
    std::size_t end = s.size();
    while (begin < end && (s[begin] == ' ' || s[begin] == '\t')) {
        ++begin;
    }
    while (end > begin && (s[end - 1] == ' ' || s[end - 1] == '\t')) {
        --end;
    }
    return std::string(s.substr(begin, end - begin));
}

/// @brief Splits @p text into lines on `\n`.
///
/// @param text Text to split.
/// @return One entry per line; a trailing newline does not produce a final
/// empty line.
inline std::vector<std::string> split_lines(const std::string& text)
{
    std::vector<std::string> lines;
    std::string current;
    for (const char c : text) {
        if (c == '\n') {
            lines.push_back(current);
            current.clear();
        }
        else {
            current += c;
        }
    }
    if (!current.empty()) {
        lines.push_back(current);
    }
    return lines;
}

/// @brief Extracts every backtick-delimited token from @p line, in order.
///
/// A token is the text strictly between a pair of backticks; an unterminated
/// backtick at end of line yields no further token. This lifts route patterns,
/// command names and path segments out of a table cell without depending on the
/// surrounding punctuation.
///
/// @param line Line (or cell) to scan.
/// @return The backtick-delimited tokens in left-to-right order.
inline std::vector<std::string> backtick_tokens(std::string_view line)
{
    std::vector<std::string> tokens;
    std::size_t i = 0;
    while (i < line.size()) {
        if (line[i] != '`') {
            ++i;
            continue;
        }
        const std::size_t close = line.find('`', i + 1);
        if (close == std::string_view::npos) {
            break;
        }
        tokens.emplace_back(line.substr(i + 1, close - (i + 1)));
        i = close + 1;
    }
    return tokens;
}

/// @brief Splits one Markdown table row into trimmed cells.
///
/// Treats a backslash-pipe (`\|`, the GitHub escape used inside cells) as a
/// literal pipe, never a column separator. The leading and trailing empty cells
/// produced by the outer `|` delimiters are dropped, so `| a | b |` yields
/// `{"a", "b"}`.
///
/// @param row The raw table-row line.
/// @return The trimmed interior cells, left to right.
inline std::vector<std::string> table_cells(std::string_view row)
{
    std::vector<std::string> cells;
    std::string current;
    for (std::size_t i = 0; i < row.size(); ++i) {
        if (row[i] == '\\' && i + 1 < row.size() && row[i + 1] == '|') {
            current += '|';
            ++i;
            continue;
        }
        if (row[i] == '|') {
            cells.push_back(trim(current));
            current.clear();
            continue;
        }
        current += row[i];
    }
    cells.push_back(trim(current));
    if (!cells.empty() && cells.front().empty()) {
        cells.erase(cells.begin());
    }
    if (!cells.empty() && cells.back().empty()) {
        cells.pop_back();
    }
    return cells;
}

/// @brief Tests whether @p line is a Markdown table separator row.
///
/// @param line Candidate line.
/// @return `true` when @p line is a `|---|---|`-style separator (only `|`, `-`,
/// `:` and spaces after a leading `|`); `false` otherwise.
inline bool is_separator_row(const std::string& line)
{
    const std::string t = trim(line);
    if (t.empty() || t.front() != '|') {
        return false;
    }
    for (const char c : t) {
        if (c != '|' && c != '-' && c != ' ' && c != ':') {
            return false;
        }
    }
    return true;
}

/// @brief Tests whether @p line opens a Markdown heading of any level.
///
/// @param line Candidate line.
/// @return `true` when @p line begins with `#`; `false` otherwise.
inline bool is_heading(const std::string& line)
{
    return !line.empty() && line.front() == '#';
}

}  // namespace doc_table
