#include "url_shortener/tui/equivalent_cmd.hpp"

#include <cctype>
#include <string>
#include <vector>

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
    for (char c : s)
    {
        out.push_back(static_cast<char>(
            std::tolower(static_cast<unsigned char>(c))));
    }
    return out;
}

/// Whether @p key names a potentially-secret argument whose value must never be
/// echoed (threat model T-125). Command args carry no secrets (contract C6);
/// this is defence in depth, matching `StorageObservability::redactSecretValue`.
bool is_redact_worthy(const std::string& key)
{
    const std::string lower = to_lower(key);
    return lower.find("password") != std::string::npos ||
           lower.find("secret") != std::string::npos ||
           lower.find("token") != std::string::npos ||
           lower.find("dsn") != std::string::npos ||
           lower.find("key") != std::string::npos;
}

/// Returns @p args_json with any redact-worthy top-level value replaced by
/// `"***"`. When @p args_json has no such key the exact original bytes are
/// returned (so the common path is byte-identical to the catalog example);
/// when it cannot be scanned as an object it is returned verbatim (the
/// single-quote escaping at the call site still prevents quote break-out).
std::string redact_args(const std::string& args_json)
{
    // Iterate members through json_view (the sanctioned scanner wrapper); TUI
    // code never tokenizes JSON itself (contract C9).
    const std::vector<JsonField> fields = object_fields(args_json);

    bool any_redacted = false;
    for (const JsonField& f : fields)
    {
        if (is_redact_worthy(f.key))
        {
            any_redacted = true;
            break;
        }
    }
    // No object, no members, or nothing to redact: keep the exact source text
    // (so the common path is byte-identical to the catalog example).
    if (!any_redacted)
    {
        return args_json;
    }

    std::string out = "{";
    bool first = true;
    for (const JsonField& f : fields)
    {
        if (!first)
        {
            out += ",";
        }
        first = false;
        out += url_shortener::jsonString(f.key);
        out += ":";
        out += is_redact_worthy(f.key) ? std::string("\"***\"") : f.raw_value;
    }
    out += "}";
    return out;
}

/// POSIX single-quote escaping: wraps @p s in single quotes and renders every
/// embedded `'` as `'\''`, so no content can break out of the quoting.
std::string shell_single_quote(const std::string& s)
{
    std::string out = "'";
    for (char c : s)
    {
        if (c == '\'')
        {
            out += "'\\''";
        }
        else
        {
            out += c;
        }
    }
    out += "'";
    return out;
}

}  // namespace

std::string equivalent_cli(const commands::CommandRequest& r)
{
    return "url_shortener cmd " + r.command + " --json " +
           shell_single_quote(redact_args(r.args_json));
}

std::string equivalent_envelope(const commands::CommandRequest& r)
{
    std::string out = "{\"v\":" + std::to_string(r.v) + ",";
    if (!r.id.empty())
    {
        out += "\"id\":" + url_shortener::jsonString(r.id) + ",";
    }
    out += "\"command\":" + url_shortener::jsonString(r.command);
    out += ",\"args\":" + redact_args(r.args_json);
    if (r.options.dry_run || r.options.async)
    {
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
