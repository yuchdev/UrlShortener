/**
 * @file link_args_json.cpp
 * @brief Implementations of the `link <verb>` DTO -> `args` JSON converters.
 *
 * Every `args` object is assembled only through
 * `url_shortener::commands::args_builder` (milestone contract C9), so the one
 * JSON-escaping path handles all untrusted flag values; the sole `add_raw`
 * caller here inserts a nested `campaign` object that was itself produced by a
 * second `args_builder` (its documented safe use). No validation beyond
 * choosing JSON types happens here (contract C1).
 */
#include <url_shortener/cli/link_args_json.hpp>

#include <map>
#include <optional>
#include <string>
#include <string_view>

#include <url_shortener/app/link_command_service.hpp>
#include <url_shortener/cli/cli_parser.hpp>
#include <url_shortener/commands/args_builder.hpp>
#include <url_shortener/core/types.hpp>
#include <url_shortener/core/utils.hpp>

namespace url_shortener::cli
{
namespace
{
using commands::args_builder;

/**
 * @brief Serializes a campaign's present UTM members into a JSON object token.
 *
 * Emits only the engaged members, in the fixed field order
 * `name, source, medium, term, content, id`, through a dedicated
 * `args_builder` so the token is a single well-formed object (`"{}"` when no
 * member is set) whose values are all JSON-escaped. The returned text is a
 * valid JSON value suitable for `args_builder::add_raw`, and its member names
 * are exactly the `campaign` object's declared `ArgSpec` fields, so it passes
 * the nested-object validation of `link.create`/`link.update`.
 *
 * @param campaign The campaign attribution to serialize.
 * @return The JSON object text for @p campaign.
 */
std::string campaign_object_json(const Link::Campaign& campaign)
{
    args_builder builder;
    if (campaign.name) {
        builder.add_string("name", *campaign.name);
    }
    if (campaign.source) {
        builder.add_string("source", *campaign.source);
    }
    if (campaign.medium) {
        builder.add_string("medium", *campaign.medium);
    }
    if (campaign.term) {
        builder.add_string("term", *campaign.term);
    }
    if (campaign.content) {
        builder.add_string("content", *campaign.content);
    }
    if (campaign.id) {
        builder.add_string("id", *campaign.id);
    }
    return builder.str();
}

/**
 * @brief Copies an unordered metadata map into a sorted `std::map`.
 *
 * `args_builder::add_string_map` takes a `std::map` (and emits keys sorted);
 * the DTOs hold `std::unordered_map`, so this adapts the one to the other.
 *
 * @param source The DTO metadata map.
 * @return A `std::map` with the same entries.
 */
std::map<std::string, std::string> to_sorted_map(
    const std::unordered_map<std::string, std::string>& source)
{
    return std::map<std::string, std::string>(source.begin(), source.end());
}

}  // namespace

std::string create_args_json(const app::CreateLinkCommand& command)
{
    args_builder builder;
    builder.add_string("target_url", command.target_url);
    if (command.slug) {
        builder.add_string("slug", *command.slug);
    }
    if (command.redirect_type) {
        builder.add_string("redirect_type",
                           redirectTypeToString(*command.redirect_type));
    }
    if (command.expires_at) {
        builder.add_string("expires_at", *command.expires_at);
    }
    if (command.enabled) {
        builder.add_bool("enabled", *command.enabled);
    }
    if (!command.tags.empty()) {
        builder.add_string_list("tags", command.tags);
    }
    if (!command.metadata.empty()) {
        builder.add_string_map("metadata", to_sorted_map(command.metadata));
    }
    if (command.campaign) {
        builder.add_raw("campaign", campaign_object_json(*command.campaign));
    }
    return builder.str();
}

std::string get_args_json(const app::GetLinkQuery& query)
{
    args_builder builder;
    const std::string_view key =
        query.by == app::GetLinkBy::slug ? "slug" : "id";
    builder.add_string(key, query.value);
    return builder.str();
}

std::string update_args_json(const app::UpdateLinkCommand& command)
{
    args_builder builder;
    builder.add_string("slug", command.slug);
    if (command.enabled) {
        builder.add_bool("enabled", *command.enabled);
    }
    if (command.expires_at) {
        // Outer engaged => the field is present. Inner empty => explicit null
        // (clear the expiry); inner value => set it.
        if (*command.expires_at) {
            builder.add_string("expires_at", **command.expires_at);
        } else {
            builder.add_null("expires_at");
        }
    }
    if (command.tags) {
        // Present (even empty) replaces the whole list; an empty list clears.
        builder.add_string_list("tags", *command.tags);
    }
    if (command.metadata) {
        builder.add_string_map("metadata", to_sorted_map(*command.metadata));
    }
    if (command.campaign) {
        // Same tri-state as expires_at: inner empty clears the campaign.
        if (*command.campaign) {
            builder.add_raw("campaign", campaign_object_json(**command.campaign));
        } else {
            builder.add_null("campaign");
        }
    }
    return builder.str();
}

std::string slug_args_json(const std::string& slug)
{
    args_builder builder;
    builder.add_string("slug", slug);
    return builder.str();
}

std::string stats_args_json(const app::GetLinkStatsQuery& query)
{
    args_builder builder;
    builder.add_string("slug", query.slug);
    builder.add_string("from", query.from);
    builder.add_string("to", query.to);
    builder.add_string("bucket", query.bucket);
    return builder.str();
}

std::string_view command_name_for(LinkCliVerb verb)
{
    switch (verb) {
    case LinkCliVerb::create:
        return "link.create";
    case LinkCliVerb::get:
        return "link.get";
    case LinkCliVerb::update:
        return "link.update";
    case LinkCliVerb::del:
        return "link.delete";
    case LinkCliVerb::enable:
        return "link.enable";
    case LinkCliVerb::disable:
        return "link.disable";
    case LinkCliVerb::restore:
        return "link.restore";
    case LinkCliVerb::preview:
        return "link.preview";
    case LinkCliVerb::stats:
        return "link.stats";
    }
    // UNREACHABLE: LinkCliVerb is a closed enum exhaustively handled above; the
    // no-default switch makes a new enumerator a compile-time prompt. Returning
    // an empty view keeps the function total without masking that prompt.
    return {};
}

}  // namespace url_shortener::cli
