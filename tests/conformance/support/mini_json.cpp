/**
 * @file mini_json.cpp
 * @brief Implementation of the test-only JSON DOM (see mini_json.hpp).
 */

#include <array>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>

#include "mini_json.hpp"

namespace conformance
{

namespace
{

/**
 * @brief Recursive-descent parser over a UTF-8 byte buffer.
 *
 * Depth-bounded and allocation-light; every failure sets @ref m_error (when a
 * sink was provided) and unwinds through `false`, so no input can throw or
 * overflow the stack.
 */
class Parser
{
public:
    /**
     * @param text The full input buffer.
     * @param error Optional reason sink (see Json::parse).
     * @param max_depth Maximum array/object nesting depth.
     */
    Parser(std::string_view text, std::string* error, int max_depth)
        : m_text(text)
        , m_error(error)
        , m_max_depth(max_depth)
    {
    }

    /**
     * @brief Parses one top-level value plus trailing whitespace only.
     * @param out Receives the parsed value on success.
     * @return `true` on success; `false` with @ref m_error set otherwise.
     */
    bool parse_document(Json& out)
    {
        skip_ws();
        if (!parse_value(out, 0)) {
            return false;
        }
        skip_ws();
        if (m_pos != m_text.size()) {
            return fail("trailing data");
        }
        return true;
    }

private:
    /// Records @p reason (if a sink exists) and returns `false`.
    bool fail(const char* reason)
    {
        if (m_error != nullptr) {
            *m_error = reason;
        }
        return false;
    }

    /// Advances past JSON insignificant whitespace.
    void skip_ws()
    {
        while (m_pos < m_text.size()) {
            const char c = m_text[m_pos];
            if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
                ++m_pos;
            }
            else {
                break;
            }
        }
    }

    /// @return The current byte, or `'\0'` at end of input.
    char peek() const { return m_pos < m_text.size() ? m_text[m_pos] : '\0'; }

    /**
     * @brief Parses any JSON value at the current position.
     * @param out Receives the value. @param depth Current nesting depth.
     * @return `true` on success.
     */
    bool parse_value(Json& out, int depth)
    {
        skip_ws();
        if (m_pos >= m_text.size()) {
            return fail("unexpected end of input");
        }
        const char c = m_text[m_pos];
        switch (c) {
            case '{':
                return parse_object(out, depth);
            case '[':
                return parse_array(out, depth);
            case '"': {
                std::string value;
                if (!parse_string(value)) {
                    return false;
                }
                out = Json::string(std::move(value));
                return true;
            }
            case 't':
            case 'f':
                return parse_bool(out);
            case 'n':
                return parse_null(out);
            default:
                if (c == '-' || (c >= '0' && c <= '9')) {
                    return parse_number(out);
                }
                return fail("unexpected character");
        }
    }

    /// Parses a `{...}` object, rejecting duplicate keys and trailing commas.
    bool parse_object(Json& out, int depth)
    {
        if (depth + 1 > m_max_depth) {
            return fail("max depth exceeded");
        }
        ++m_pos;  // consume '{'
        out = Json::object();
        skip_ws();
        if (peek() == '}') {
            ++m_pos;
            return true;
        }
        for (;;) {
            skip_ws();
            if (peek() != '"') {
                return fail("expected object key");
            }
            std::string key;
            if (!parse_string(key)) {
                return false;
            }
            for (const auto& member : out.members()) {
                if (member.first == key) {
                    return fail("duplicate key");
                }
            }
            skip_ws();
            if (peek() != ':') {
                return fail("expected ':'");
            }
            ++m_pos;
            Json value;
            if (!parse_value(value, depth + 1)) {
                return false;
            }
            out.set(std::move(key), std::move(value));
            skip_ws();
            const char sep = peek();
            if (sep == ',') {
                ++m_pos;
                continue;
            }
            if (sep == '}') {
                ++m_pos;
                return true;
            }
            return fail("expected ',' or '}'");
        }
    }

    /// Parses a `[...]` array, rejecting trailing commas.
    bool parse_array(Json& out, int depth)
    {
        if (depth + 1 > m_max_depth) {
            return fail("max depth exceeded");
        }
        ++m_pos;  // consume '['
        out = Json::array();
        skip_ws();
        if (peek() == ']') {
            ++m_pos;
            return true;
        }
        for (;;) {
            Json value;
            if (!parse_value(value, depth + 1)) {
                return false;
            }
            out.push_back(std::move(value));
            skip_ws();
            const char sep = peek();
            if (sep == ',') {
                ++m_pos;
                continue;
            }
            if (sep == ']') {
                ++m_pos;
                return true;
            }
            return fail("expected ',' or ']'");
        }
    }

    /// Parses the `true`/`false` literal at the current position.
    bool parse_bool(Json& out)
    {
        if (m_text.compare(m_pos, 4, "true") == 0) {
            m_pos += 4;
            out = Json::boolean(true);
            return true;
        }
        if (m_text.compare(m_pos, 5, "false") == 0) {
            m_pos += 5;
            out = Json::boolean(false);
            return true;
        }
        return fail("malformed literal");
    }

    /// Parses the `null` literal at the current position.
    bool parse_null(Json& out)
    {
        if (m_text.compare(m_pos, 4, "null") == 0) {
            m_pos += 4;
            out = Json::null();
            return true;
        }
        return fail("malformed literal");
    }

    /**
     * @brief Parses a JSON number, classifying it as `Int` or `Double`.
     *
     * Validates the JSON number grammar (optional leading `-`, no leading
     * zeros, optional fraction and exponent) before converting.
     */
    bool parse_number(Json& out)
    {
        const std::size_t start = m_pos;
        bool is_double = false;
        if (peek() == '-') {
            ++m_pos;
        }
        if (peek() == '0') {
            ++m_pos;
        }
        else if (peek() >= '1' && peek() <= '9') {
            while (peek() >= '0' && peek() <= '9') {
                ++m_pos;
            }
        }
        else {
            return fail("malformed number");
        }
        if (peek() == '.') {
            is_double = true;
            ++m_pos;
            if (!(peek() >= '0' && peek() <= '9')) {
                return fail("malformed number");
            }
            while (peek() >= '0' && peek() <= '9') {
                ++m_pos;
            }
        }
        if (peek() == 'e' || peek() == 'E') {
            is_double = true;
            ++m_pos;
            if (peek() == '+' || peek() == '-') {
                ++m_pos;
            }
            if (!(peek() >= '0' && peek() <= '9')) {
                return fail("malformed number");
            }
            while (peek() >= '0' && peek() <= '9') {
                ++m_pos;
            }
        }
        const std::string token(m_text.substr(start, m_pos - start));
        if (is_double) {
            out = Json::real(std::strtod(token.c_str(), nullptr));
        }
        else {
            errno = 0;
            char* end = nullptr;
            const long long value = std::strtoll(token.c_str(), &end, 10);
            if (errno != 0) {
                // Out of long long range: fall back to a double so the value is
                // retained rather than crashing; still never throws.
                out = Json::real(std::strtod(token.c_str(), nullptr));
            }
            else {
                out = Json::integer(value);
            }
        }
        return true;
    }

    /**
     * @brief Parses a `"..."` string token and decodes it to UTF-8.
     *
     * Handles the JSON escapes and `\uXXXX`, including a high/low surrogate
     * pair that combines into one code point. Rejects unescaped control
     * characters, invalid escapes, and unterminated strings.
     *
     * @param out Receives the decoded UTF-8 bytes.
     * @return `true` on success.
     */
    bool parse_string(std::string& out)
    {
        if (peek() != '"') {
            return fail("expected string");
        }
        ++m_pos;
        out.clear();
        while (m_pos < m_text.size()) {
            const unsigned char c = static_cast<unsigned char>(m_text[m_pos]);
            if (c == '"') {
                ++m_pos;
                return true;
            }
            if (c == '\\') {
                ++m_pos;
                if (!parse_escape(out)) {
                    return false;
                }
                continue;
            }
            if (c < 0x20) {
                return fail("control character in string");
            }
            if (c < 0x80) {
                out.push_back(static_cast<char>(c));
                ++m_pos;
                continue;
            }
            // Multi-byte lead: validate the whole UTF-8 scalar so malformed
            // bytes are rejected (parity with Python's json, which decodes
            // bytes to str up front).
            if (!copy_utf8_sequence(out)) {
                return false;
            }
        }
        return fail("unterminated string");
    }

    /// Validates and copies one UTF-8 scalar starting at @p m_pos into @p out.
    /// Rejects truncated/overlong sequences, invalid continuation bytes,
    /// surrogate code points and values above U+10FFFF.
    bool copy_utf8_sequence(std::string& out)
    {
        const unsigned char lead = static_cast<unsigned char>(m_text[m_pos]);
        std::size_t length = 0;
        unsigned int min_cp = 0;
        if ((lead & 0xE0) == 0xC0) {
            length = 2;
            min_cp = 0x80;
        }
        else if ((lead & 0xF0) == 0xE0) {
            length = 3;
            min_cp = 0x800;
        }
        else if ((lead & 0xF8) == 0xF0) {
            length = 4;
            min_cp = 0x10000;
        }
        else {
            return fail("invalid UTF-8");
        }
        if (m_pos + length > m_text.size()) {
            return fail("invalid UTF-8");
        }
        unsigned int code = lead & (0x7Fu >> length);
        for (std::size_t i = 1; i < length; ++i) {
            const unsigned char cont =
                static_cast<unsigned char>(m_text[m_pos + i]);
            if ((cont & 0xC0) != 0x80) {
                return fail("invalid UTF-8");
            }
            code = (code << 6) | (cont & 0x3Fu);
        }
        if (code < min_cp || code > 0x10FFFF
            || (code >= 0xD800 && code <= 0xDFFF))
        {
            return fail("invalid UTF-8");
        }
        out.append(m_text.substr(m_pos, length));
        m_pos += length;
        return true;
    }

    /// Parses one escape sequence (the `\` already consumed) into @p out.
    bool parse_escape(std::string& out)
    {
        if (m_pos >= m_text.size()) {
            return fail("unterminated escape");
        }
        const char e = m_text[m_pos];
        switch (e) {
            case '"':
                out.push_back('"');
                ++m_pos;
                return true;
            case '\\':
                out.push_back('\\');
                ++m_pos;
                return true;
            case '/':
                out.push_back('/');
                ++m_pos;
                return true;
            case 'b':
                out.push_back('\b');
                ++m_pos;
                return true;
            case 'f':
                out.push_back('\f');
                ++m_pos;
                return true;
            case 'n':
                out.push_back('\n');
                ++m_pos;
                return true;
            case 'r':
                out.push_back('\r');
                ++m_pos;
                return true;
            case 't':
                out.push_back('\t');
                ++m_pos;
                return true;
            case 'u':
                return parse_unicode_escape(out);
            default:
                return fail("invalid escape");
        }
    }

    /// Parses `\uXXXX` (the `u` at `m_pos`), joining surrogate pairs.
    bool parse_unicode_escape(std::string& out)
    {
        unsigned int code = 0;
        if (!read_hex4(code)) {
            return false;
        }
        if (code >= 0xD800 && code <= 0xDBFF) {
            // High surrogate: a low surrogate must immediately follow.
            if (m_pos + 1 >= m_text.size() || m_text[m_pos] != '\\'
                || m_text[m_pos + 1] != 'u')
            {
                return fail("unpaired surrogate");
            }
            m_pos += 2;
            unsigned int low = 0;
            if (!read_hex4(low)) {
                return false;
            }
            if (low < 0xDC00 || low > 0xDFFF) {
                return fail("unpaired surrogate");
            }
            code = 0x10000 + ((code - 0xD800) << 10) + (low - 0xDC00);
        }
        else if (code >= 0xDC00 && code <= 0xDFFF) {
            return fail("unpaired surrogate");
        }
        append_utf8(out, code);
        return true;
    }

    /// Reads four hex digits (after the `u`), advancing past them.
    bool read_hex4(unsigned int& value)
    {
        // At entry m_pos points at 'u'; advance past it.
        ++m_pos;
        if (m_pos + 4 > m_text.size()) {
            return fail("truncated \\u escape");
        }
        value = 0;
        for (int i = 0; i < 4; ++i) {
            const char h = m_text[m_pos + static_cast<std::size_t>(i)];
            value <<= 4;
            if (h >= '0' && h <= '9') {
                value |= static_cast<unsigned int>(h - '0');
            }
            else if (h >= 'a' && h <= 'f') {
                value |= static_cast<unsigned int>(h - 'a' + 10);
            }
            else if (h >= 'A' && h <= 'F') {
                value |= static_cast<unsigned int>(h - 'A' + 10);
            }
            else {
                return fail("bad hex digit");
            }
        }
        m_pos += 4;
        return true;
    }

    /// Appends the UTF-8 encoding of code point @p code to @p out.
    static void append_utf8(std::string& out, unsigned int code)
    {
        if (code <= 0x7F) {
            out.push_back(static_cast<char>(code));
        }
        else if (code <= 0x7FF) {
            out.push_back(static_cast<char>(0xC0 | (code >> 6)));
            out.push_back(static_cast<char>(0x80 | (code & 0x3F)));
        }
        else if (code <= 0xFFFF) {
            out.push_back(static_cast<char>(0xE0 | (code >> 12)));
            out.push_back(static_cast<char>(0x80 | ((code >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (code & 0x3F)));
        }
        else {
            out.push_back(static_cast<char>(0xF0 | (code >> 18)));
            out.push_back(static_cast<char>(0x80 | ((code >> 12) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | ((code >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (code & 0x3F)));
        }
    }

    std::string_view m_text;  ///< Full input buffer.
    std::size_t m_pos = 0;  ///< Current read offset.
    std::string* m_error;  ///< Optional failure-reason sink.
    int m_max_depth;  ///< Maximum array/object nesting depth.
};

/// Appends @p value as a JSON string literal (with escapes) to @p out.
void dump_string(const std::string& value, std::string& out)
{
    out.push_back('"');
    for (const char ch : value) {
        const unsigned char c = static_cast<unsigned char>(ch);
        switch (c) {
            case '"':
                out += "\\\"";
                break;
            case '\\':
                out += "\\\\";
                break;
            case '\b':
                out += "\\b";
                break;
            case '\f':
                out += "\\f";
                break;
            case '\n':
                out += "\\n";
                break;
            case '\r':
                out += "\\r";
                break;
            case '\t':
                out += "\\t";
                break;
            default:
                if (c < 0x20) {
                    std::array<char, 8> buffer {};
                    std::snprintf(buffer.data(), buffer.size(), "\\u%04x", c);
                    out += buffer.data();
                }
                else {
                    out.push_back(ch);
                }
                break;
        }
    }
    out.push_back('"');
}

/// Appends @p value as a round-trippable JSON number literal to @p out.
void dump_double(double value, std::string& out)
{
    std::array<char, 32> buffer {};
    std::snprintf(buffer.data(), buffer.size(), "%.17g", value);
    std::string text(buffer.data());
    // Preserve the Double kind on re-parse: a bare integer-looking form would
    // otherwise come back as Int.
    if (text.find_first_of(".eEnN") == std::string::npos) {
        text += ".0";
    }
    out += text;
}

}  // namespace

Json Json::null()
{
    return Json();
}

Json Json::boolean(bool value)
{
    Json j;
    j.m_kind = Kind::Bool;
    j.m_bool = value;
    return j;
}

Json Json::integer(long long value)
{
    Json j;
    j.m_kind = Kind::Int;
    j.m_int = value;
    return j;
}

Json Json::real(double value)
{
    Json j;
    j.m_kind = Kind::Double;
    j.m_double = value;
    return j;
}

Json Json::string(std::string value)
{
    Json j;
    j.m_kind = Kind::String;
    j.m_string = std::move(value);
    return j;
}

Json Json::array()
{
    Json j;
    j.m_kind = Kind::Array;
    return j;
}

Json Json::object()
{
    Json j;
    j.m_kind = Kind::Object;
    return j;
}

std::optional<Json> Json::parse(std::string_view text,
                                std::string* error,
                                int max_depth)
{
    Parser parser(text, error, max_depth);
    Json out;
    if (!parser.parse_document(out)) {
        return std::nullopt;
    }
    return out;
}

std::string Json::dump() const
{
    std::string out;
    switch (m_kind) {
        case Kind::Null:
            out += "null";
            break;
        case Kind::Bool:
            out += m_bool ? "true" : "false";
            break;
        case Kind::Int:
            out += std::to_string(m_int);
            break;
        case Kind::Double:
            dump_double(m_double, out);
            break;
        case Kind::String:
            dump_string(m_string, out);
            break;
        case Kind::Array: {
            out.push_back('[');
            bool first = true;
            for (const Json& element : m_array) {
                if (!first) {
                    out.push_back(',');
                }
                first = false;
                out += element.dump();
            }
            out.push_back(']');
            break;
        }
        case Kind::Object: {
            out.push_back('{');
            bool first = true;
            for (const Member& member : m_object) {
                if (!first) {
                    out.push_back(',');
                }
                first = false;
                dump_string(member.first, out);
                out.push_back(':');
                out += member.second.dump();
            }
            out.push_back('}');
            break;
        }
    }
    return out;
}

const Json* Json::find(std::string_view key) const
{
    if (m_kind != Kind::Object) {
        return nullptr;
    }
    for (const Member& member : m_object) {
        if (member.first == key) {
            return &member.second;
        }
    }
    return nullptr;
}

const Json* Json::at(std::size_t index) const
{
    if (m_kind != Kind::Array || index >= m_array.size()) {
        return nullptr;
    }
    return &m_array[index];
}

const Json* Json::path(std::string_view jsonpath) const
{
    if (jsonpath.empty() || jsonpath.front() != '$') {
        return nullptr;
    }
    const Json* current = this;
    std::size_t pos = 1;
    while (pos < jsonpath.size() && current != nullptr) {
        const char c = jsonpath[pos];
        if (c == '.') {
            ++pos;
            const std::size_t start = pos;
            while (pos < jsonpath.size() && jsonpath[pos] != '.'
                   && jsonpath[pos] != '[')
            {
                ++pos;
            }
            if (pos == start) {
                return nullptr;
            }
            current = current->find(jsonpath.substr(start, pos - start));
        }
        else if (c == '[') {
            ++pos;
            const std::size_t start = pos;
            while (pos < jsonpath.size() && jsonpath[pos] != ']') {
                ++pos;
            }
            if (pos >= jsonpath.size() || pos == start) {
                return nullptr;
            }
            std::size_t index = 0;
            for (std::size_t i = start; i < pos; ++i) {
                const char d = jsonpath[i];
                if (d < '0' || d > '9') {
                    return nullptr;
                }
                index = index * 10 + static_cast<std::size_t>(d - '0');
            }
            ++pos;  // consume ']'
            current = current->at(index);
        }
        else {
            return nullptr;
        }
    }
    return current;
}

const std::string& Json::as_string() const
{
    assert(m_kind == Kind::String);
    return m_string;
}

long long Json::as_int() const
{
    assert(m_kind == Kind::Int);
    return m_int;
}

double Json::as_double() const
{
    assert(m_kind == Kind::Double);
    return m_double;
}

bool Json::as_bool() const
{
    assert(m_kind == Kind::Bool);
    return m_bool;
}

const std::vector<Json>& Json::items() const
{
    assert(m_kind == Kind::Array);
    return m_array;
}

const std::vector<Json::Member>& Json::members() const
{
    assert(m_kind == Kind::Object);
    return m_object;
}

void Json::push_back(Json value)
{
    assert(m_kind == Kind::Array);
    m_array.push_back(std::move(value));
}

void Json::set(std::string key, Json value)
{
    assert(m_kind == Kind::Object);
    m_object.emplace_back(std::move(key), std::move(value));
}

}  // namespace conformance
