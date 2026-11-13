#include "url_shortener/commands/json_scan.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace url_shortener::commands
{

namespace
{

/**
 * @brief Tests whether @p c is one of the four JSON whitespace characters.
 * @param c Candidate byte.
 * @return `true` for space, tab, line feed or carriage return.
 */
bool is_ws(char c) noexcept
{
    return c == ' ' || c == '\t' || c == '\n' || c == '\r';
}

/**
 * @brief Tests whether @p c is an ASCII decimal digit.
 * @param c Candidate byte.
 * @return `true` for `'0'`..`'9'`.
 */
bool is_digit(char c) noexcept
{
    return c >= '0' && c <= '9';
}

/**
 * @brief Advances @p i past any JSON whitespace in @p text.
 * @param text Input being scanned.
 * @param i Current offset; advanced in place to the next non-whitespace byte
 * or to `text.size()`.
 */
void skip_ws(std::string_view text, std::size_t& i) noexcept
{
    while (i < text.size() && is_ws(text[i]))
    {
        ++i;
    }
}

/**
 * @brief Finds the end of a JSON string token starting at @p start.
 *
 * Escape-aware: a backslash escapes the following byte so an escaped quote
 * does not terminate the token. Performs no escape validation (that is
 * `decode_json_string`'s job); it only balances the quotes.
 *
 * @param text Input being scanned.
 * @param start Offset of the opening quote; `text[start]` must be `"`.
 * @param end Receives the offset one past the closing quote on success.
 * @return `true` if a closing quote was found; `false` if the token is
 * unterminated (including a trailing lone backslash).
 */
bool scan_string_token(std::string_view text, std::size_t start,
                       std::size_t& end) noexcept
{
    std::size_t j = start + 1;
    while (j < text.size())
    {
        const char c = text[j];
        if (c == '\\')
        {
            if (j + 1 >= text.size())
            {
                return false;
            }
            j += 2;
        }
        else if (c == '"')
        {
            end = j + 1;
            return true;
        }
        else
        {
            ++j;
        }
    }
    return false;
}

/**
 * @brief Validates a JSON number starting at @p start and finds its end.
 *
 * Enforces the JSON number grammar: optional leading `-`, an integer part of
 * either `0` or a non-zero digit followed by digits, an optional fraction,
 * and an optional exponent. Rejects leading `+`, leading zeros, a bare
 * fraction and a dangling decimal point.
 *
 * @param text Input being scanned.
 * @param start Offset of the first number byte.
 * @param end Receives the offset one past the last number byte on success.
 * @return `true` if a grammatically valid number was consumed; `false`
 * otherwise.
 */
bool scan_number_token(std::string_view text, std::size_t start,
                       std::size_t& end) noexcept
{
    const std::size_t n = text.size();
    std::size_t j = start;

    if (j < n && text[j] == '-')
    {
        ++j;
    }

    // Integer part: a single '0', or a 1-9 digit followed by more digits.
    if (j >= n)
    {
        return false;
    }
    if (text[j] == '0')
    {
        ++j;
    }
    else if (text[j] >= '1' && text[j] <= '9')
    {
        ++j;
        while (j < n && is_digit(text[j]))
        {
            ++j;
        }
    }
    else
    {
        return false;
    }

    // Optional fraction.
    if (j < n && text[j] == '.')
    {
        ++j;
        if (j >= n || !is_digit(text[j]))
        {
            return false;
        }
        while (j < n && is_digit(text[j]))
        {
            ++j;
        }
    }

    // Optional exponent.
    if (j < n && (text[j] == 'e' || text[j] == 'E'))
    {
        ++j;
        if (j < n && (text[j] == '+' || text[j] == '-'))
        {
            ++j;
        }
        if (j >= n || !is_digit(text[j]))
        {
            return false;
        }
        while (j < n && is_digit(text[j]))
        {
            ++j;
        }
    }

    end = j;
    return true;
}

/**
 * @brief Finds the end of a nested object/array using an explicit stack.
 *
 * Iterative (no recursion), string- and escape-aware, bounded by @p max_depth.
 * It balances braces and brackets while treating string contents as opaque so
 * a `}` or `]` inside a string does not close the container.
 *
 * @param text Input being scanned.
 * @param start Offset of the opening `{` or `[`.
 * @param max_depth Maximum allowed nesting of containers inside this value.
 * @param end Receives the offset one past the matching closing byte on
 * success.
 * @param too_deep Set to `true` when nesting exceeds @p max_depth (otherwise a
 * `false` return means a structural error).
 * @return `true` if the container was balanced within depth; `false` on a
 * depth overflow (with @p too_deep set) or a structural error (mismatched or
 * unterminated container, unterminated string).
 */
bool scan_container_token(std::string_view text, std::size_t start,
                          int max_depth, std::size_t& end,
                          bool& too_deep) noexcept
{
    too_deep = false;
    std::vector<char> expected_close;
    std::size_t j = start;
    const std::size_t n = text.size();

    while (j < n)
    {
        const char c = text[j];
        if (c == '"')
        {
            std::size_t str_end = 0;
            if (!scan_string_token(text, j, str_end))
            {
                return false;
            }
            j = str_end;
        }
        else if (c == '{' || c == '[')
        {
            expected_close.push_back(c == '{' ? '}' : ']');
            if (static_cast<int>(expected_close.size()) > max_depth)
            {
                too_deep = true;
                return false;
            }
            ++j;
        }
        else if (c == '}' || c == ']')
        {
            if (expected_close.empty() || expected_close.back() != c)
            {
                return false;
            }
            expected_close.pop_back();
            ++j;
            if (expected_close.empty())
            {
                end = j;
                return true;
            }
        }
        else
        {
            ++j;
        }
    }
    return false;
}

/**
 * @brief Outcome of classifying and measuring a single JSON value.
 */
struct value_scan
{
    ///< `true` if a syntactically valid value was recognized.
    bool ok = false;

    ///< Kind of the recognized value; meaningful only when `ok`.
    json_kind kind = json_kind::null;

    ///< Offset one past the end of the value; meaningful only when `ok`.
    std::size_t end = 0;

    ///< Set when the failure was specifically a nesting-depth overflow, so
    ///< the caller can map it to `"too_deep"` rather than `"malformed"`.
    bool too_deep = false;
};

/**
 * @brief Classifies the value at @p start by its first character and measures
 * its extent.
 *
 * @param text Input being scanned.
 * @param start Offset of the first value byte (already past whitespace).
 * @param max_depth Maximum container nesting allowed inside the value.
 * @return A `value_scan`; on failure `ok` is `false` and `too_deep` indicates
 * whether the cause was a depth overflow.
 */
value_scan scan_value(std::string_view text, std::size_t start,
                      int max_depth) noexcept
{
    value_scan out;
    if (start >= text.size())
    {
        return out;
    }

    const char c = text[start];
    if (c == '"')
    {
        std::size_t end = 0;
        if (scan_string_token(text, start, end))
        {
            out.ok = true;
            out.kind = json_kind::string;
            out.end = end;
        }
        return out;
    }
    if (c == '{' || c == '[')
    {
        std::size_t end = 0;
        bool too_deep = false;
        if (scan_container_token(text, start, max_depth, end, too_deep))
        {
            out.ok = true;
            out.kind = (c == '{') ? json_kind::object : json_kind::array;
            out.end = end;
        }
        else
        {
            out.too_deep = too_deep;
        }
        return out;
    }
    if (c == 't' || c == 'f')
    {
        const std::string_view lit = (c == 't') ? "true" : "false";
        if (text.substr(start, lit.size()) == lit)
        {
            out.ok = true;
            out.kind = json_kind::boolean;
            out.end = start + lit.size();
        }
        return out;
    }
    if (c == 'n')
    {
        const std::string_view lit = "null";
        if (text.substr(start, lit.size()) == lit)
        {
            out.ok = true;
            out.kind = json_kind::null;
            out.end = start + lit.size();
        }
        return out;
    }

    std::size_t end = 0;
    if (scan_number_token(text, start, end))
    {
        out.ok = true;
        out.kind = json_kind::number;
        out.end = end;
    }
    return out;
}

/**
 * @brief Parses four hexadecimal digits at @p pos into a code unit.
 * @param text Source text.
 * @param pos Offset of the first hex digit.
 * @param out Receives the parsed 16-bit value on success.
 * @return `true` if four valid hex digits were present; `false` otherwise.
 */
bool parse_hex4(std::string_view text, std::size_t pos,
                std::uint32_t& out) noexcept
{
    if (pos + 4 > text.size())
    {
        return false;
    }
    std::uint32_t value = 0;
    for (std::size_t k = 0; k < 4; ++k)
    {
        const char c = text[pos + k];
        value <<= 4;
        if (c >= '0' && c <= '9')
        {
            value |= static_cast<std::uint32_t>(c - '0');
        }
        else if (c >= 'a' && c <= 'f')
        {
            value |= static_cast<std::uint32_t>(c - 'a' + 10);
        }
        else if (c >= 'A' && c <= 'F')
        {
            value |= static_cast<std::uint32_t>(c - 'A' + 10);
        }
        else
        {
            return false;
        }
    }
    out = value;
    return true;
}

/**
 * @brief Appends @p cp to @p out as a UTF-8 byte sequence.
 * @param out Destination buffer.
 * @param cp Unicode code point in the range U+0000..U+10FFFF.
 */
void append_utf8(std::string& out, std::uint32_t cp)
{
    if (cp <= 0x7F)
    {
        out.push_back(static_cast<char>(cp));
    }
    else if (cp <= 0x7FF)
    {
        out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    }
    else if (cp <= 0xFFFF)
    {
        out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    }
    else
    {
        out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    }
}

} // namespace

std::optional<std::string> decode_json_string(std::string_view raw_token)
{
    if (raw_token.size() < 2 || raw_token.front() != '"' ||
        raw_token.back() != '"')
    {
        return std::nullopt;
    }

    std::string out;
    out.reserve(raw_token.size());
    const std::size_t last = raw_token.size() - 1; // index of closing quote

    std::size_t i = 1;
    while (i < last)
    {
        const char c = raw_token[i];
        if (c != '\\')
        {
            out.push_back(c);
            ++i;
            continue;
        }

        // Escape sequence: need at least one more character before the quote.
        if (i + 1 >= last)
        {
            return std::nullopt;
        }
        const char esc = raw_token[i + 1];
        switch (esc)
        {
        case '"':
            out.push_back('"');
            i += 2;
            break;
        case '\\':
            out.push_back('\\');
            i += 2;
            break;
        case '/':
            out.push_back('/');
            i += 2;
            break;
        case 'b':
            out.push_back('\b');
            i += 2;
            break;
        case 'f':
            out.push_back('\f');
            i += 2;
            break;
        case 'n':
            out.push_back('\n');
            i += 2;
            break;
        case 'r':
            out.push_back('\r');
            i += 2;
            break;
        case 't':
            out.push_back('\t');
            i += 2;
            break;
        case 'u':
        {
            std::uint32_t unit = 0;
            if (!parse_hex4(raw_token, i + 2, unit) || i + 6 > last)
            {
                return std::nullopt;
            }
            i += 6;
            if (unit >= 0xD800 && unit <= 0xDBFF)
            {
                // High surrogate: require a following low-surrogate escape.
                if (i + 6 > last || raw_token[i] != '\\' ||
                    raw_token[i + 1] != 'u')
                {
                    return std::nullopt;
                }
                std::uint32_t low = 0;
                if (!parse_hex4(raw_token, i + 2, low))
                {
                    return std::nullopt;
                }
                if (low < 0xDC00 || low > 0xDFFF)
                {
                    return std::nullopt;
                }
                i += 6;
                const std::uint32_t cp =
                    0x10000 + ((unit - 0xD800) << 10) + (low - 0xDC00);
                append_utf8(out, cp);
            }
            else if (unit >= 0xDC00 && unit <= 0xDFFF)
            {
                // Lone low surrogate.
                return std::nullopt;
            }
            else
            {
                append_utf8(out, unit);
            }
            break;
        }
        default:
            return std::nullopt;
        }
    }
    return out;
}

const json_member* find_member(const std::vector<json_member>& members,
                               std::string_view key)
{
    for (const json_member& member : members)
    {
        if (member.key == key)
        {
            return &member;
        }
    }
    return nullptr;
}

scan_result scan_object(std::string_view text, const scan_limits& limits)
{
    scan_result result;

    if (text.size() > limits.max_bytes)
    {
        result.error = "too_large";
        return result;
    }

    std::size_t i = 0;
    skip_ws(text, i);
    if (i >= text.size() || text[i] != '{')
    {
        result.error = "not_an_object";
        return result;
    }
    ++i; // consume '{'

    skip_ws(text, i);
    if (i < text.size() && text[i] == '}')
    {
        ++i;
        skip_ws(text, i);
        if (i != text.size())
        {
            result.error = "trailing_data";
            return result;
        }
        result.ok = true;
        return result;
    }

    while (true)
    {
        if (result.members.size() >= limits.max_members)
        {
            result.members.clear();
            result.error = "too_many_members";
            return result;
        }

        // Key: must be a string token.
        skip_ws(text, i);
        if (i >= text.size() || text[i] != '"')
        {
            result.members.clear();
            result.error = "malformed";
            return result;
        }
        std::size_t key_end = 0;
        if (!scan_string_token(text, i, key_end))
        {
            result.members.clear();
            result.error = "malformed";
            return result;
        }
        std::optional<std::string> decoded =
            decode_json_string(text.substr(i, key_end - i));
        if (!decoded || decoded->size() > limits.max_key_len ||
            decoded->find('\0') != std::string::npos)
        {
            result.members.clear();
            result.error = "malformed";
            return result;
        }
        i = key_end;

        // Separator.
        skip_ws(text, i);
        if (i >= text.size() || text[i] != ':')
        {
            result.members.clear();
            result.error = "malformed";
            return result;
        }
        ++i;

        // Value.
        skip_ws(text, i);
        const value_scan vs = scan_value(text, i, limits.max_depth);
        if (!vs.ok)
        {
            result.members.clear();
            result.error = vs.too_deep ? "too_deep" : "malformed";
            return result;
        }

        if (find_member(result.members, *decoded) != nullptr)
        {
            result.members.clear();
            result.error = "duplicate_key";
            return result;
        }

        json_member member;
        member.key = std::move(*decoded);
        member.kind = vs.kind;
        member.raw.assign(text.substr(i, vs.end - i));
        result.members.push_back(std::move(member));
        i = vs.end;

        // Next: ',' continues, '}' ends.
        skip_ws(text, i);
        if (i >= text.size())
        {
            result.members.clear();
            result.error = "malformed";
            return result;
        }
        if (text[i] == ',')
        {
            ++i;
            continue;
        }
        if (text[i] == '}')
        {
            ++i;
            break;
        }
        result.members.clear();
        result.error = "malformed";
        return result;
    }

    skip_ws(text, i);
    if (i != text.size())
    {
        result.members.clear();
        result.error = "trailing_data";
        return result;
    }

    result.ok = true;
    return result;
}

array_scan_result scan_array(std::string_view text, const scan_limits& limits)
{
    array_scan_result result;

    if (text.size() > limits.max_bytes)
    {
        result.error = "too_large";
        return result;
    }

    std::size_t i = 0;
    skip_ws(text, i);
    if (i >= text.size() || text[i] != '[')
    {
        result.error = "not_an_array";
        return result;
    }
    ++i; // consume '['

    skip_ws(text, i);
    if (i < text.size() && text[i] == ']')
    {
        ++i;
        skip_ws(text, i);
        if (i != text.size())
        {
            result.error = "trailing_data";
            return result;
        }
        result.ok = true;
        return result;
    }

    while (true)
    {
        if (result.elements.size() >= limits.max_members)
        {
            result.elements.clear();
            result.error = "too_many_members";
            return result;
        }

        skip_ws(text, i);
        const value_scan vs = scan_value(text, i, limits.max_depth);
        if (!vs.ok)
        {
            result.elements.clear();
            result.error = vs.too_deep ? "too_deep" : "malformed";
            return result;
        }

        json_element element;
        element.kind = vs.kind;
        element.raw.assign(text.substr(i, vs.end - i));
        result.elements.push_back(std::move(element));
        i = vs.end;

        skip_ws(text, i);
        if (i >= text.size())
        {
            result.elements.clear();
            result.error = "malformed";
            return result;
        }
        if (text[i] == ',')
        {
            ++i;
            continue;
        }
        if (text[i] == ']')
        {
            ++i;
            break;
        }
        result.elements.clear();
        result.error = "malformed";
        return result;
    }

    skip_ws(text, i);
    if (i != text.size())
    {
        result.elements.clear();
        result.error = "trailing_data";
        return result;
    }

    result.ok = true;
    return result;
}

} // namespace url_shortener::commands
