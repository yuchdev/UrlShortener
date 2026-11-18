#include <charconv>
#include <cstddef>
#include <string_view>
#include <system_error>

#include "url_shortener/tui/json_view.hpp"

#include <url_shortener/commands/json_scan.hpp>
#include <url_shortener/core/utils.hpp>

namespace url_shortener::tui
{

namespace
{

/**
 * @brief The scan limits every view helper uses.
 *
 * Deliberately looser than the request-envelope defaults: an 8 MiB byte cap
 * lets a large `link.list`/`db.rows` page through while still bounding hostile
 * input (threat model T-121), a raised member cap accepts a wide page, and a
 * 16-level depth cap matches the result-serialization bound. Built by
 * assignment (not a C++20 designated initializer) to stay C++17.
 *
 * @return The shared `commands::scan_limits` for this translation unit.
 */
commands::scan_limits view_limits()
{
    commands::scan_limits limits;
    limits.max_bytes = 8u * 1024u * 1024u;  // 8 MiB
    limits.max_depth = 16;
    limits.max_members = 1u * 1024u * 1024u;  // 1,048,576 rows/elements
    // max_key_len stays at the 128-byte scanner default.
    return limits;
}

/**
 * @brief Trims leading/trailing JSON whitespace from a raw token.
 *
 * @param text The raw token, possibly surrounded by JSON whitespace.
 * @return A view of @p text with surrounding spaces/tabs/newlines removed.
 */
std::string_view trim_ws(std::string_view text)
{
    const auto is_ws = [](char c)
    { return c == ' ' || c == '\t' || c == '\n' || c == '\r'; };
    std::size_t begin = 0;
    std::size_t end = text.size();
    while (begin < end && is_ws(text[begin])) {
        ++begin;
    }
    while (end > begin && is_ws(text[end - 1])) {
        --end;
    }
    return text.substr(begin, end - begin);
}

/**
 * @brief Appends `indent * depth` spaces to @p out.
 *
 * @param out Destination buffer.
 * @param indent Spaces per nesting level.
 * @param depth Current nesting depth.
 */
void append_indent(std::string& out, int indent, int depth)
{
    const std::size_t count =
        static_cast<std::size_t>(indent) * static_cast<std::size_t>(depth);
    out.append(count, ' ');
}

/**
 * @brief Recursively re-indents one JSON value, delegating all tokenizing to
 * the sanctioned scanner.
 *
 * Classifies @p raw only by its first non-whitespace character (`{`/`[` -> a
 * container re-scanned with `scan_object`/`scan_array`, anything else -> a leaf
 * emitted verbatim); it performs no character-level scanning of its own. On any
 * scan failure it appends the offending text verbatim and returns `false` so
 * the top-level caller can fall back to the original input.
 *
 * @param raw The value's raw source text.
 * @param indent Spaces per nesting level.
 * @param depth Current nesting depth (bounds recursion against the scanner
 * depth cap).
 * @param limits Scan limits to apply to nested containers.
 * @param out Destination buffer the pretty form is appended to.
 * @return `true` when @p raw was a well-formed value re-indented successfully;
 * `false` when a scan failed (and @p out received the text verbatim).
 */
bool pretty_value(std::string_view raw,
                  int indent,
                  int depth,
                  const commands::scan_limits& limits,
                  std::string& out)
{
    const std::string_view trimmed = trim_ws(raw);
    if (trimmed.empty()) {
        out.append(raw.data(), raw.size());
        return false;
    }

    const char first = trimmed.front();
    if (first != '{' && first != '[') {
        // Leaf (string/number/boolean/null): emit verbatim so string escapes
        // and number spelling are preserved exactly.
        out.append(trimmed.data(), trimmed.size());
        return true;
    }

    if (depth >= limits.max_depth) {
        // At the depth cap, stop recursing and emit the nested text unchanged.
        out.append(trimmed.data(), trimmed.size());
        return true;
    }

    const std::string text(trimmed);

    if (first == '{') {
        const commands::scan_result scanned =
            commands::scan_object(text, limits);
        if (!scanned.ok) {
            out.append(trimmed.data(), trimmed.size());
            return false;
        }
        if (scanned.members.empty()) {
            out.append("{}");
            return true;
        }
        out.append("{\n");
        for (std::size_t i = 0; i < scanned.members.size(); ++i) {
            append_indent(out, indent, depth + 1);
            out.append(url_shortener::jsonString(scanned.members[i].key));
            out.append(": ");
            if (!pretty_value(
                    scanned.members[i].raw, indent, depth + 1, limits, out))
            {
                return false;
            }
            if (i + 1 < scanned.members.size()) {
                out.append(",");
            }
            out.append("\n");
        }
        append_indent(out, indent, depth);
        out.append("}");
        return true;
    }

    const commands::array_scan_result scanned =
        commands::scan_array(text, limits);
    if (!scanned.ok) {
        out.append(trimmed.data(), trimmed.size());
        return false;
    }
    if (scanned.elements.empty()) {
        out.append("[]");
        return true;
    }
    out.append("[\n");
    for (std::size_t i = 0; i < scanned.elements.size(); ++i) {
        append_indent(out, indent, depth + 1);
        if (!pretty_value(
                scanned.elements[i].raw, indent, depth + 1, limits, out))
        {
            return false;
        }
        if (i + 1 < scanned.elements.size()) {
            out.append(",");
        }
        out.append("\n");
    }
    append_indent(out, indent, depth);
    out.append("]");
    return true;
}

}  // namespace

std::optional<std::string> raw_field(const std::string& json,
                                     const std::string& field)
{
    const commands::scan_result result =
        commands::scan_object(json, view_limits());
    if (!result.ok) {
        return std::nullopt;
    }
    const commands::json_member* member =
        commands::find_member(result.members, field);
    if (member == nullptr) {
        return std::nullopt;
    }
    return member->raw;
}

std::vector<std::string> split_array(const std::string& raw_array)
{
    std::vector<std::string> out;
    const commands::array_scan_result result =
        commands::scan_array(raw_array, view_limits());
    if (!result.ok) {
        return out;
    }
    out.reserve(result.elements.size());
    for (const commands::json_element& element : result.elements) {
        out.push_back(element.raw);
    }
    return out;
}

std::optional<std::string> get_string(const std::string& json,
                                      const std::string& field)
{
    const std::optional<std::string> raw = raw_field(json, field);
    if (!raw) {
        return std::nullopt;
    }
    return commands::decode_json_string(*raw);
}

std::optional<std::int64_t> get_int(const std::string& json,
                                    const std::string& field)
{
    const commands::scan_result result =
        commands::scan_object(json, view_limits());
    if (!result.ok) {
        return std::nullopt;
    }
    const commands::json_member* member =
        commands::find_member(result.members, field);
    if (member == nullptr || member->kind != commands::json_kind::number) {
        return std::nullopt;
    }
    std::int64_t value = 0;
    const char* begin = member->raw.data();
    const char* end = begin + member->raw.size();
    const std::from_chars_result parsed = std::from_chars(begin, end, value);
    if (parsed.ec != std::errc() || parsed.ptr != end) {
        return std::nullopt;
    }
    return value;
}

std::optional<bool> get_bool(const std::string& json, const std::string& field)
{
    const commands::scan_result result =
        commands::scan_object(json, view_limits());
    if (!result.ok) {
        return std::nullopt;
    }
    const commands::json_member* member =
        commands::find_member(result.members, field);
    if (member == nullptr || member->kind != commands::json_kind::boolean) {
        return std::nullopt;
    }
    return member->raw == "true";
}

std::string pretty_print(const std::string& json, int indent)
{
    if (indent < 0) {
        indent = 0;
    }
    if (indent > 32) {
        indent = 32;  // bound display width (defense in depth)
    }
    std::string out;
    if (!pretty_value(json, indent, 0, view_limits(), out)) {
        return json;  // invalid/over-limit input returned verbatim
    }
    return out;
}

std::optional<std::string> reserialize_object(const std::string& text)
{
    const std::vector<JsonField> members = object_fields(text);
    if (members.empty()) {
        // Empty members means either a verified empty object or invalid input;
        // only a verified (whitespace-tolerant) empty object pretty-prints to
        // exactly "{}", so malformed or non-object input is rejected here.
        if (pretty_print(text) == "{}") {
            return std::string("{}");
        }
        return std::nullopt;
    }
    std::string out = "{";
    bool first = true;
    for (const JsonField& member : members) {
        if (!first) {
            out += ",";
        }
        first = false;
        out += url_shortener::jsonString(member.key);
        out += ":";
        const std::string_view value = trim_ws(member.raw_value);
        out.append(value.data(), value.size());
    }
    out += "}";
    return out;
}

std::vector<JsonField> object_fields(const std::string& json)
{
    const commands::scan_result result =
        commands::scan_object(json, view_limits());
    if (!result.ok) {
        return {};
    }
    std::vector<JsonField> fields;
    fields.reserve(result.members.size());
    for (const commands::json_member& member : result.members) {
        fields.push_back(JsonField {member.key, member.raw});
    }
    return fields;
}

}  // namespace url_shortener::tui
