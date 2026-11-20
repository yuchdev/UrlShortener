/**
 * @file matcher.cpp
 * @brief Implementation of the shared conformance matcher (see matcher.hpp).
 *
 * Every rule here is a direct port of `conformance_match.py`; the two must
 * agree byte-for-byte on `normalizer_vectors.json`. When editing, change the
 * vectors and the Python side first, then mirror here.
 *
 * @note Tech debt (07.0/03 review): this file, `mini_json.cpp` and `corpus.cpp`
 * exceed the soft 400-line guideline from the subtask spec. They are kept as
 * cohesive single modules (the matcher mirrors the single-file Python
 * reference; `mini_json` is one DOM) rather than fragmenting the ported logic.
 * A split is deferred, not forgotten.
 */

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <regex>
#include <stack>
#include <string>

#include "matcher.hpp"

namespace conformance
{

namespace
{

/// Maximum subject length fed to a `<regex:...>` match (CWE-674 / ReDoS guard).
/// A longer subject is a mismatch and is never handed to `std::regex_match`.
/// Mirrors `conformance_match._REGEX_SUBJECT_MAX`.
constexpr std::size_t kRegexSubjectMax = 1024;

/// Maximum subject length fed to the `<ts>` timestamp match. An RFC 3339 UTC
/// timestamp is far shorter; a longer subject is a mismatch without running the
/// regex. Mirrors `conformance_match._TS_SUBJECT_MAX`.
constexpr std::size_t kTsSubjectMax = 64;

/// Upper bound on a `<slug:N>` length argument; anything larger (or
/// overflowing) is reported as malformed rather than parsed into an unusable
/// length.
constexpr std::size_t kSlugLengthMax = 4096;

// ---------------------------------------------------------------------------
// Unicode / NFC helpers (rule N6)
// ---------------------------------------------------------------------------

/// One canonical (base, combining-mark) -> precomposed mapping, Latin subset.
struct Composition
{
    char32_t base;  ///< The starter code point.
    char32_t mark;  ///< The combining mark code point.
    char32_t composed;  ///< The precomposed code point.
};

/**
 * @brief Canonical compositions for the Latin-1 accented letters.
 *
 * The conformance corpus compares human-facing text (slugs, labels, error
 * codes) that is ASCII plus, at most, accented Latin letters; the single NFC
 * vector (`n6_nfc_equal`) exercises `e`+combining-acute vs `é`. This focused
 * table makes NFC idempotent and correct over that Latin range without pulling
 * in a Unicode data library (forbidden by CLAUDE.md). Uppercase entries are
 * listed; the lowercase variants (offset `0x20` for this block) are derived at
 * table-build time.
 */
constexpr std::array<Composition, 26> kLatinCompositions = {{
    {U'A', 0x0300, U'À'}, {U'A', 0x0301, U'Á'}, {U'A', 0x0302, U'Â'},
    {U'A', 0x0303, U'Ã'}, {U'A', 0x0308, U'Ä'}, {U'A', 0x030A, U'Å'},
    {U'C', 0x0327, U'Ç'}, {U'E', 0x0300, U'È'}, {U'E', 0x0301, U'É'},
    {U'E', 0x0302, U'Ê'}, {U'E', 0x0308, U'Ë'}, {U'I', 0x0300, U'Ì'},
    {U'I', 0x0301, U'Í'}, {U'I', 0x0302, U'Î'}, {U'I', 0x0308, U'Ï'},
    {U'N', 0x0303, U'Ñ'}, {U'O', 0x0300, U'Ò'}, {U'O', 0x0301, U'Ó'},
    {U'O', 0x0302, U'Ô'}, {U'O', 0x0303, U'Õ'}, {U'O', 0x0308, U'Ö'},
    {U'U', 0x0300, U'Ù'}, {U'U', 0x0301, U'Ú'}, {U'U', 0x0302, U'Û'},
    {U'U', 0x0308, U'Ü'}, {U'Y', 0x0301, U'Ý'},
}};

/// Decodes one UTF-8 scalar at @p pos into @p cp; advances @p pos. Lone bytes
/// are passed through as a single code point so decoding never fails.
void utf8_next(const std::string& text, std::size_t& pos, char32_t& cp)
{
    const unsigned char lead = static_cast<unsigned char>(text[pos]);
    std::size_t extra = 0;
    char32_t value = lead;
    if (lead < 0x80) {
        extra = 0;
        value = lead;
    }
    else if ((lead & 0xE0) == 0xC0) {
        extra = 1;
        value = lead & 0x1F;
    }
    else if ((lead & 0xF0) == 0xE0) {
        extra = 2;
        value = lead & 0x0F;
    }
    else if ((lead & 0xF8) == 0xF0) {
        extra = 3;
        value = lead & 0x07;
    }
    else {
        cp = lead;
        ++pos;
        return;
    }
    if (pos + extra >= text.size()) {
        cp = lead;
        ++pos;
        return;
    }
    for (std::size_t i = 1; i <= extra; ++i) {
        const unsigned char cont = static_cast<unsigned char>(text[pos + i]);
        if ((cont & 0xC0) != 0x80) {
            cp = lead;
            ++pos;
            return;
        }
        value = (value << 6) | (cont & 0x3F);
    }
    pos += extra + 1;
    cp = value;
}

/// Appends the UTF-8 encoding of @p cp to @p out.
void utf8_append(std::string& out, char32_t cp)
{
    if (cp <= 0x7F) {
        out.push_back(static_cast<char>(cp));
    }
    else if (cp <= 0x7FF) {
        out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    }
    else if (cp <= 0xFFFF) {
        out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    }
    else {
        out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    }
}

/// Decomposes a precomposed Latin letter into (base, mark), else returns false.
bool decompose_one(char32_t cp, char32_t& base, char32_t& mark)
{
    for (const Composition& entry : kLatinCompositions) {
        if (entry.composed == cp) {
            base = entry.base;
            mark = entry.mark;
            return true;
        }
        const char32_t lower_composed = entry.composed + 0x20;
        if (lower_composed == cp) {
            base = entry.base + 0x20;
            mark = entry.mark;
            return true;
        }
    }
    return false;
}

/// Composes (base, mark) into a precomposed Latin letter, else returns false.
bool compose_pair(char32_t base, char32_t mark, char32_t& composed)
{
    for (const Composition& entry : kLatinCompositions) {
        if (entry.base == base && entry.mark == mark) {
            composed = entry.composed;
            return true;
        }
        if (entry.base + 0x20 == base && entry.mark == mark) {
            composed = entry.composed + 0x20;
            return true;
        }
    }
    return false;
}

/**
 * @brief Normalizes @p text to Unicode NFC over the supported Latin range.
 *
 * Fully decomposes any supported precomposed letter, then recomposes adjacent
 * (starter, mark) pairs. This makes NFC idempotent and lets a decomposed
 * `e`+combining-acute compare equal to a precomposed `é` (rule N6). Code points
 * outside the supported table pass through unchanged; there is no trimming.
 */
std::string nfc(const std::string& text)
{
    std::vector<char32_t> codepoints;
    codepoints.reserve(text.size());
    std::size_t pos = 0;
    while (pos < text.size()) {
        char32_t cp = 0;
        utf8_next(text, pos, cp);
        char32_t base = 0;
        char32_t mark = 0;
        if (decompose_one(cp, base, mark)) {
            codepoints.push_back(base);
            codepoints.push_back(mark);
        }
        else {
            codepoints.push_back(cp);
        }
    }

    std::vector<char32_t> composed;
    composed.reserve(codepoints.size());
    for (const char32_t cp : codepoints) {
        char32_t merged = 0;
        if (!composed.empty() && compose_pair(composed.back(), cp, merged)) {
            composed.back() = merged;
        }
        else {
            composed.push_back(cp);
        }
    }

    std::string out;
    out.reserve(text.size());
    for (const char32_t cp : composed) {
        utf8_append(out, cp);
    }
    return out;
}

// ---------------------------------------------------------------------------
// Value helpers (rule N3)
// ---------------------------------------------------------------------------

/// Returns the numeric value of @p value as a double (precondition: number).
double number_value(const Json& value)
{
    return value.kind() == Json::Kind::Int ? static_cast<double>(value.as_int())
                                           : value.as_double();
}

/// Rule N3: returns @p value as an integer when it is one, else `std::nullopt`.
std::optional<long long> as_integer(const Json& value)
{
    if (value.kind() == Json::Kind::Int) {
        return value.as_int();
    }
    if (value.kind() == Json::Kind::Double) {
        const double d = value.as_double();
        if (std::isfinite(d) && std::floor(d) == d) {
            return static_cast<long long>(d);
        }
    }
    return std::nullopt;
}

/// Structural equality by the N3/N6 value rules (used by `<var:name>`).
bool deep_equal(const Json& left, const Json& right)
{
    if (left.is_object() && right.is_object()) {
        if (left.members().size() != right.members().size()) {
            return false;
        }
        for (const Json::Member& member : left.members()) {
            const Json* other = right.find(member.first);
            if (other == nullptr || !deep_equal(member.second, *other)) {
                return false;
            }
        }
        return true;
    }
    if (left.is_array() && right.is_array()) {
        if (left.items().size() != right.items().size()) {
            return false;
        }
        for (std::size_t i = 0; i < left.items().size(); ++i) {
            if (!deep_equal(left.items()[i], right.items()[i])) {
                return false;
            }
        }
        return true;
    }
    if (left.is_object() || left.is_array() || right.is_object()
        || right.is_array())
    {
        return false;
    }
    if (left.is_bool() || right.is_bool()) {
        return left.is_bool() && right.is_bool()
            && left.as_bool() == right.as_bool();
    }
    if (left.is_number() && right.is_number()) {
        return number_value(left) == number_value(right);
    }
    if (left.is_string() && right.is_string()) {
        return nfc(left.as_string()) == nfc(right.as_string());
    }
    if (left.is_null() || right.is_null()) {
        return left.is_null() && right.is_null();
    }
    return false;
}

/// A short JSON type name for diagnostics (mirrors Python `_json_type`).
const char* json_type(const Json& value)
{
    switch (value.kind()) {
        case Json::Kind::Null:
            return "null";
        case Json::Kind::Bool:
            return "boolean";
        case Json::Kind::Int:
        case Json::Kind::Double:
            return "number";
        case Json::Kind::String:
            return "string";
        case Json::Kind::Array:
            return "array";
        case Json::Kind::Object:
            return "object";
    }
    return "unknown";
}

// ---------------------------------------------------------------------------
// Regex-dialect / ReDoS lint
// ---------------------------------------------------------------------------

/// Returns the index just past the `[...]` class at @p start (quantifier-safe).
std::size_t skip_char_class(const std::string& pattern, std::size_t start)
{
    const std::size_t len = pattern.size();
    std::size_t pos = start + 1;
    if (pos < len && pattern[pos] == '^') {
        ++pos;
    }
    if (pos < len && pattern[pos] == ']') {
        ++pos;
    }
    while (pos < len && pattern[pos] != ']') {
        pos += (pattern[pos] == '\\') ? 2 : 1;
    }
    return pos + 1;
}

/// Returns `true` if a group body holds a quantifier or alternation.
bool body_has_quantifier_or_alternation(const std::string& body)
{
    const std::size_t len = body.size();
    std::size_t pos = 0;
    while (pos < len) {
        const char c = body[pos];
        if (c == '\\') {
            pos += 2;
            continue;
        }
        if (c == '[') {
            pos = skip_char_class(body, pos);
            continue;
        }
        if (c == '*' || c == '+' || c == '?' || c == '{' || c == '|') {
            return true;
        }
        ++pos;
    }
    return false;
}

/// Flags a quantified group whose body risks catastrophic backtracking.
std::optional<std::string> nested_quantifier_reason(const std::string& pattern)
{
    const std::size_t len = pattern.size();
    std::size_t index = 0;
    std::stack<std::size_t> open_stack;
    while (index < len) {
        const char c = pattern[index];
        if (c == '\\') {
            index += 2;
            continue;
        }
        if (c == '[') {
            index = skip_char_class(pattern, index);
            continue;
        }
        if (c == '(') {
            open_stack.push(index);
            ++index;
            continue;
        }
        if (c == ')') {
            if (!open_stack.empty()) {
                const std::size_t open_index = open_stack.top();
                open_stack.pop();
                const std::size_t nxt = index + 1;
                if (nxt < len
                    && (pattern[nxt] == '*' || pattern[nxt] == '+'
                        || pattern[nxt] == '?' || pattern[nxt] == '{'))
                {
                    std::size_t body_start = open_index + 1;
                    const std::string prefix = pattern.substr(body_start, 2);
                    if (prefix == "?:" || prefix == "?=" || prefix == "?!") {
                        body_start += 2;
                    }
                    if (body_start <= index) {
                        const std::string body =
                            pattern.substr(body_start, index - body_start);
                        if (body_has_quantifier_or_alternation(body)) {
                            return std::string(
                                "nested or ambiguous quantifier "
                                "(catastrophic backtracking risk)");
                        }
                    }
                }
            }
            ++index;
            continue;
        }
        ++index;
    }
    return std::nullopt;
}

/// `true` if @p needle occurs in @p haystack at or after @p from.
bool contains(const std::string& haystack, const char* needle)
{
    return haystack.find(needle) != std::string::npos;
}

// ---------------------------------------------------------------------------
// Matcher tokens
// ---------------------------------------------------------------------------

/// A classified matcher token: its kind and optional parameter.
struct Token
{
    std::string kind;  ///< e.g. `"any"`, `"id"`, `"slug"`, `"regex"`.
    std::string arg;  ///< Parameter for `slug`/`regex`/`var`, else empty.
    bool has_arg = false;  ///< Whether @ref arg is meaningful.
};

/// Classifies @p value as a matcher token, or returns `std::nullopt` (literal).
std::optional<Token> parse_token(const std::string& value)
{
    static const std::array<const char*, 6> kBare = {
        "<any>", "<id>", "<ts>", "<int>", "<uint>", "<absent>"};
    for (const char* bare : kBare) {
        if (value == bare) {
            Token token;
            token.kind = value.substr(1, value.size() - 2);
            return token;
        }
    }
    struct Prefixed
    {
        const char* prefix;
        const char* kind;
    };
    static const std::array<Prefixed, 3> kPrefixed = {
        {{"<slug:", "slug"}, {"<regex:", "regex"}, {"<var:", "var"}}};
    for (const Prefixed& entry : kPrefixed) {
        const std::string prefix(entry.prefix);
        if (value.size() >= prefix.size() + 1
            && value.compare(0, prefix.size(), prefix) == 0
            && value.back() == '>')
        {
            Token token;
            token.kind = entry.kind;
            token.arg =
                value.substr(prefix.size(), value.size() - prefix.size() - 1);
            token.has_arg = true;
            return token;
        }
    }
    return std::nullopt;
}

/// `true` iff @p value is drawn entirely from the `<id>` character class.
bool is_id_text(const std::string& value)
{
    for (const char c : value) {
        const bool ok = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z')
            || (c >= '0' && c <= '9') || c == '_' || c == '.' || c == ':'
            || c == '-';
        if (!ok) {
            return false;
        }
    }
    return true;
}

/// `true` iff @p value is drawn entirely from the base62 slug alphabet.
bool is_slug_text(const std::string& value)
{
    for (const char c : value) {
        const bool ok = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z')
            || (c >= '0' && c <= '9');
        if (!ok) {
            return false;
        }
    }
    return true;
}

/// `true` iff @p value is an RFC 3339 UTC timestamp (rule for `<ts>`).
bool is_ts_text(const std::string& value)
{
    // Bound the subject before touching the engine (CWE-674). A valid timestamp
    // is well under this cap, so this changes no verdict - it only refuses to
    // scan an attacker-sized string.
    if (value.size() > kTsSubjectMax) {
        return false;
    }
    static const std::regex kTs(
        R"(\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2}(?:\.\d{1,9})?Z)",
        std::regex::ECMAScript);
    return std::regex_match(value, kTs);
}

/**
 * @brief Tests @p actual against a matcher token; returns a reason on failure.
 *
 * @return `std::nullopt` on success, else a human-readable mismatch reason
 * (without a path prefix - the caller prepends the JSONPath location).
 */
std::optional<std::string> match_token(const Token& token,
                                       const Json& actual,
                                       const VariableMap& vars)
{
    const std::string& kind = token.kind;
    if (kind == "any") {
        return std::nullopt;
    }
    if (kind == "id") {
        if (actual.is_string() && !actual.as_string().empty()
            && actual.as_string().size() <= 64
            && is_id_text(actual.as_string()))
        {
            return std::nullopt;
        }
        return std::string("expected <id>, got ") + actual.dump();
    }
    if (kind == "ts") {
        if (actual.is_string() && is_ts_text(actual.as_string())) {
            return std::nullopt;
        }
        return std::string("expected <ts> (RFC 3339 UTC), got ")
            + actual.dump();
    }
    if (kind == "int") {
        if (as_integer(actual).has_value()) {
            return std::nullopt;
        }
        return std::string("expected <int>, got ") + actual.dump();
    }
    if (kind == "uint") {
        const std::optional<long long> value = as_integer(actual);
        if (value.has_value() && *value >= 0) {
            return std::nullopt;
        }
        return std::string("expected <uint>, got ") + actual.dump();
    }
    if (kind == "slug") {
        // Bounded digit parse: no std::stoul (which throws out_of_range on an
        // over-long argument). A non-digit or overflowing argument is
        // malformed.
        std::size_t length = 0;
        bool numeric = !token.arg.empty();
        for (const char c : token.arg) {
            if (c < '0' || c > '9') {
                numeric = false;
                break;
            }
            length = length * 10 + static_cast<std::size_t>(c - '0');
            if (length > kSlugLengthMax) {
                numeric = false;
                break;
            }
        }
        if (!numeric) {
            return std::string("malformed <slug:N> token argument ")
                + token.arg;
        }
        if (actual.is_string() && actual.as_string().size() == length
            && is_slug_text(actual.as_string()))
        {
            return std::nullopt;
        }
        return std::string("expected <slug:") + token.arg + ">, got "
            + actual.dump();
    }
    if (kind == "regex") {
        const std::string& pattern = token.arg;
        const std::optional<std::string> bad =
            Matcher::forbidden_regex_reason(pattern);
        if (bad.has_value()) {
            // Refused BEFORE any std::regex is built, so a catastrophic
            // pattern is never executed (CWE-1333).
            return std::string("<regex> pattern uses a disallowed construct: ")
                + *bad;
        }
        try {
            const std::regex compiled(pattern, std::regex::ECMAScript);
            // Bound the subject before matching (CWE-674): an over-long subject
            // is a mismatch and is never scanned. Python applies the same cap
            // so the verdict stays identical (pinned by a vector).
            if (actual.is_string()
                && actual.as_string().size() <= kRegexSubjectMax
                && std::regex_match(actual.as_string(), compiled))
            {
                return std::nullopt;
            }
        } catch (const std::regex_error&) {
            return std::string("<regex> pattern failed to compile: ") + pattern;
        }
        return std::string("expected <regex:") + pattern + "> full match, got "
            + actual.dump();
    }
    if (kind == "var") {
        const std::string& name = token.arg;
        const auto it = vars.find(name);
        if (it == vars.end()) {
            return std::string("unknown variable '") + name
                + "' referenced by <var>";
        }
        if (deep_equal(it->second, actual)) {
            return std::nullopt;
        }
        return std::string("expected <var:") + name
            + "> == " + it->second.dump() + ", got " + actual.dump();
    }
    if (kind == "absent") {
        return std::string(
            "<absent> may only be used as an object field value");
    }
    return std::string("unknown matcher token kind ") + kind;
}

// ---------------------------------------------------------------------------
// Structural match
// ---------------------------------------------------------------------------

void match_value(const Json& expected,
                 const Json& actual,
                 bool subset,
                 const VariableMap& vars,
                 const std::string& path,
                 std::vector<std::string>& reasons);

/// Matches an expected object, honouring `<absent>` and the match mode.
void match_object(const Json& expected,
                  const Json& actual,
                  bool subset,
                  const VariableMap& vars,
                  const std::string& path,
                  std::vector<std::string>& reasons)
{
    if (!actual.is_object()) {
        reasons.push_back(path + ": expected object, got " + json_type(actual));
        return;
    }
    std::vector<std::string> expected_keys;
    std::vector<std::string> absent_keys;
    for (const Json::Member& member : expected.members()) {
        const std::string child = path + "." + member.first;
        if (member.second.is_string()
            && member.second.as_string() == "<absent>")
        {
            absent_keys.push_back(member.first);
            if (actual.find(member.first) != nullptr) {
                reasons.push_back(child + ": expected key to be absent");
            }
            continue;
        }
        expected_keys.push_back(member.first);
        const Json* child_actual = actual.find(member.first);
        if (child_actual == nullptr) {
            reasons.push_back(child + ": missing key");
            continue;
        }
        match_value(member.second, *child_actual, subset, vars, child, reasons);
    }
    if (!subset) {
        for (const Json::Member& member : actual.members()) {
            const bool is_expected =
                std::find(
                    expected_keys.begin(), expected_keys.end(), member.first)
                != expected_keys.end();
            const bool is_absent =
                std::find(absent_keys.begin(), absent_keys.end(), member.first)
                != absent_keys.end();
            if (!is_expected && !is_absent) {
                reasons.push_back(path + "." + member.first
                                  + ": unexpected key");
            }
        }
    }
}

/// Matches an expected array (always element-for-element, even in subset).
void match_array(const Json& expected,
                 const Json& actual,
                 bool subset,
                 const VariableMap& vars,
                 const std::string& path,
                 std::vector<std::string>& reasons)
{
    if (!actual.is_array()) {
        reasons.push_back(path + ": expected array, got " + json_type(actual));
        return;
    }
    if (expected.items().size() != actual.items().size()) {
        reasons.push_back(
            path + ": array length " + std::to_string(actual.items().size())
            + " != expected " + std::to_string(expected.items().size()));
        return;
    }
    for (std::size_t i = 0; i < expected.items().size(); ++i) {
        const std::string child = path + "[" + std::to_string(i) + "]";
        match_value(expected.items()[i],
                    actual.items()[i],
                    subset,
                    vars,
                    child,
                    reasons);
    }
}

/// Matches an expected string, interpreting a recognized `<...>` token.
void match_string(const Json& expected,
                  const Json& actual,
                  const VariableMap& vars,
                  const std::string& path,
                  std::vector<std::string>& reasons)
{
    const std::string& text = expected.as_string();
    const std::optional<Token> token = parse_token(text);
    if (!token.has_value()) {
        if (actual.is_string() && nfc(actual.as_string()) == nfc(text)) {
            return;
        }
        reasons.push_back(path + ": expected string " + expected.dump()
                          + ", got " + actual.dump());
        return;
    }
    // A value normalized to the token sentinel (e.g. N4 replaces id with
    // "<id>") matches the token trivially.
    if (actual.is_string() && actual.as_string() == text) {
        return;
    }
    const std::optional<std::string> reason = match_token(*token, actual, vars);
    if (reason.has_value()) {
        reasons.push_back(path + ": " + *reason);
    }
}

void match_value(const Json& expected,
                 const Json& actual,
                 bool subset,
                 const VariableMap& vars,
                 const std::string& path,
                 std::vector<std::string>& reasons)
{
    switch (expected.kind()) {
        case Json::Kind::Object:
            match_object(expected, actual, subset, vars, path, reasons);
            return;
        case Json::Kind::Array:
            match_array(expected, actual, subset, vars, path, reasons);
            return;
        case Json::Kind::String:
            match_string(expected, actual, vars, path, reasons);
            return;
        case Json::Kind::Bool:
            if (actual.is_bool() && actual.as_bool() == expected.as_bool()) {
                return;
            }
            reasons.push_back(path + ": expected boolean " + expected.dump()
                              + ", got " + actual.dump());
            return;
        case Json::Kind::Int:
        case Json::Kind::Double:
            if (actual.is_bool() || !actual.is_number()) {
                reasons.push_back(path + ": expected number " + expected.dump()
                                  + ", got " + actual.dump());
                return;
            }
            if (number_value(actual) == number_value(expected)) {
                return;
            }
            reasons.push_back(path + ": expected number " + expected.dump()
                              + ", got " + actual.dump());
            return;
        case Json::Kind::Null:
            if (actual.is_null()) {
                return;
            }
            reasons.push_back(path + ": expected null, got " + actual.dump());
            return;
    }
}

/// Stringifies a scalar JSON value for `${name}` inlining (mirrors `str()`).
std::string stringify_scalar(const Json& value)
{
    switch (value.kind()) {
        case Json::Kind::String:
            return value.as_string();
        case Json::Kind::Bool:
            return value.as_bool() ? "True" : "False";
        case Json::Kind::Null:
            return "None";
        default:
            return value.dump();
    }
}

}  // namespace

Json Matcher::substitute(const Json& value,
                         const VariableMap& vars,
                         std::string* error)
{
    if (value.is_object()) {
        Json out = Json::object();
        for (const Json::Member& member : value.members()) {
            out.set(member.first, substitute(member.second, vars, error));
        }
        return out;
    }
    if (value.is_array()) {
        Json out = Json::array();
        for (const Json& element : value.items()) {
            out.push_back(substitute(element, vars, error));
        }
        return out;
    }
    if (!value.is_string()) {
        return value;
    }

    const std::string& text = value.as_string();
    std::string result;
    std::size_t pos = 0;
    while (pos < text.size()) {
        if (text[pos] == '$' && pos + 1 < text.size() && text[pos + 1] == '{') {
            const std::size_t close = text.find('}', pos + 2);
            if (close != std::string::npos) {
                const std::string name =
                    text.substr(pos + 2, close - (pos + 2));
                bool valid = !name.empty()
                    && (std::isalpha(static_cast<unsigned char>(name.front()))
                        || name.front() == '_');
                for (std::size_t i = 1; valid && i < name.size(); ++i) {
                    const char c = name[i];
                    valid =
                        std::isalnum(static_cast<unsigned char>(c)) || c == '_';
                }
                if (valid) {
                    const auto it = vars.find(name);
                    if (it == vars.end()) {
                        if (error != nullptr) {
                            *error = std::string("unknown variable '") + name
                                + "' in substitution";
                        }
                        return value;
                    }
                    std::string inlined = stringify_scalar(it->second);
                    if (name == "tmp" && it->second.is_string()) {
                        std::string rewritten;
                        rewritten.reserve(inlined.size());
                        for (const char c : inlined) {
                            rewritten.push_back(c == '\\' ? '/' : c);
                        }
                        inlined = std::move(rewritten);
                    }
                    result += inlined;
                    pos = close + 1;
                    continue;
                }
            }
        }
        result.push_back(text[pos]);
        ++pos;
    }
    return Json::string(std::move(result));
}

Json Matcher::normalize_actual(const Json& envelope,
                               const Json* request,
                               bool carries_id,
                               std::vector<std::string>* findings)
{
    if (!envelope.is_object()) {
        return envelope;
    }

    // N1: drop the human-only error fields while copying.
    Json env = Json::object();
    for (const Json::Member& member : envelope.members()) {
        if (member.first == "error" && member.second.is_object()) {
            Json error = Json::object();
            for (const Json::Member& field : member.second.members()) {
                if (field.first != "message" && field.first != "details") {
                    error.set(field.first, field.second);
                }
            }
            env.set("error", std::move(error));
        }
        else {
            env.set(member.first, member.second);
        }
    }

    // N5: no extra top-level keys.
    for (const Json::Member& member : env.members()) {
        const std::string& key = member.first;
        const bool allowed = key == "v" || key == "id" || key == "ok"
            || key == "result" || key == "job" || key == "error";
        if (!allowed && findings != nullptr) {
            findings->push_back("$." + key
                                + ": unexpected top-level envelope key (N5)");
        }
    }

    // N4: id echo / generated-id check, then replace with the sentinel.
    const Json* id = env.find("id");
    if (id != nullptr) {
        const Json* request_id =
            request != nullptr ? request->find("id") : nullptr;
        if (request_id != nullptr && carries_id) {
            if (!deep_equal(*id, *request_id) && findings != nullptr) {
                findings->push_back("$.id: expected echoed request id "
                                    + request_id->dump() + ", got " + id->dump()
                                    + " (N4)");
            }
        }
        else {
            static const std::regex kSrv(R"(srv-[0-9]+)",
                                         std::regex::ECMAScript);
            const bool ok =
                id->is_string() && std::regex_match(id->as_string(), kSrv);
            if (!ok && findings != nullptr) {
                findings->push_back("$.id: generated id " + id->dump()
                                    + " does not match ^srv-[0-9]+$ (N4)");
            }
        }

        Json normalized = Json::object();
        for (const Json::Member& member : env.members()) {
            if (member.first == "id") {
                normalized.set("id", Json::string("<id>"));
            }
            else {
                normalized.set(member.first, member.second);
            }
        }
        return normalized;
    }

    return env;
}

MatchResult Matcher::match(const Json& expected,
                           const Json& actual,
                           bool subset,
                           const VariableMap& vars)
{
    MatchResult result;
    match_value(expected, actual, subset, vars, "$", result.findings);
    result.ok = result.findings.empty();
    return result;
}

std::optional<std::string> Matcher::forbidden_regex_reason(
    const std::string& pattern)
{
    if (contains(pattern, "(?<=") || contains(pattern, "(?<!")) {
        return std::string("lookbehind");
    }
    if (contains(pattern, "(?P<") || contains(pattern, "(?P=")) {
        return std::string("Python-only named group / backreference");
    }
    for (std::size_t i = 0; i + 2 < pattern.size(); ++i) {
        if (pattern[i] == '(' && pattern[i + 1] == '?' && pattern[i + 2] == '<')
        {
            if (i + 3 < pattern.size()) {
                const char c = pattern[i + 3];
                if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z')
                    || c == '_')
                {
                    return std::string("named group");
                }
            }
        }
    }
    if (contains(pattern, "(?'")) {
        return std::string("named group");
    }
    for (std::size_t i = 0; i + 1 < pattern.size(); ++i) {
        const char c = pattern[i];
        if ((c == '*' || c == '+' || c == '?' || c == '}')
            && pattern[i + 1] == '+')
        {
            return std::string("possessive quantifier");
        }
    }
    return nested_quantifier_reason(pattern);
}

}  // namespace conformance
