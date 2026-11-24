#include <url_shortener/commands/pair_formatter.hpp>

#include <algorithm>
#include <string>
#include <vector>

#include <url_shortener/app/link_command_service.hpp>
#include <url_shortener/core/utils.hpp>

namespace url_shortener::commands
{

namespace
{

/**
 * @brief Quotes one CSV field per the RFC 4180 line-based subset when needed.
 *
 * Returns @p field verbatim unless it contains a delimiter the line-based
 * parser would otherwise mis-split on: a comma, a double quote, a CR or an LF.
 * In that case the field is wrapped in double quotes and every embedded double
 * quote is doubled (`"` -> `""`). No byte is dropped or transformed otherwise,
 * so the result is the exact inverse of @ref PairLineParser's CSV tokenizer and
 * is safe for any byte value.
 *
 * @param field The raw field text (a target URL or slug).
 * @return The field, quoted and escaped only when it needs it.
 */
std::string csv_quote(const std::string& field)
{
    bool needs_quoting = false;
    for (const char c : field) {
        if (c == ',' || c == '"' || c == '\r' || c == '\n') {
            needs_quoting = true;
            break;
        }
    }
    if (!needs_quoting) {
        return field;
    }

    std::string out;
    out.reserve(field.size() + 2);
    out.push_back('"');
    for (const char c : field) {
        if (c == '"') {
            out.push_back('"');  // double an embedded quote
        }
        out.push_back(c);
    }
    out.push_back('"');
    return out;
}

/**
 * @brief Appends `"key":value` to @p out, prefixing a comma for a non-first
 * member.
 *
 * @param out The object buffer being built (without its enclosing braces yet).
 * @param first `true` before any member has been written; set to `false` after
 * this call so the next member is comma-separated.
 * @param key The bare member name (no surrounding quotes); a fixed schema token.
 * @param value The already-serialized JSON value text (string, bool, array or
 * object).
 */
void append_member(std::string& out,
                   bool& first,
                   const char* key,
                   const std::string& value)
{
    if (!first) {
        out.push_back(',');
    }
    first = false;
    out.push_back('"');
    out.append(key);
    out.append("\":");
    out.append(value);
}

/**
 * @brief Serializes a string list as a JSON array of escaped strings.
 *
 * @param values The strings to emit, in order.
 * @return `[]` when empty, otherwise `["a","b",...]` with each element escaped
 * by `jsonString`.
 */
std::string json_string_array(const std::vector<std::string>& values)
{
    std::string out = "[";
    bool first = true;
    for (const std::string& value : values) {
        if (!first) {
            out.push_back(',');
        }
        first = false;
        out.append(jsonString(value));
    }
    out.push_back(']');
    return out;
}

/**
 * @brief Serializes the campaign's present fields as a JSON object.
 *
 * Emits only the set members, in the fixed `link.create` field order
 * (`name`, `source`, `medium`, `term`, `content`, `id`), each escaped by
 * `jsonString`. An all-unset campaign yields `{}`, which `link.create` accepts
 * as a present empty campaign.
 *
 * @param campaign The decoded campaign to serialize.
 * @return The campaign object text.
 */
std::string format_campaign(const Link::Campaign& campaign)
{
    std::string out = "{";
    bool first = true;
    const auto field = [&](const char* key,
                           const std::optional<std::string>& value)
    {
        if (value.has_value()) {
            append_member(out, first, key, jsonString(*value));
        }
    };
    field("name", campaign.name);
    field("source", campaign.source);
    field("medium", campaign.medium);
    field("term", campaign.term);
    field("content", campaign.content);
    field("id", campaign.id);
    out.push_back('}');
    return out;
}

}  // namespace

std::string csv_header()
{
    return "full_url,slug\n";
}

std::string format_csv_row(const app::LinkView& link)
{
    return csv_quote(link.target_url) + "," + csv_quote(link.slug) + "\n";
}

std::string format_ndjson_row(const app::LinkView& link)
{
    std::string out = "{";
    bool first = true;

    append_member(out, first, "target_url", jsonString(link.target_url));
    append_member(out, first, "slug", jsonString(link.slug));
    append_member(out, first, "redirect_type",
                  jsonString(redirectTypeToString(link.redirect_type)));
    append_member(out, first, "enabled", link.enabled ? "true" : "false");
    append_member(out, first, "tags", json_string_array(link.tags));

    // metadata keys are emitted in sorted order for a deterministic line (the
    // backing map is unordered).
    std::vector<std::string> keys;
    keys.reserve(link.metadata.size());
    for (const auto& entry : link.metadata) {
        keys.push_back(entry.first);
    }
    std::sort(keys.begin(), keys.end());
    std::string metadata = "{";
    bool meta_first = true;
    for (const std::string& key : keys) {
        if (!meta_first) {
            metadata.push_back(',');
        }
        meta_first = false;
        metadata.append(jsonString(key));
        metadata.push_back(':');
        metadata.append(jsonString(link.metadata.at(key)));
    }
    metadata.push_back('}');
    append_member(out, first, "metadata", metadata);

    if (link.expires_at.has_value()) {
        append_member(out, first, "expires_at", jsonString(*link.expires_at));
    }
    if (link.campaign.has_value()) {
        append_member(out, first, "campaign", format_campaign(*link.campaign));
    }

    out.append("}\n");
    return out;
}

}  // namespace url_shortener::commands
