#include <url_shortener/commands/pair_line_parser.hpp>

#include <string>

#include <url_shortener/core/utils.hpp>

namespace url_shortener::commands
{

namespace
{

/// Error code reported for every malformed line, in either format (plan C6:
/// code only, never the offending content).
constexpr const char* k_invalid_field = "invalid_field";

/// UTF-8 byte-order mark stripped from the first physical line.
constexpr std::string_view k_utf8_bom = "\xEF\xBB\xBF";

/**
 * @brief Reports whether @p c is ASCII whitespace for trimming/blank checks.
 *
 * @param c The byte to test.
 * @return `true` for space, tab, CR, LF, form feed or vertical tab.
 */
bool is_space(char c)
{
    return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\f' ||
           c == '\v';
}

/**
 * @brief Returns @p text with surrounding ASCII whitespace removed, without
 * allocating.
 *
 * @param text The view to trim; the result aliases its storage.
 * @return A sub-view of @p text with leading/trailing whitespace stripped;
 * empty when @p text is all whitespace.
 */
std::string_view trim_view(std::string_view text)
{
    std::size_t begin = 0;
    std::size_t end = text.size();
    while (begin < end && is_space(text[begin]))
    {
        ++begin;
    }
    while (end > begin && is_space(text[end - 1]))
    {
        --end;
    }
    return text.substr(begin, end - begin);
}

/**
 * @brief Reports whether @p line is a recognized CSV header.
 *
 * @param line The already BOM/CR-stripped line text.
 * @return `true` when the trimmed line equals, ASCII case-insensitively,
 * `"full_url,slug"` or `"full_url"`.
 */
bool is_csv_header(std::string_view line)
{
    const std::string_view trimmed = trim_view(line);

    auto equals_ci = [](std::string_view a, std::string_view b)
    {
        if (a.size() != b.size())
        {
            return false;
        }
        for (std::size_t i = 0; i < a.size(); ++i)
        {
            char lhs = a[i];
            char rhs = b[i];
            if (lhs >= 'A' && lhs <= 'Z')
            {
                lhs = static_cast<char>(lhs - 'A' + 'a');
            }
            if (rhs >= 'A' && rhs <= 'Z')
            {
                rhs = static_cast<char>(rhs - 'A' + 'a');
            }
            if (lhs != rhs)
            {
                return false;
            }
        }
        return true;
    };

    return equals_ci(trimmed, "full_url,slug") || equals_ci(trimmed, "full_url");
}

/**
 * @brief Removes trailing spaces and tabs from @p field in place.
 *
 * Used to trim the right edge of an unquoted CSV field; the left edge is
 * trimmed by skipping leading whitespace in the tokenizer's field-start state.
 *
 * @param field The accumulated unquoted field text, trimmed in place.
 */
void rstrip_inplace(std::string& field)
{
    std::size_t end = field.size();
    while (end > 0 && (field[end - 1] == ' ' || field[end - 1] == '\t'))
    {
        --end;
    }
    field.resize(end);
}

/// Internal state of the line-based CSV tokenizer.
enum class CsvState
{
    ///< At the start of a field, consuming leading spaces/tabs.
    field_start,

    ///< Inside an unquoted field.
    unquoted,

    ///< Inside a double-quoted field.
    quoted,

    ///< After a field's closing quote, consuming trailing spaces/tabs only.
    after_quote
};

/**
 * @brief Tokenizes one CSV line into fields per the RFC 4180 line-based subset.
 *
 * @param line The BOM/CR-stripped line text.
 * @param fields Receives the tokenized fields (cleared first) on success.
 * @return `true` on a well-formed line; `false` for an unterminated quote or a
 * stray quote in the middle of an unquoted field. Field-count and empty-URL
 * rules are checked by the caller.
 */
bool tokenize_csv(std::string_view line, std::vector<std::string>& fields)
{
    fields.clear();
    std::string current;
    CsvState state = CsvState::field_start;

    for (std::size_t i = 0; i < line.size(); ++i)
    {
        const char c = line[i];
        switch (state)
        {
        case CsvState::field_start:
            if (c == ' ' || c == '\t')
            {
                break;  // trim leading whitespace outside quotes
            }
            if (c == '"')
            {
                state = CsvState::quoted;
                break;
            }
            if (c == ',')
            {
                fields.emplace_back();  // empty field, advance
                break;
            }
            current.push_back(c);
            state = CsvState::unquoted;
            break;

        case CsvState::unquoted:
            if (c == ',')
            {
                rstrip_inplace(current);
                fields.push_back(std::move(current));
                current.clear();
                state = CsvState::field_start;
                break;
            }
            if (c == '"')
            {
                return false;  // quote in the middle of an unquoted field
            }
            current.push_back(c);
            break;

        case CsvState::quoted:
            if (c == '"')
            {
                if (i + 1 < line.size() && line[i + 1] == '"')
                {
                    current.push_back('"');  // "" escaped quote
                    ++i;
                }
                else
                {
                    state = CsvState::after_quote;
                }
                break;
            }
            current.push_back(c);  // comma/newline-free data, verbatim
            break;

        case CsvState::after_quote:
            if (c == ' ' || c == '\t')
            {
                break;  // trim trailing whitespace outside quotes
            }
            if (c == ',')
            {
                fields.push_back(std::move(current));
                current.clear();
                state = CsvState::field_start;
                break;
            }
            return false;  // stray data after a closing quote
        }
    }

    switch (state)
    {
    case CsvState::field_start:
        fields.emplace_back();  // trailing comma or sole empty field
        return true;
    case CsvState::unquoted:
        rstrip_inplace(current);
        fields.push_back(std::move(current));
        return true;
    case CsvState::after_quote:
        fields.push_back(std::move(current));
        return true;
    case CsvState::quoted:
    default:
        return false;  // unterminated quote (also an embedded newline)
    }
}

/**
 * @brief Finds the end of the first balanced top-level `{...}` object.
 *
 * Scans @p text honoring string literals and backslash escapes so braces
 * inside strings do not count. Structural only (no value validation, plan C9).
 *
 * @param text The trimmed NDJSON line; must begin with `{`.
 * @return The index of the matching `}` of the object opened at index 0, or
 * `std::string_view::npos` if the object never closes within @p text.
 */
std::size_t find_object_end(std::string_view text)
{
    int depth = 0;
    bool in_string = false;
    bool escaped = false;

    for (std::size_t i = 0; i < text.size(); ++i)
    {
        const char c = text[i];
        if (in_string)
        {
            if (escaped)
            {
                escaped = false;
            }
            else if (c == '\\')
            {
                escaped = true;
            }
            else if (c == '"')
            {
                in_string = false;
            }
            continue;
        }

        if (c == '"')
        {
            in_string = true;
        }
        else if (c == '{')
        {
            ++depth;
        }
        else if (c == '}')
        {
            --depth;
            if (depth == 0)
            {
                return i;
            }
        }
    }

    return std::string_view::npos;
}

}  // namespace

PairLineParser::PairLineParser(PairFormat format) : m_format(format) {}

ParsedRow PairLineParser::parse(std::string_view line, std::size_t line_number)
{
    ParsedRow row;
    row.line = line_number;

    // Strip a UTF-8 BOM on the first physical line only.
    if (line_number == 1 && line.size() >= k_utf8_bom.size() &&
        line.substr(0, k_utf8_bom.size()) == k_utf8_bom)
    {
        line.remove_prefix(k_utf8_bom.size());
    }

    // Strip a single trailing CR so CRLF streams parse like LF streams.
    if (!line.empty() && line.back() == '\r')
    {
        line.remove_suffix(1);
    }

    if (m_format == PairFormat::ndjson)
    {
        const std::string_view trimmed = trim_view(line);
        if (trimmed.empty())
        {
            row.kind = RowKind::skipped;
            return row;
        }
        if (trimmed.front() != '{')
        {
            row.kind = RowKind::error;
            row.error_code = k_invalid_field;
            return row;
        }
        const std::size_t end = find_object_end(trimmed);
        if (end == std::string_view::npos)
        {
            row.kind = RowKind::error;  // truncated / unterminated object
            row.error_code = k_invalid_field;
            return row;
        }
        for (std::size_t i = end + 1; i < trimmed.size(); ++i)
        {
            if (!is_space(trimmed[i]))
            {
                row.kind = RowKind::error;  // trailing garbage (e.g. }{...})
                row.error_code = k_invalid_field;
                return row;
            }
        }
        row.kind = RowKind::pair;
        row.args_json = std::string(trimmed.substr(0, end + 1));
        return row;
    }

    // CSV.
    if (trim_view(line).empty())
    {
        row.kind = RowKind::skipped;
        return row;
    }

    // The header is recognized only as the first non-blank line.
    if (!m_seen_first_data_line)
    {
        m_seen_first_data_line = true;
        if (is_csv_header(line))
        {
            row.kind = RowKind::skipped;
            return row;
        }
    }

    std::vector<std::string> fields;
    if (!tokenize_csv(line, fields) || fields.empty() || fields.size() > 2)
    {
        row.kind = RowKind::error;
        row.error_code = k_invalid_field;
        return row;
    }

    if (fields[0].empty())
    {
        row.kind = RowKind::error;  // first column required, non-empty
        row.error_code = k_invalid_field;
        return row;
    }

    row.kind = RowKind::pair;
    row.full_url = std::move(fields[0]);
    if (fields.size() == 2 && !fields[1].empty())
    {
        row.slug = std::move(fields[1]);
    }
    return row;
}

void ImportErrorList::add(std::size_t line, std::string code)
{
    if (m_items.size() < kMaxReportedImportErrors)
    {
        m_items.emplace_back(line, std::move(code));
    }
    else
    {
        ++m_omitted;
    }
}

const std::vector<std::pair<std::size_t, std::string>>&
ImportErrorList::items() const noexcept
{
    return m_items;
}

std::size_t ImportErrorList::omitted() const noexcept
{
    return m_omitted;
}

std::string ImportErrorList::to_json() const
{
    std::string out = "[";
    for (std::size_t i = 0; i < m_items.size(); ++i)
    {
        if (i != 0)
        {
            out.push_back(',');
        }
        out += "{\"line\":";
        out += std::to_string(m_items[i].first);
        out += ",\"code\":\"";
        out += ::url_shortener::jsonEscape(m_items[i].second);
        out += "\"}";
    }
    out.push_back(']');
    return out;
}

}  // namespace url_shortener::commands
