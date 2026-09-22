/**
 * @file 01_cli_parser_link_commands.cpp
 * @brief Unit tests: CliParser recognizes a leading `link <verb>` positional
 * and short-circuits server-flag parsing, leaving server mode unchanged.
 */
#define BOOST_TEST_MODULE CliParserLinkCommands
#include <boost/test/unit_test.hpp>

#include <stdexcept>
#include <string>
#include <variant>
#include <vector>

#include <url_shortener/cli/cli_parser.h>

namespace
{
/// Build a mutable argv from string literals for CliParser::parse().
///
/// Boost.ProgramOptions (and CliParser) take `char* argv[]`, so the backing
/// strings must be non-const and outlive the parse call.
class ArgvBuilder
{
public:
    explicit ArgvBuilder(std::vector<std::string> args)
        : m_storage_(std::move(args))
    {
        m_pointers_.reserve(m_storage_.size());
        for (auto& arg : m_storage_) {
            m_pointers_.push_back(arg.data());
        }
    }

    int argc() const
    {
        return static_cast<int>(m_pointers_.size());
    }

    char** argv()
    {
        return m_pointers_.data();
    }

private:
    std::vector<std::string> m_storage_;
    std::vector<char*> m_pointers_;
};
}  // namespace

/**
 * [Unit][CLI] A leading `link create` positional yields a command with
 * verb == create and leaves the ServerConfig at its default (unused) state.
 *
 * If this breaks, first check:
 *   - CliParser::parse() inspects argv[1] before any ServerConfig parsing.
 *   - parseLinkVerb maps "create" to LinkCliVerb::create.
 */
BOOST_AUTO_TEST_CASE(link_create_sets_command_verb_and_leaves_config_default)
{
    ArgvBuilder args({"url_shortener", "link", "create",
        "--url", "https://example.com/path"});
    CliParser parser;

    const ParseResult result = parser.parse(args.argc(), args.argv());

    BOOST_REQUIRE(result.command.has_value());
    BOOST_CHECK(result.command->verb == LinkCliVerb::create);
    BOOST_CHECK(std::holds_alternative<url_shortener::app::CreateLinkCommand>(
        result.command->payload));
    BOOST_CHECK(!result.help_requested);

    // Server config is left at its defaults (server-flag parsing skipped).
    const ServerConfig defaults;
    BOOST_CHECK_EQUAL(result.config.http_port, defaults.http_port);
    BOOST_CHECK_EQUAL(
        result.config.shortener_base_domain, defaults.shortener_base_domain);
}

/**
 * [Unit][CLI] Every recognized verb maps to the correct enumerator and its
 * matching payload alternative, including the delete/enable/disable aliases.
 */
BOOST_AUTO_TEST_CASE(link_verbs_map_to_expected_enumerators)
{
    {
        ArgvBuilder args({"url_shortener", "link", "get", "--slug", "g"});
        const auto r = CliParser{}.parse(args.argc(), args.argv());
        BOOST_REQUIRE(r.command.has_value());
        BOOST_CHECK(r.command->verb == LinkCliVerb::get);
        BOOST_CHECK(std::holds_alternative<url_shortener::app::GetLinkQuery>(
            r.command->payload));
    }
    {
        ArgvBuilder args({"url_shortener", "link", "delete", "--slug", "d"});
        const auto r = CliParser{}.parse(args.argc(), args.argv());
        BOOST_REQUIRE(r.command.has_value());
        BOOST_CHECK(r.command->verb == LinkCliVerb::del);
        BOOST_CHECK(
            std::holds_alternative<url_shortener::app::DeleteLinkCommand>(
                r.command->payload));
    }
    {
        ArgvBuilder args({"url_shortener", "link", "enable", "--slug", "e"});
        const auto r = CliParser{}.parse(args.argc(), args.argv());
        BOOST_REQUIRE(r.command.has_value());
        BOOST_CHECK(r.command->verb == LinkCliVerb::enable);
        const auto* payload =
            std::get_if<url_shortener::app::SetLinkEnabledCommand>(
                &r.command->payload);
        BOOST_REQUIRE(payload != nullptr);
        BOOST_CHECK(payload->enabled);
    }
    {
        ArgvBuilder args({"url_shortener", "link", "disable", "--slug", "e"});
        const auto r = CliParser{}.parse(args.argc(), args.argv());
        BOOST_REQUIRE(r.command.has_value());
        BOOST_CHECK(r.command->verb == LinkCliVerb::disable);
        const auto* payload =
            std::get_if<url_shortener::app::SetLinkEnabledCommand>(
                &r.command->payload);
        BOOST_REQUIRE(payload != nullptr);
        BOOST_CHECK(!payload->enabled);
    }
    {
        ArgvBuilder args({"url_shortener", "link", "preview", "--slug", "p"});
        const auto r = CliParser{}.parse(args.argc(), args.argv());
        BOOST_REQUIRE(r.command.has_value());
        BOOST_CHECK(r.command->verb == LinkCliVerb::preview);
        BOOST_CHECK(std::holds_alternative<url_shortener::app::GetLinkQuery>(
            r.command->payload));
    }
}

/**
 * [Unit][CLI] With no `link` token, parse() falls through to server-flag
 * parsing exactly as before: command stays nullopt and --http-port is parsed.
 *
 * If this breaks, first check:
 *   - The `link` branch only triggers when argv[1] == "link".
 *   - Existing Boost.ProgramOptions parsing (C8) is unchanged.
 */
BOOST_AUTO_TEST_CASE(server_flags_parse_unchanged_without_link_token)
{
    ArgvBuilder args({"url_shortener", "--http-port", "9090"});
    CliParser parser;

    const ParseResult result = parser.parse(args.argc(), args.argv());

    BOOST_CHECK(!result.command.has_value());
    BOOST_CHECK(!result.help_requested);
    BOOST_CHECK_EQUAL(result.config.http_port, 9090);
}

/**
 * [Unit][CLI] A bare server-flag value equal to "link" (not the first
 * positional) does not trigger command mode.
 */
BOOST_AUTO_TEST_CASE(link_only_recognized_as_first_positional)
{
    ArgvBuilder args({"url_shortener", "--shortener-base-domain", "link"});
    CliParser parser;

    const ParseResult result = parser.parse(args.argc(), args.argv());

    BOOST_CHECK(!result.command.has_value());
    BOOST_CHECK_EQUAL(result.config.shortener_base_domain, "link");
}

/**
 * [Unit][CLI] An unrecognized verb after `link` throws std::invalid_argument,
 * matching the existing "unknown argument" behavior for server flags.
 */
BOOST_AUTO_TEST_CASE(unknown_link_verb_throws_invalid_argument)
{
    ArgvBuilder args({"url_shortener", "link", "frobnicate"});
    CliParser parser;

    BOOST_CHECK_THROW(
        parser.parse(args.argc(), args.argv()), std::invalid_argument);
}

/**
 * [Unit][CLI] A bare `link` with no verb throws std::invalid_argument.
 */
BOOST_AUTO_TEST_CASE(link_without_verb_throws_invalid_argument)
{
    ArgvBuilder args({"url_shortener", "link"});
    CliParser parser;

    BOOST_CHECK_THROW(
        parser.parse(args.argc(), args.argv()), std::invalid_argument);
}

// ---------------------------------------------------------------------------
// link create flag mapping
// ---------------------------------------------------------------------------

namespace
{
/// Extract the CreateLinkCommand payload or fail the test.
const url_shortener::app::CreateLinkCommand& requireCreate(
    const ParseResult& result)
{
    BOOST_REQUIRE(result.command.has_value());
    const auto* payload =
        std::get_if<url_shortener::app::CreateLinkCommand>(
            &result.command->payload);
    BOOST_REQUIRE(payload != nullptr);
    return *payload;
}
}  // namespace

/**
 * [Unit][CLI] `link create --url <u> --slug <s>` maps --url onto target_url and
 * --slug onto the optional slug (the spec's normal case).
 */
BOOST_AUTO_TEST_CASE(link_create_maps_url_and_slug)
{
    ArgvBuilder args({"url_shortener", "link", "create",
        "--url", "https://example.com/x", "--slug", "custom"});
    const ParseResult result = CliParser{}.parse(args.argc(), args.argv());

    const auto& cmd = requireCreate(result);
    BOOST_CHECK_EQUAL(cmd.target_url, "https://example.com/x");
    BOOST_REQUIRE(cmd.slug.has_value());
    BOOST_CHECK_EQUAL(*cmd.slug, "custom");
}

/**
 * [Unit][CLI] `--url` alone leaves slug unset (auto-generated downstream).
 */
BOOST_AUTO_TEST_CASE(link_create_url_only_leaves_slug_unset)
{
    ArgvBuilder args({"url_shortener", "link", "create",
        "--url", "https://example.com/x"});
    const ParseResult result = CliParser{}.parse(args.argc(), args.argv());

    const auto& cmd = requireCreate(result);
    BOOST_CHECK_EQUAL(cmd.target_url, "https://example.com/x");
    BOOST_CHECK(!cmd.slug.has_value());
}

/**
 * [Unit][CLI] `--base-domain` and `--allow-private-targets` are routed onto the
 * ServerConfig the dispatcher consumes, not the DTO (edge case: config-side
 * overrides on a link command).
 */
BOOST_AUTO_TEST_CASE(link_create_routes_config_overrides)
{
    ArgvBuilder args({"url_shortener", "link", "create",
        "--url", "https://example.com/x",
        "--base-domain", "http://sho.rt",
        "--allow-private-targets"});
    const ParseResult result = CliParser{}.parse(args.argc(), args.argv());

    requireCreate(result);
    BOOST_CHECK_EQUAL(result.config.shortener_base_domain, "http://sho.rt");
    BOOST_CHECK(result.config.shortener_allow_private_targets);
}

/**
 * [Unit][CLI] Optional create fields map onto their DTO counterparts:
 * redirect type, expiry, enabled, repeatable tags/metadata, and campaign.
 */
BOOST_AUTO_TEST_CASE(link_create_maps_optional_fields)
{
    ArgvBuilder args({"url_shortener", "link", "create",
        "--url", "https://example.com/x",
        "--redirect-type", "permanent",
        "--expires-at", "2030-01-01T00:00:00Z",
        "--enabled", "false",
        "--tag", "a", "--tag", "b",
        "--metadata", "team=growth",
        "--campaign-name", "summer"});
    const ParseResult result = CliParser{}.parse(args.argc(), args.argv());

    const auto& cmd = requireCreate(result);
    BOOST_REQUIRE(cmd.redirect_type.has_value());
    BOOST_CHECK(*cmd.redirect_type == url_shortener::RedirectType::permanent);
    BOOST_REQUIRE(cmd.expires_at.has_value());
    BOOST_CHECK_EQUAL(*cmd.expires_at, "2030-01-01T00:00:00Z");
    BOOST_REQUIRE(cmd.enabled.has_value());
    BOOST_CHECK(!*cmd.enabled);
    BOOST_REQUIRE_EQUAL(cmd.tags.size(), 2u);
    BOOST_CHECK_EQUAL(cmd.tags[0], "a");
    BOOST_CHECK_EQUAL(cmd.tags[1], "b");
    BOOST_REQUIRE_EQUAL(cmd.metadata.count("team"), 1u);
    BOOST_CHECK_EQUAL(cmd.metadata.at("team"), "growth");
    BOOST_REQUIRE(cmd.campaign.has_value());
    BOOST_REQUIRE(cmd.campaign->name.has_value());
    BOOST_CHECK_EQUAL(*cmd.campaign->name, "summer");
}

/**
 * [Unit][CLI] `link create` without `--url` is a parse error (required flag).
 */
BOOST_AUTO_TEST_CASE(link_create_missing_url_throws)
{
    ArgvBuilder args({"url_shortener", "link", "create", "--slug", "s"});
    BOOST_CHECK_THROW(
        CliParser{}.parse(args.argc(), args.argv()), std::invalid_argument);
}

/**
 * [Unit][CLI] An empty `--url` value is rejected (invalid input).
 */
BOOST_AUTO_TEST_CASE(link_create_empty_url_throws)
{
    ArgvBuilder args({"url_shortener", "link", "create", "--url", ""});
    BOOST_CHECK_THROW(
        CliParser{}.parse(args.argc(), args.argv()), std::invalid_argument);
}

/**
 * [Unit][CLI] An unknown create flag is rejected rather than silently ignored.
 */
BOOST_AUTO_TEST_CASE(link_create_unknown_flag_throws)
{
    ArgvBuilder args({"url_shortener", "link", "create",
        "--url", "https://example.com/x", "--bogus", "1"});
    BOOST_CHECK_THROW(
        CliParser{}.parse(args.argc(), args.argv()), std::invalid_argument);
}

/**
 * [Unit][CLI] An invalid `--redirect-type` value is rejected at parse time.
 */
BOOST_AUTO_TEST_CASE(link_create_invalid_redirect_type_throws)
{
    ArgvBuilder args({"url_shortener", "link", "create",
        "--url", "https://example.com/x", "--redirect-type", "sideways"});
    BOOST_CHECK_THROW(
        CliParser{}.parse(args.argc(), args.argv()), std::invalid_argument);
}

/**
 * [Unit][CLI] A `--metadata` token without `KEY=VALUE` shape is rejected.
 */
BOOST_AUTO_TEST_CASE(link_create_malformed_metadata_throws)
{
    ArgvBuilder args({"url_shortener", "link", "create",
        "--url", "https://example.com/x", "--metadata", "novalue"});
    BOOST_CHECK_THROW(
        CliParser{}.parse(args.argc(), args.argv()), std::invalid_argument);
}

// ---------------------------------------------------------------------------
// link get flag mapping
// ---------------------------------------------------------------------------

/**
 * [Unit][CLI] `link get --slug foo` selects GetLinkBy::slug with value "foo";
 * `--id bar` selects GetLinkBy::id with value "bar".
 */
BOOST_AUTO_TEST_CASE(link_get_selects_slug_or_id)
{
    {
        ArgvBuilder args({"url_shortener", "link", "get", "--slug", "foo"});
        const auto r = CliParser{}.parse(args.argc(), args.argv());
        const auto* q =
            std::get_if<url_shortener::app::GetLinkQuery>(&r.command->payload);
        BOOST_REQUIRE(q != nullptr);
        BOOST_CHECK(q->by == url_shortener::app::GetLinkBy::slug);
        BOOST_CHECK_EQUAL(q->value, "foo");
    }
    {
        ArgvBuilder args({"url_shortener", "link", "get", "--id", "bar"});
        const auto r = CliParser{}.parse(args.argc(), args.argv());
        const auto* q =
            std::get_if<url_shortener::app::GetLinkQuery>(&r.command->payload);
        BOOST_REQUIRE(q != nullptr);
        BOOST_CHECK(q->by == url_shortener::app::GetLinkBy::id);
        BOOST_CHECK_EQUAL(q->value, "bar");
    }
}

/**
 * [Unit][CLI] `link get` with both --slug and --id, or with neither, is a parse
 * error (mutually exclusive, exactly-one selector).
 */
BOOST_AUTO_TEST_CASE(link_get_requires_exactly_one_selector)
{
    {
        ArgvBuilder args({"url_shortener", "link", "get",
            "--slug", "a", "--id", "b"});
        BOOST_CHECK_THROW(CliParser{}.parse(args.argc(), args.argv()),
            std::invalid_argument);
    }
    {
        ArgvBuilder args({"url_shortener", "link", "get"});
        BOOST_CHECK_THROW(CliParser{}.parse(args.argc(), args.argv()),
            std::invalid_argument);
    }
}

// ---------------------------------------------------------------------------
// link update flag mapping (PATCH three-state semantics)
// ---------------------------------------------------------------------------

namespace
{
/// Extract the UpdateLinkCommand payload or fail the test.
///
/// The returned reference points into `result`, so callers must bind the
/// ParseResult to a named local first. Passing a temporary
/// (`requireUpdate(CliParser{}.parse(...))`) leaves the reference dangling:
/// the payload reads back as empty or faults.
const url_shortener::app::UpdateLinkCommand& requireUpdate(
    const ParseResult& result)
{
    BOOST_REQUIRE(result.command.has_value());
    const auto* payload =
        std::get_if<url_shortener::app::UpdateLinkCommand>(
            &result.command->payload);
    BOOST_REQUIRE(payload != nullptr);
    return *payload;
}
}  // namespace

/**
 * [Unit][CLI] `link update --slug foo --enabled false` sets only `enabled`,
 * leaving every three-state optional field at "absent" (nullopt).
 */
BOOST_AUTO_TEST_CASE(link_update_sets_only_enabled)
{
    ArgvBuilder args({"url_shortener", "link", "update",
        "--slug", "foo", "--enabled", "false"});
    const auto result = CliParser{}.parse(args.argc(), args.argv());
    const auto& cmd = requireUpdate(result);

    BOOST_CHECK_EQUAL(cmd.slug, "foo");
    BOOST_REQUIRE(cmd.enabled.has_value());
    BOOST_CHECK(!*cmd.enabled);
    BOOST_CHECK(!cmd.expires_at.has_value());
    BOOST_CHECK(!cmd.tags.has_value());
    BOOST_CHECK(!cmd.metadata.has_value());
    BOOST_CHECK(!cmd.campaign.has_value());
}

/**
 * [Unit][CLI] `--expires-at <value>` is the present-with-value case: the outer
 * optional is engaged and the inner optional holds the timestamp.
 */
BOOST_AUTO_TEST_CASE(link_update_expires_at_value_case)
{
    ArgvBuilder args({"url_shortener", "link", "update",
        "--slug", "foo", "--expires-at", "2030-01-01T00:00:00Z"});
    const auto result = CliParser{}.parse(args.argc(), args.argv());
    const auto& cmd = requireUpdate(result);

    BOOST_REQUIRE(cmd.expires_at.has_value());
    BOOST_REQUIRE(cmd.expires_at->has_value());
    BOOST_CHECK_EQUAL(**cmd.expires_at, "2030-01-01T00:00:00Z");
}

/**
 * [Unit][CLI] `--expires-at clear` is the explicit-null case: the literal value
 * "clear" engages the outer optional but leaves the inner optional empty (clear
 * the expiry), matching the spec's single-flag syntax.
 */
BOOST_AUTO_TEST_CASE(link_update_expires_at_clear_case)
{
    ArgvBuilder args({"url_shortener", "link", "update",
        "--slug", "foo", "--expires-at", "clear"});
    const auto result = CliParser{}.parse(args.argc(), args.argv());
    const auto& cmd = requireUpdate(result);

    BOOST_REQUIRE(cmd.expires_at.has_value());
    BOOST_CHECK(!cmd.expires_at->has_value());
}

/**
 * [Unit][CLI] `--tags a,b` splits one comma-separated value into the present
 * replacement list, and `--tags ""` yields a present, empty list (drop all
 * tags) - the spec's present-but-empty case.
 */
BOOST_AUTO_TEST_CASE(link_update_tags_replace_and_clear)
{
    {
        ArgvBuilder args({"url_shortener", "link", "update",
            "--slug", "foo", "--tags", "a,b"});
        const auto result = CliParser{}.parse(args.argc(), args.argv());
        const auto& cmd = requireUpdate(result);
        BOOST_REQUIRE(cmd.tags.has_value());
        BOOST_REQUIRE_EQUAL(cmd.tags->size(), 2u);
        BOOST_CHECK_EQUAL((*cmd.tags)[0], "a");
        BOOST_CHECK_EQUAL((*cmd.tags)[1], "b");
    }
    {
        ArgvBuilder args({"url_shortener", "link", "update",
            "--slug", "foo", "--tags", ""});
        const auto result = CliParser{}.parse(args.argc(), args.argv());
        const auto& cmd = requireUpdate(result);
        BOOST_REQUIRE(cmd.tags.has_value());
        BOOST_CHECK(cmd.tags->empty());
    }
}

/**
 * [Unit][CLI] `--metadata k=v,k2=v2` splits one comma-separated value into the
 * present replacement map; `--metadata ""` yields a present, empty map; a
 * malformed entry (no `=`) is rejected.
 */
BOOST_AUTO_TEST_CASE(link_update_metadata_replace_clear_and_malformed)
{
    {
        ArgvBuilder args({"url_shortener", "link", "update",
            "--slug", "foo", "--metadata", "team=growth,tier=gold"});
        const auto result = CliParser{}.parse(args.argc(), args.argv());
        const auto& cmd = requireUpdate(result);
        BOOST_REQUIRE(cmd.metadata.has_value());
        BOOST_REQUIRE_EQUAL(cmd.metadata->size(), 2u);
        BOOST_REQUIRE_EQUAL(cmd.metadata->count("team"), 1u);
        BOOST_CHECK_EQUAL(cmd.metadata->at("team"), "growth");
        BOOST_REQUIRE_EQUAL(cmd.metadata->count("tier"), 1u);
        BOOST_CHECK_EQUAL(cmd.metadata->at("tier"), "gold");
    }
    {
        ArgvBuilder args({"url_shortener", "link", "update",
            "--slug", "foo", "--metadata", ""});
        const auto result = CliParser{}.parse(args.argc(), args.argv());
        const auto& cmd = requireUpdate(result);
        BOOST_REQUIRE(cmd.metadata.has_value());
        BOOST_CHECK(cmd.metadata->empty());
    }
    {
        ArgvBuilder args({"url_shortener", "link", "update",
            "--slug", "foo", "--metadata", "novalue"});
        BOOST_CHECK_THROW(CliParser{}.parse(args.argc(), args.argv()),
            std::invalid_argument);
    }
    {
        ArgvBuilder args({"url_shortener", "link", "update",
            "--slug", "foo", "--metadata", "team=growth,novalue"});
        BOOST_CHECK_THROW(CliParser{}.parse(args.argc(), args.argv()),
            std::invalid_argument);
    }
}

/**
 * [Unit][CLI] Campaign three-state: `--campaign-name` sets the value case and
 * `--clear-campaign` sets the explicit-null case.
 */
BOOST_AUTO_TEST_CASE(link_update_campaign_value_and_clear)
{
    {
        ArgvBuilder args({"url_shortener", "link", "update",
            "--slug", "foo", "--campaign-name", "summer"});
        const auto result = CliParser{}.parse(args.argc(), args.argv());
        const auto& cmd = requireUpdate(result);
        BOOST_REQUIRE(cmd.campaign.has_value());
        BOOST_REQUIRE(cmd.campaign->has_value());
        BOOST_REQUIRE((*cmd.campaign)->name.has_value());
        BOOST_CHECK_EQUAL(*(*cmd.campaign)->name, "summer");
    }
    {
        ArgvBuilder args({"url_shortener", "link", "update",
            "--slug", "foo", "--clear-campaign"});
        const auto result = CliParser{}.parse(args.argc(), args.argv());
        const auto& cmd = requireUpdate(result);
        BOOST_REQUIRE(cmd.campaign.has_value());
        BOOST_CHECK(!cmd.campaign->has_value());
    }
    {
        ArgvBuilder args({"url_shortener", "link", "update",
            "--slug", "foo", "--campaign-name", "summer", "--clear-campaign"});
        BOOST_CHECK_THROW(CliParser{}.parse(args.argc(), args.argv()),
            std::invalid_argument);
    }
}

/**
 * [Unit][CLI] `link update` without `--slug` is a parse error, and an unknown
 * flag is rejected rather than silently ignored.
 */
BOOST_AUTO_TEST_CASE(link_update_missing_slug_and_unknown_flag_throw)
{
    {
        ArgvBuilder args({"url_shortener", "link", "update",
            "--enabled", "true"});
        BOOST_CHECK_THROW(CliParser{}.parse(args.argc(), args.argv()),
            std::invalid_argument);
    }
    {
        ArgvBuilder args({"url_shortener", "link", "update",
            "--slug", "foo", "--bogus", "1"});
        BOOST_CHECK_THROW(CliParser{}.parse(args.argc(), args.argv()),
            std::invalid_argument);
    }
}

// ---------------------------------------------------------------------------
// link delete / enable / disable / restore flag mapping
// ---------------------------------------------------------------------------

/**
 * [Unit][CLI] `link delete --slug foo` yields DeleteLinkCommand{slug=foo}.
 */
BOOST_AUTO_TEST_CASE(link_delete_maps_slug)
{
    ArgvBuilder args({"url_shortener", "link", "delete", "--slug", "foo"});
    const auto r = CliParser{}.parse(args.argc(), args.argv());
    const auto* cmd =
        std::get_if<url_shortener::app::DeleteLinkCommand>(&r.command->payload);
    BOOST_REQUIRE(cmd != nullptr);
    BOOST_CHECK_EQUAL(cmd->slug, "foo");
}

/**
 * [Unit][CLI] `link enable` sets enabled=true and `link disable` sets
 * enabled=false on SetLinkEnabledCommand; both carry the slug.
 */
BOOST_AUTO_TEST_CASE(link_enable_disable_map_slug_and_state)
{
    {
        ArgvBuilder args({"url_shortener", "link", "enable", "--slug", "foo"});
        const auto r = CliParser{}.parse(args.argc(), args.argv());
        const auto* cmd = std::get_if<
            url_shortener::app::SetLinkEnabledCommand>(&r.command->payload);
        BOOST_REQUIRE(cmd != nullptr);
        BOOST_CHECK_EQUAL(cmd->slug, "foo");
        BOOST_CHECK(cmd->enabled);
    }
    {
        ArgvBuilder args({"url_shortener", "link", "disable", "--slug", "foo"});
        const auto r = CliParser{}.parse(args.argc(), args.argv());
        const auto* cmd = std::get_if<
            url_shortener::app::SetLinkEnabledCommand>(&r.command->payload);
        BOOST_REQUIRE(cmd != nullptr);
        BOOST_CHECK_EQUAL(cmd->slug, "foo");
        BOOST_CHECK(!cmd->enabled);
    }
}

/**
 * [Unit][CLI] `link restore --slug foo` yields RestoreLinkCommand{slug=foo}.
 */
BOOST_AUTO_TEST_CASE(link_restore_maps_slug)
{
    ArgvBuilder args({"url_shortener", "link", "restore", "--slug", "foo"});
    const auto r = CliParser{}.parse(args.argc(), args.argv());
    const auto* cmd =
        std::get_if<url_shortener::app::RestoreLinkCommand>(&r.command->payload);
    BOOST_REQUIRE(cmd != nullptr);
    BOOST_CHECK_EQUAL(cmd->slug, "foo");
}

/**
 * [Unit][CLI] delete/enable/disable/restore all require `--slug`; omitting it is
 * a parse error for each.
 */
BOOST_AUTO_TEST_CASE(link_lifecycle_verbs_require_slug)
{
    for (const char* verb : {"delete", "enable", "disable", "restore"}) {
        ArgvBuilder args({"url_shortener", "link", verb});
        BOOST_CHECK_THROW(CliParser{}.parse(args.argc(), args.argv()),
            std::invalid_argument);
    }
}

// ---------------------------------------------------------------------------
// link preview flag mapping
// ---------------------------------------------------------------------------

/**
 * [Unit][CLI] `link preview` reuses the get lookup DTO: `--slug` selects slug,
 * `--id` selects id, and neither/both is a parse error.
 */
BOOST_AUTO_TEST_CASE(link_preview_selects_slug_or_id)
{
    {
        ArgvBuilder args({"url_shortener", "link", "preview", "--slug", "foo"});
        const auto r = CliParser{}.parse(args.argc(), args.argv());
        const auto* q =
            std::get_if<url_shortener::app::GetLinkQuery>(&r.command->payload);
        BOOST_REQUIRE(q != nullptr);
        BOOST_CHECK(q->by == url_shortener::app::GetLinkBy::slug);
        BOOST_CHECK_EQUAL(q->value, "foo");
    }
    {
        ArgvBuilder args({"url_shortener", "link", "preview", "--id", "bar"});
        const auto r = CliParser{}.parse(args.argc(), args.argv());
        const auto* q =
            std::get_if<url_shortener::app::GetLinkQuery>(&r.command->payload);
        BOOST_REQUIRE(q != nullptr);
        BOOST_CHECK(q->by == url_shortener::app::GetLinkBy::id);
        BOOST_CHECK_EQUAL(q->value, "bar");
    }
    {
        ArgvBuilder args({"url_shortener", "link", "preview"});
        BOOST_CHECK_THROW(CliParser{}.parse(args.argc(), args.argv()),
            std::invalid_argument);
    }
}

// ---------------------------------------------------------------------------
// link stats flag mapping
// ---------------------------------------------------------------------------

/**
 * [Unit][CLI] `link stats --slug --from --to --bucket` maps every field
 * verbatim into GetLinkStatsQuery (timestamp/bucket validation is deferred).
 */
BOOST_AUTO_TEST_CASE(link_stats_maps_fields_verbatim)
{
    ArgvBuilder args({"url_shortener", "link", "stats",
        "--slug", "foo", "--from", "100", "--to", "200", "--bucket", "day"});
    const auto r = CliParser{}.parse(args.argc(), args.argv());
    const auto* q =
        std::get_if<url_shortener::app::GetLinkStatsQuery>(&r.command->payload);
    BOOST_REQUIRE(q != nullptr);
    BOOST_CHECK_EQUAL(q->slug, "foo");
    BOOST_CHECK_EQUAL(q->from, "100");
    BOOST_CHECK_EQUAL(q->to, "200");
    BOOST_CHECK_EQUAL(q->bucket, "day");
}

/**
 * [Unit][CLI] `link stats` missing any required flag is a parse error.
 */
BOOST_AUTO_TEST_CASE(link_stats_missing_required_flag_throws)
{
    ArgvBuilder args({"url_shortener", "link", "stats",
        "--slug", "foo", "--from", "100", "--to", "200"});
    BOOST_CHECK_THROW(
        CliParser{}.parse(args.argc(), args.argv()), std::invalid_argument);
}

/**
 * [Unit][CLI] Regression guard: the exact argv shapes the integration/e2e CLI
 * suites invoke parse without error. Pins the flag spellings (`--url`,
 * `--slug`, `--base-domain`, `--id`, `--from`/`--to`/`--bucket`) so a rename
 * here is caught before those slower suites run.
 */
BOOST_AUTO_TEST_CASE(link_commands_accept_documented_argv_shapes)
{
    {
        ArgvBuilder args({"url_shortener", "link", "create",
            "--url", "https://e2e-create.example.com",
            "--slug", "e2e-create-slug",
            "--base-domain", "http://sho.rt"});
        BOOST_CHECK_NO_THROW(CliParser{}.parse(args.argc(), args.argv()));
    }
    {
        ArgvBuilder args({"url_shortener", "link", "get",
            "--slug", "e2e-get-slug"});
        BOOST_CHECK_NO_THROW(CliParser{}.parse(args.argc(), args.argv()));
    }
    {
        ArgvBuilder args({"url_shortener", "link", "get",
            "--id", "some-id"});
        BOOST_CHECK_NO_THROW(CliParser{}.parse(args.argc(), args.argv()));
    }
    {
        ArgvBuilder args({"url_shortener", "link", "stats",
            "--slug", "stats-fresh",
            "--from", "1700000000", "--to", "1800000000", "--bucket", "day"});
        BOOST_CHECK_NO_THROW(CliParser{}.parse(args.argc(), args.argv()));
    }
}

/**
 * [Unit][CLI] Regression: server-flag parsing (a broad, representative mix) is
 * unchanged by the additive `link` path - values still land in ServerConfig
 * and no command is produced (contract C8).
 */
BOOST_AUTO_TEST_CASE(server_flags_unchanged_by_link_additions)
{
    ArgvBuilder args({"url_shortener",
        "--http-port", "9091",
        "--tls-enabled", "true",
        "--https-port", "8443",
        "--shortener-base-domain", "http://cfg.example",
        "--analytics-enabled", "false",
        "--max-request-body-bytes", "4096"});
    const ParseResult result = CliParser{}.parse(args.argc(), args.argv());

    BOOST_CHECK(!result.command.has_value());
    BOOST_CHECK(!result.help_requested);
    BOOST_CHECK_EQUAL(result.config.http_port, 9091);
    BOOST_CHECK(result.config.tls.enabled);
    BOOST_CHECK_EQUAL(result.config.tls.port, 8443);
    BOOST_CHECK_EQUAL(
        result.config.shortener_base_domain, "http://cfg.example");
    BOOST_CHECK(!result.config.analytics_enabled);
    BOOST_CHECK_EQUAL(result.config.max_request_body_bytes, 4096u);
}

// ---------------------------------------------------------------------------
// link help text (subtask 04)
// ---------------------------------------------------------------------------

namespace
{
/// Golden copy of the top-level (server-mode) `--help` output, captured before
/// the link-scoped help branch was added. Kept as an exact raw-string literal
/// so any accidental edit to the server `--help` path (contract C8) is caught
/// by a byte-for-byte comparison rather than a lenient substring check.
///
/// Regenerate only if the *server* option surface intentionally changes:
///   ./cmake-build/url_shortener --help
const char* const kTopLevelHelpGolden =
    R"HELPGOLD(Usage: simple-http [OPTIONS] [PORT]


General options:
  -h [ --help ]                         Print this help message and exit.

HTTP options:
  --http-port PORT                      HTTP listening port (default: 8000).
  --http-enabled BOOL                   Enable HTTP listener (true/false, 
                                        default: true).
  --http-redirect-to-https BOOL         Redirect all HTTP requests to HTTPS 
                                        (true/false, default: false).
  --hsts-max-age SECONDS                HSTS max-age in seconds (optional; omit
                                        to disable HSTS header).

TLS/HTTPS options:
  --tls-enabled BOOL                    Enable TLS/HTTPS listener (true/false, 
                                        default: false).
  --https-port PORT                     HTTPS listening port (default: 443).
  --tls-cert PATH                       Path to PEM certificate chain file.
  --tls-key PATH                        Path to PEM private key file.
  --tls-key-passphrase PASSPHRASE       Private key passphrase (optional).
  --tls-ca-file PATH                    Path to CA certificate file (optional).
  --tls-ca-path PATH                    Path to CA certificate directory 
                                        (optional).
  --tls-min-version VERSION (=TLS1.2)   Minimum TLS version (default: TLS1.2).
  --tls-cipher-suites SUITES            TLS 1.3 cipher suites 
                                        (colon-separated).
  --tls-ciphers CIPHERS                 TLS 1.2 cipher list (OpenSSL format).
  --tls-curves CURVES (=X25519:P-256)   Elliptic curves (default: 
                                        X25519:P-256).
  --tls-alpn ALPN (=http/1.1)           ALPN protocol (default: http/1.1).
  --tls-session-tickets BOOL            Enable TLS session tickets (true/false,
                                        default: false).
  --tls-session-cache BOOL              Enable TLS session cache (true/false, 
                                        default: true).
  --tls-client-auth MODE                Client authentication mode: none, 
                                        optional, or required (default: none).

URL shortener options:
  --shortener-base-domain URL           Base domain used in generated short 
                                        URLs (default: http://localhost:8000; 
                                        overridable via SHORTENER_BASE_DOMAIN 
                                        env var).
  --shortener-default-redirect-type TYPE (=temporary)
                                        Default redirect type: temporary or 
                                        permanent (default: temporary).
  --shortener-default-expiry-seconds SECONDS
                                        Default expiration window in seconds 
                                        for new links when expires_at is 
                                        omitted.
  --shortener-generated-slug-length N (=7)
                                        Length of auto-generated slugs 
                                        (default: 7).
  --shortener-allow-private-targets BOOL
                                        Allow private/intranet target URLs 
                                        (true/false, default: false).

Analytics options:
  --analytics-enabled BOOL              Enable analytics collection 
                                        (true/false, default: true).
  --analytics-queue-capacity N (=1024)  In-memory analytics queue capacity 
                                        (default: 1024).
  --analytics-client-hash-salt SALT (=dev-analytics-salt)
                                        HMAC salt for client ID hashing 
                                        (default: dev-analytics-salt).

Request limits:
  --request-id-max-length N (=64)       Maximum accepted X-Request-Id header 
                                        length (default: 64).
  --max-request-body-bytes BYTES (=65536)
                                        Maximum request body size in bytes 
                                        (default: 65536).
  --max-request-target-length N (=2048) Maximum request target (URL path) 
                                        length (default: 2048).
)HELPGOLD";
}  // namespace

/**
 * [Unit][CLI] Regression guard: top-level `--help` (no `link` token) still sets
 * help_requested and emits help_text byte-identical to the pre-subtask-04
 * baseline. Exact-string, not substring, so an accidental edit to the server
 * help path is caught immediately (contract C8, subtask 04 constraint).
 *
 * If this breaks, first check:
 *   - The `link` help branch only fires when argv[1] == "link".
 *   - No server option was added/renamed/reworded (that regenerates the
 *     golden; update kTopLevelHelpGolden deliberately if so).
 */
BOOST_AUTO_TEST_CASE(top_level_help_is_byte_identical_to_baseline)
{
    ArgvBuilder args({"url_shortener", "--help"});
    const ParseResult result = CliParser{}.parse(args.argc(), args.argv());

    BOOST_CHECK(result.help_requested);
    BOOST_CHECK(!result.command.has_value());
    BOOST_CHECK_EQUAL(result.help_text, std::string(kTopLevelHelpGolden));
}

/**
 * [Unit][CLI] `link --help` (no verb) requests help and lists every verb, plus
 * the `link <verb> --help` drill-down hint - and does NOT parse a command.
 */
BOOST_AUTO_TEST_CASE(link_help_lists_all_verbs)
{
    ArgvBuilder args({"url_shortener", "link", "--help"});
    const ParseResult result = CliParser{}.parse(args.argc(), args.argv());

    BOOST_CHECK(result.help_requested);
    BOOST_CHECK(!result.command.has_value());

    const std::string& help = result.help_text;
    for (const char* verb : {"create", "get", "update", "delete", "enable",
             "disable", "restore", "preview", "stats"}) {
        BOOST_CHECK_MESSAGE(help.find(verb) != std::string::npos,
            "link --help must mention verb: " << verb);
    }
    // The tenth "verb" is the `link` group itself, surfaced via the drill-down
    // hint that points at per-verb help.
    BOOST_CHECK(help.find("link <verb> --help") != std::string::npos);
}

/**
 * [Unit][CLI] `link create --help` requests help and describes create's own
 * flags (required --url plus the optional set from subtask 02); it does not
 * parse a create command nor require --url.
 */
BOOST_AUTO_TEST_CASE(link_create_help_describes_create_flags)
{
    ArgvBuilder args({"url_shortener", "link", "create", "--help"});
    const ParseResult result = CliParser{}.parse(args.argc(), args.argv());

    BOOST_CHECK(result.help_requested);
    BOOST_CHECK(!result.command.has_value());

    const std::string& help = result.help_text;
    for (const char* flag : {"--url", "--slug", "--redirect-type",
             "--expires-at", "--enabled", "--tag", "--metadata",
             "--campaign-name", "--base-domain", "--allow-private-targets"}) {
        BOOST_CHECK_MESSAGE(help.find(flag) != std::string::npos,
            "link create --help must describe flag: " << flag);
    }
    // Verb-scoped: it must not bleed in an unrelated verb's unique flag.
    BOOST_CHECK(help.find("--bucket") == std::string::npos);
}
