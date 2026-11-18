#include <cctype>
#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

#include "url_shortener/tui/equivalent_cmd.hpp"

#include <url_shortener/core/utils.hpp>
#include <url_shortener/tui/json_view.hpp>

namespace url_shortener::tui
{

namespace
{

/// Lower-cases @p s for case-insensitive key matching (ASCII only; argument
/// keys are ASCII identifiers).
std::string to_lower(const std::string& s)
{
    std::string out;
    out.reserve(s.size());
    for (char c : s) {
        out.push_back(
            static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    }
    return out;
}

/// Whether @p key names a potentially-secret argument whose value must never be
/// echoed (threat model T-125). Command args carry no secrets (contract C6);
/// this is defence in depth, matching
/// `StorageObservability::redactSecretValue`.
bool is_redact_worthy(const std::string& key)
{
    const std::string lower = to_lower(key);
    return lower.find("password") != std::string::npos
        || lower.find("secret") != std::string::npos
        || lower.find("token") != std::string::npos
        || lower.find("dsn") != std::string::npos
        || lower.find("key") != std::string::npos;
}

/// Maximum nesting the recursive redactor walks; a value deeper than this is
/// redacted whole (fail closed) rather than echoed un-scanned (threat model
/// T-121/T-125).
constexpr int k_redact_max_depth = 32;

/// The token substituted for any redacted value, at any depth.
constexpr char k_redacted[] = "\"***\"";

/// Trims leading/trailing JSON whitespace so the first significant character
/// can classify a raw value as object / array / scalar.
std::string_view trim_ws(const std::string& s)
{
    const auto is_ws = [](char c)
    { return c == ' ' || c == '\t' || c == '\n' || c == '\r'; };
    std::size_t begin = 0;
    std::size_t end = s.size();
    while (begin < end && is_ws(s[begin])) {
        ++begin;
    }
    while (end > begin && is_ws(s[end - 1])) {
        --end;
    }
    return std::string_view(s).substr(begin, end - begin);
}

// Forward declaration for the mutual recursion object <-> array <-> value.
std::string redact_value(const std::string& raw, int depth, bool& changed);

/// Rebuilds a JSON object with redact-worthy values (at this level) replaced by
/// `"***"` and non-secret container values recursed into. An object that cannot
/// be scanned (malformed, over-limit, or nested past the scanner's depth cap)
/// and is not the empty object is redacted whole (fail closed).
std::string redact_object(const std::string& raw, int depth, bool& changed)
{
    // Iterate members through json_view (the sanctioned scanner wrapper); TUI
    // code never tokenizes JSON itself (contract C9).
    const std::vector<JsonField> fields = object_fields(raw);
    if (fields.empty()) {
        if (trim_ws(raw) == "{}") {
            return "{}";
        }
        changed = true;  // unparseable object: fail closed
        return k_redacted;
    }

    std::string out = "{";
    bool first = true;
    for (const JsonField& f : fields) {
        if (!first) {
            out += ",";
        }
        first = false;
        out += url_shortener::jsonString(f.key);
        out += ":";
        if (is_redact_worthy(f.key)) {
            out += k_redacted;
            changed = true;
        }
        else {
            out += redact_value(f.raw_value, depth + 1, changed);
        }
    }
    out += "}";
    return out;
}

/// Rebuilds a JSON array, recursing into each element (so a secret inside an
/// array-of-objects is redacted). An array that cannot be scanned and is not
/// the empty array is redacted whole (fail closed).
std::string redact_array(const std::string& raw, int depth, bool& changed)
{
    const std::vector<std::string> elements = split_array(raw);
    if (elements.empty()) {
        if (trim_ws(raw) == "[]") {
            return "[]";
        }
        changed = true;  // unparseable array: fail closed
        return k_redacted;
    }

    std::string out = "[";
    bool first = true;
    for (const std::string& element : elements) {
        if (!first) {
            out += ",";
        }
        first = false;
        out += redact_value(element, depth + 1, changed);
    }
    out += "]";
    return out;
}

/// Redacts one JSON value: recurses into objects/arrays, leaves scalars
/// verbatim, and fails closed past @ref k_redact_max_depth.
std::string redact_value(const std::string& raw, int depth, bool& changed)
{
    if (depth > k_redact_max_depth) {
        changed = true;  // too deep to scan safely: fail closed
        return k_redacted;
    }
    const std::string_view trimmed = trim_ws(raw);
    if (!trimmed.empty() && trimmed.front() == '{') {
        return redact_object(raw, depth, changed);
    }
    if (!trimmed.empty() && trimmed.front() == '[') {
        return redact_array(raw, depth, changed);
    }
    return raw;  // scalar (string / number / boolean / null): verbatim
}

/// Returns @p args_json with any redact-worthy value replaced by `"***"` at
/// *any* nesting depth (threat model T-125). When nothing is redacted the exact
/// original bytes are returned, so the common path stays byte-identical to the
/// catalog example.
std::string redact_args(const std::string& args_json)
{
    bool changed = false;
    const std::string redacted = redact_value(args_json, 0, changed);
    return changed ? redacted : args_json;
}

/// POSIX single-quote escaping: wraps @p s in single quotes and renders every
/// embedded `'` as `'\''`, so no content can break out of the quoting.
std::string shell_single_quote(const std::string& s)
{
    std::string out = "'";
    for (char c : s) {
        if (c == '\'') {
            out += "'\\''";
        }
        else {
            out += c;
        }
    }
    out += "'";
    return out;
}

}  // namespace

std::string equivalent_cli(const commands::CommandRequest& r)
{
    return "url_shortener cmd " + r.command + " --json "
        + shell_single_quote(redact_args(r.args_json));
}

std::string equivalent_envelope(const commands::CommandRequest& r)
{
    std::string out = "{\"v\":" + std::to_string(r.v) + ",";
    if (!r.id.empty()) {
        out += "\"id\":" + url_shortener::jsonString(r.id) + ",";
    }
    out += "\"command\":" + url_shortener::jsonString(r.command);
    out += ",\"args\":" + redact_args(r.args_json);
    if (r.options.dry_run || r.options.async) {
        out += ",\"options\":{\"dry_run\":";
        out += r.options.dry_run ? "true" : "false";
        out += ",\"async\":";
        out += r.options.async ? "true" : "false";
        out += "}";
    }
    out += "}";
    return out;
}

}  // namespace url_shortener::tui
