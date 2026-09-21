/**
 * @file link_command_args.cpp
 * @brief Implementation of the per-verb argv-to-DTO mapping for `link create`,
 * `link get`, and `link stats`.
 */
#include <url_shortener/cli/link_command_args.hpp>

#include <stdexcept>
#include <string>
#include <vector>

#include <boost/program_options.hpp>

#include <url_shortener/core/utils.h>

namespace po = boost::program_options;

namespace url_shortener {
namespace cli {

namespace {

/**
 * @brief Parse a textual boolean, mirroring the server-flag parser's tokens.
 *
 * @param flag  Flag name, used only for the error message.
 * @param value Raw value token.
 * @return bool Parsed boolean.
 * @throws std::invalid_argument If @p value is not a recognized boolean token.
 */
bool parseBoolFlag(const std::string& flag, const std::string& value)
{
    if (value == "1" || value == "true" || value == "on" || value == "yes") {
        return true;
    }
    if (value == "0" || value == "false" || value == "off" || value == "no") {
        return false;
    }
    throw std::invalid_argument(
        "Invalid boolean value for " + flag + ": '" + value
        + "'. Use true/false, 1/0, on/off, or yes/no.");
}

/**
 * @brief Ensure a required flag value is present and non-empty.
 *
 * @param flag  Flag name for diagnostics.
 * @param value Raw value token.
 * @throws std::invalid_argument If @p value is empty.
 */
void requireNonEmpty(const std::string& flag, const std::string& value)
{
    if (value.empty()) {
        throw std::invalid_argument(flag + " must not be empty.");
    }
}

/**
 * @brief Run a Boost.ProgramOptions description over post-verb tokens.
 *
 * Wraps unknown-flag and value-conversion failures into std::invalid_argument
 * so callers see the same error type as the rest of the CLI parser, and defers
 * required-flag enforcement to the caller (which validates each value anyway).
 *
 * @param args Flag tokens after the verb.
 * @param desc Option description for the current verb.
 * @param vm   Destination variable map.
 * @throws std::invalid_argument On any parse or notify failure.
 */
void runVerbParse(
    const std::vector<std::string>& args,
    const po::options_description& desc,
    po::variables_map& vm)
{
    try {
        po::store(
            po::command_line_parser(args).options(desc).run(),
            vm);
        po::notify(vm);
    }
    catch (const po::error& e) {
        throw std::invalid_argument(e.what());
    }
}

} // namespace

app::CreateLinkCommand parseCreateArgs(
    const std::vector<std::string>& args,
    ServerConfig& config)
{
    std::string url;
    std::string slug;
    std::string redirect_type;
    std::string expires_at;
    std::string enabled;
    std::vector<std::string> tags;
    std::vector<std::string> metadata;
    std::string campaign_name;
    std::string campaign_source;
    std::string campaign_medium;
    std::string campaign_term;
    std::string campaign_content;
    std::string campaign_id;
    std::string base_domain;

    po::options_description desc("link create options");
    desc.add_options()(
        "url", po::value<std::string>(&url)->value_name("URL"),
        "Target URL to shorten (required).")(
        "slug", po::value<std::string>(&slug)->value_name("SLUG"),
        "Custom slug for the short link.")(
        "redirect-type",
        po::value<std::string>(&redirect_type)->value_name("TYPE"),
        "Redirect type: temporary or permanent.")(
        "expires-at",
        po::value<std::string>(&expires_at)->value_name("RFC3339"),
        "Expiry timestamp (RFC3339 UTC).")(
        "enabled", po::value<std::string>(&enabled)->value_name("BOOL"),
        "Initial enabled state (true/false).")(
        "tag", po::value<std::vector<std::string>>(&tags)->composing()
                   ->value_name("TAG"),
        "Tag to attach (repeatable).")(
        "metadata",
        po::value<std::vector<std::string>>(&metadata)->composing()
            ->value_name("KEY=VALUE"),
        "Metadata entry as KEY=VALUE (repeatable).")(
        "campaign-name",
        po::value<std::string>(&campaign_name)->value_name("VALUE"),
        "Campaign name.")(
        "campaign-source",
        po::value<std::string>(&campaign_source)->value_name("VALUE"),
        "Campaign source.")(
        "campaign-medium",
        po::value<std::string>(&campaign_medium)->value_name("VALUE"),
        "Campaign medium.")(
        "campaign-term",
        po::value<std::string>(&campaign_term)->value_name("VALUE"),
        "Campaign term.")(
        "campaign-content",
        po::value<std::string>(&campaign_content)->value_name("VALUE"),
        "Campaign content.")(
        "campaign-id",
        po::value<std::string>(&campaign_id)->value_name("VALUE"),
        "Campaign identifier.")(
        "base-domain",
        po::value<std::string>(&base_domain)->value_name("URL"),
        "Base domain used to render the generated short URL.")(
        "allow-private-targets", po::bool_switch(),
        "Permit private/intranet target URLs for this command.");

    po::variables_map vm;
    runVerbParse(args, desc, vm);

    if (!vm.count("url")) {
        throw std::invalid_argument(
            "link create requires --url <URL>.");
    }
    requireNonEmpty("--url", url);

    app::CreateLinkCommand command;
    command.target_url = url;

    if (vm.count("slug")) {
        requireNonEmpty("--slug", slug);
        command.slug = slug;
    }

    if (vm.count("redirect-type")) {
        const auto parsed = url_shortener::parseRedirectType(redirect_type);
        if (!parsed.has_value()) {
            throw std::invalid_argument(
                "Invalid --redirect-type: '" + redirect_type
                + "'. Use temporary or permanent.");
        }
        command.redirect_type = *parsed;
    }

    if (vm.count("expires-at")) {
        requireNonEmpty("--expires-at", expires_at);
        command.expires_at = expires_at;
    }

    if (vm.count("enabled")) {
        command.enabled = parseBoolFlag("--enabled", enabled);
    }

    if (!tags.empty()) {
        command.tags = tags;
    }

    if (!metadata.empty()) {
        for (const auto& entry : metadata) {
            const auto eq = entry.find('=');
            if (eq == std::string::npos || eq == 0) {
                throw std::invalid_argument(
                    "Invalid --metadata entry: '" + entry
                    + "'. Expected KEY=VALUE with a non-empty KEY.");
            }
            command.metadata[entry.substr(0, eq)] = entry.substr(eq + 1);
        }
    }

    if (vm.count("campaign-name") || vm.count("campaign-source")
        || vm.count("campaign-medium") || vm.count("campaign-term")
        || vm.count("campaign-content") || vm.count("campaign-id"))
    {
        Link::Campaign campaign;
        if (vm.count("campaign-name")) {
            campaign.name = campaign_name;
        }
        if (vm.count("campaign-source")) {
            campaign.source = campaign_source;
        }
        if (vm.count("campaign-medium")) {
            campaign.medium = campaign_medium;
        }
        if (vm.count("campaign-term")) {
            campaign.term = campaign_term;
        }
        if (vm.count("campaign-content")) {
            campaign.content = campaign_content;
        }
        if (vm.count("campaign-id")) {
            campaign.id = campaign_id;
        }
        command.campaign = campaign;
    }

    if (vm.count("base-domain")) {
        requireNonEmpty("--base-domain", base_domain);
        config.shortener_base_domain = base_domain;
    }
    if (vm["allow-private-targets"].as<bool>()) {
        config.shortener_allow_private_targets = true;
    }

    return command;
}

app::GetLinkQuery parseGetArgs(const std::vector<std::string>& args)
{
    std::string slug;
    std::string id;

    po::options_description desc("link get options");
    desc.add_options()(
        "slug", po::value<std::string>(&slug)->value_name("SLUG"),
        "Look up the link by slug.")(
        "id", po::value<std::string>(&id)->value_name("ID"),
        "Look up the link by id.");

    po::variables_map vm;
    runVerbParse(args, desc, vm);

    const bool has_slug = vm.count("slug") != 0;
    const bool has_id = vm.count("id") != 0;
    if (has_slug && has_id) {
        throw std::invalid_argument(
            "link get accepts exactly one of --slug or --id, not both.");
    }
    if (!has_slug && !has_id) {
        throw std::invalid_argument(
            "link get requires one of --slug <SLUG> or --id <ID>.");
    }

    app::GetLinkQuery query;
    if (has_slug) {
        requireNonEmpty("--slug", slug);
        query.by = app::GetLinkBy::slug;
        query.value = slug;
    }
    else {
        requireNonEmpty("--id", id);
        query.by = app::GetLinkBy::id;
        query.value = id;
    }
    return query;
}

app::GetLinkStatsQuery parseStatsArgs(const std::vector<std::string>& args)
{
    std::string slug;
    std::string from;
    std::string to;
    std::string bucket;

    po::options_description desc("link stats options");
    desc.add_options()(
        "slug", po::value<std::string>(&slug)->value_name("SLUG"),
        "Slug of the link to report on (required).")(
        "from", po::value<std::string>(&from)->value_name("EPOCH"),
        "Window start as a Unix epoch (required).")(
        "to", po::value<std::string>(&to)->value_name("EPOCH"),
        "Window end as a Unix epoch (required).")(
        "bucket", po::value<std::string>(&bucket)->value_name("BUCKET"),
        "Aggregation granularity: hour, day, or week (required).");

    po::variables_map vm;
    runVerbParse(args, desc, vm);

    if (!vm.count("slug")) {
        throw std::invalid_argument("link stats requires --slug <SLUG>.");
    }
    if (!vm.count("from")) {
        throw std::invalid_argument("link stats requires --from <EPOCH>.");
    }
    if (!vm.count("to")) {
        throw std::invalid_argument("link stats requires --to <EPOCH>.");
    }
    if (!vm.count("bucket")) {
        throw std::invalid_argument("link stats requires --bucket <BUCKET>.");
    }
    requireNonEmpty("--slug", slug);
    requireNonEmpty("--from", from);
    requireNonEmpty("--to", to);
    requireNonEmpty("--bucket", bucket);

    app::GetLinkStatsQuery query;
    query.slug = slug;
    query.from = from;
    query.to = to;
    query.bucket = bucket;
    return query;
}

} // namespace cli
} // namespace url_shortener
