/**
 * @file 09_link_args_json.cpp
 * @brief Unit tests for the `link <verb>` DTO -> `args` JSON converters and the
 * ArgSpec coverage guard.
 *
 * Three groups:
 *   1. Converter shape tests: each converter emits the exact `args` object text
 *      (insertion order, PATCH tri-state, omitted/empty rules) and hostile
 *      strings round-trip through the sanctioned scanner.
 *   2. ArgSpec coverage: against the *real* registered `link.*` descriptors, a
 *      maximal DTO's converter output passes `validate_args`, and the set of
 *      keys the converters can emit equals the descriptor's declared arg names
 *      (no arg is unreachable, no converter emits an unknown arg). A descriptor
 *      that gains an arg without a flag/converter makes this fail.
 *   3. End-to-end: a real argv per verb through `CliParser::parse`, then the
 *      converter, compared against the expected JSON text.
 */
#define BOOST_TEST_MODULE LinkArgsJson
#include <boost/test/unit_test.hpp>

#include <set>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include <url_shortener/app/link_command_service.hpp>
#include <url_shortener/cli/cli_parser.hpp>
#include <url_shortener/cli/link_args_json.hpp>
#include <url_shortener/commands/arg_spec.hpp>
#include <url_shortener/commands/command_registry.hpp>
#include <url_shortener/commands/json_scan.hpp>
#include <url_shortener/commands/link_commands.hpp>

#include "../app/app_test_fakes.hpp"

namespace
{
using url_shortener::app::CreateLinkCommand;
using url_shortener::app::DeleteLinkCommand;
using url_shortener::app::GetLinkBy;
using url_shortener::app::GetLinkQuery;
using url_shortener::app::GetLinkStatsQuery;
using url_shortener::app::RestoreLinkCommand;
using url_shortener::app::SetLinkEnabledCommand;
using url_shortener::app::UpdateLinkCommand;
using url_shortener::cli::command_name_for;
using url_shortener::cli::create_args_json;
using url_shortener::cli::get_args_json;
using url_shortener::cli::slug_args_json;
using url_shortener::cli::stats_args_json;
using url_shortener::cli::update_args_json;

/// Build a mutable argv from string literals for CliParser::parse().
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

    int argc() const { return static_cast<int>(m_pointers_.size()); }
    char** argv() { return m_pointers_.data(); }

private:
    std::vector<std::string> m_storage_;
    std::vector<char*> m_pointers_;
};

/// The set of top-level member keys the scanner finds in @p args_json.
std::set<std::string> emitted_keys(const std::string& args_json)
{
    const url_shortener::commands::scan_result scanned =
        url_shortener::commands::scan_object(args_json);
    BOOST_REQUIRE_MESSAGE(scanned.ok,
                          "args not a valid JSON object: " + args_json);
    std::set<std::string> keys;
    for (const auto& member : scanned.members) {
        keys.insert(member.key);
    }
    return keys;
}

/// Route a parsed `link <verb>` command to the matching converter, mirroring
/// what subtask 15 will wire into the dispatcher.
std::string args_json_for(const LinkCliCommand& command)
{
    switch (command.verb) {
    case LinkCliVerb::create:
        return create_args_json(std::get<CreateLinkCommand>(command.payload));
    case LinkCliVerb::get:
    case LinkCliVerb::preview:
        return get_args_json(std::get<GetLinkQuery>(command.payload));
    case LinkCliVerb::update:
        return update_args_json(std::get<UpdateLinkCommand>(command.payload));
    case LinkCliVerb::del:
        return slug_args_json(
            std::get<DeleteLinkCommand>(command.payload).slug);
    case LinkCliVerb::enable:
    case LinkCliVerb::disable:
        return slug_args_json(
            std::get<SetLinkEnabledCommand>(command.payload).slug);
    case LinkCliVerb::restore:
        return slug_args_json(
            std::get<RestoreLinkCommand>(command.payload).slug);
    case LinkCliVerb::stats:
        return stats_args_json(std::get<GetLinkStatsQuery>(command.payload));
    }
    return {};
}

/// A maximal `create` DTO that engages every optional field (eight args).
CreateLinkCommand maximal_create()
{
    CreateLinkCommand command;
    command.target_url = "https://example.com/a";
    command.slug = "abc";
    command.redirect_type = url_shortener::RedirectType::permanent;
    command.expires_at = "2025-12-31T23:59:59Z";
    command.enabled = false;
    command.tags = {"t1", "t2"};
    command.metadata = {{"k1", "v1"}, {"k2", "v2"}};
    url_shortener::Link::Campaign campaign;
    campaign.name = "n";
    campaign.source = "s";
    campaign.medium = "m";
    campaign.term = "t";
    campaign.content = "c";
    campaign.id = "i";
    command.campaign = campaign;
    return command;
}

/// A maximal `update` DTO engaging every field with a value (six args).
UpdateLinkCommand maximal_update()
{
    UpdateLinkCommand command;
    command.slug = "abc";
    command.enabled = true;
    command.expires_at = std::optional<std::string>("2025-12-31T23:59:59Z");
    command.tags = std::vector<std::string>{"t1", "t2"};
    command.metadata =
        std::unordered_map<std::string, std::string>{{"k1", "v1"}};
    url_shortener::Link::Campaign campaign;
    campaign.name = "n";
    command.campaign = std::optional<url_shortener::Link::Campaign>(campaign);
    return command;
}

}  // namespace

// --------------------------------------------------------------------------
//  Group 1: converter shape tests
// --------------------------------------------------------------------------

/**
 * [Unit][CLI] create_args_json emits only target_url when nothing else is set.
 *
 * On failure first check: args_builder insertion order; that empty
 * tags/metadata and absent optionals are omitted.
 */
BOOST_AUTO_TEST_CASE(create_minimal_emits_only_target_url)
{
    CreateLinkCommand command;
    command.target_url = "https://example.com";
    BOOST_CHECK_EQUAL(create_args_json(command),
                      R"({"target_url":"https://example.com"})");
}

/**
 * [Unit][CLI] create_args_json emits every field in insertion order, with the
 * campaign carrying only its present members.
 */
BOOST_AUTO_TEST_CASE(create_full_emits_all_fields_in_order)
{
    CreateLinkCommand command;
    command.target_url = "https://e.com";
    command.slug = "s";
    command.redirect_type = url_shortener::RedirectType::permanent;
    command.expires_at = "2025-01-01T00:00:00Z";
    command.enabled = false;
    command.tags = {"a", "b"};
    command.metadata = {{"k1", "v1"}, {"k2", "v2"}};
    url_shortener::Link::Campaign campaign;
    campaign.name = "c";
    command.campaign = campaign;

    BOOST_CHECK_EQUAL(
        create_args_json(command),
        R"({"target_url":"https://e.com","slug":"s",)"
        R"("redirect_type":"permanent","expires_at":"2025-01-01T00:00:00Z",)"
        R"("enabled":false,"tags":["a","b"],)"
        R"("metadata":{"k1":"v1","k2":"v2"},"campaign":{"name":"c"}})");
}

/**
 * [Unit][CLI] create_args_json emits all six campaign members in field order.
 */
BOOST_AUTO_TEST_CASE(create_campaign_emits_all_six_members)
{
    CreateLinkCommand command;
    command.target_url = "https://e.com";
    url_shortener::Link::Campaign campaign;
    campaign.name = "n";
    campaign.source = "s";
    campaign.medium = "m";
    campaign.term = "t";
    campaign.content = "c";
    campaign.id = "i";
    command.campaign = campaign;

    BOOST_CHECK_EQUAL(
        create_args_json(command),
        R"({"target_url":"https://e.com","campaign":{"name":"n",)"
        R"("source":"s","medium":"m","term":"t","content":"c","id":"i"}})");
}

/**
 * [Unit][CLI] create_args_json maps RedirectType::temporary to "temporary".
 */
BOOST_AUTO_TEST_CASE(create_redirect_type_temporary)
{
    CreateLinkCommand command;
    command.target_url = "https://e.com";
    command.redirect_type = url_shortener::RedirectType::temporary;
    BOOST_CHECK_EQUAL(
        create_args_json(command),
        R"({"target_url":"https://e.com","redirect_type":"temporary"})");
}

/**
 * [Unit][CLI] get_args_json emits {"slug":..} or {"id":..} by discriminator.
 */
BOOST_AUTO_TEST_CASE(get_by_slug_and_by_id)
{
    GetLinkQuery by_slug;
    by_slug.by = GetLinkBy::slug;
    by_slug.value = "x";
    BOOST_CHECK_EQUAL(get_args_json(by_slug), R"({"slug":"x"})");

    GetLinkQuery by_id;
    by_id.by = GetLinkBy::id;
    by_id.value = "y";
    BOOST_CHECK_EQUAL(get_args_json(by_id), R"({"id":"y"})");
}

/**
 * [Unit][CLI] update_args_json leaves absent fields out (slug only).
 */
BOOST_AUTO_TEST_CASE(update_absent_fields_emit_slug_only)
{
    UpdateLinkCommand command;
    command.slug = "s";
    BOOST_CHECK_EQUAL(update_args_json(command), R"({"slug":"s"})");
}

/**
 * [Unit][CLI] update_args_json emits each tri-state field correctly: a cleared
 * expires_at/campaign becomes JSON null, a value becomes a string/object, and
 * a present-but-empty tags/metadata becomes []/{}.
 */
BOOST_AUTO_TEST_CASE(update_tri_state_fields)
{
    // expires_at cleared -> null
    UpdateLinkCommand cleared_expiry;
    cleared_expiry.slug = "s";
    cleared_expiry.expires_at = std::optional<std::string>(std::nullopt);
    BOOST_CHECK_EQUAL(update_args_json(cleared_expiry),
                      R"({"slug":"s","expires_at":null})");

    // expires_at value -> string
    UpdateLinkCommand value_expiry;
    value_expiry.slug = "s";
    value_expiry.expires_at =
        std::optional<std::string>("2025-01-01T00:00:00Z");
    BOOST_CHECK_EQUAL(update_args_json(value_expiry),
                      R"({"slug":"s","expires_at":"2025-01-01T00:00:00Z"})");

    // campaign cleared -> null
    UpdateLinkCommand cleared_campaign;
    cleared_campaign.slug = "s";
    cleared_campaign.campaign =
        std::optional<url_shortener::Link::Campaign>(std::nullopt);
    BOOST_CHECK_EQUAL(update_args_json(cleared_campaign),
                      R"({"slug":"s","campaign":null})");

    // campaign value -> object with present members
    UpdateLinkCommand value_campaign;
    value_campaign.slug = "s";
    url_shortener::Link::Campaign campaign;
    campaign.source = "ads";
    value_campaign.campaign =
        std::optional<url_shortener::Link::Campaign>(campaign);
    BOOST_CHECK_EQUAL(update_args_json(value_campaign),
                      R"({"slug":"s","campaign":{"source":"ads"}})");

    // tags present empty -> []
    UpdateLinkCommand empty_tags;
    empty_tags.slug = "s";
    empty_tags.tags = std::vector<std::string>{};
    BOOST_CHECK_EQUAL(update_args_json(empty_tags),
                      R"({"slug":"s","tags":[]})");

    // metadata present empty -> {}
    UpdateLinkCommand empty_metadata;
    empty_metadata.slug = "s";
    empty_metadata.metadata =
        std::unordered_map<std::string, std::string>{};
    BOOST_CHECK_EQUAL(update_args_json(empty_metadata),
                      R"({"slug":"s","metadata":{}})");

    // enabled present -> bool
    UpdateLinkCommand enabled_only;
    enabled_only.slug = "s";
    enabled_only.enabled = true;
    BOOST_CHECK_EQUAL(update_args_json(enabled_only),
                      R"({"slug":"s","enabled":true})");
}

/**
 * [Unit][CLI] slug_args_json emits a single escaped slug member.
 */
BOOST_AUTO_TEST_CASE(slug_args_json_emits_slug)
{
    BOOST_CHECK_EQUAL(slug_args_json("abc"), R"({"slug":"abc"})");
}

/**
 * [Unit][CLI] stats_args_json emits slug, from, to, bucket as strings.
 */
BOOST_AUTO_TEST_CASE(stats_args_json_emits_four_strings)
{
    GetLinkStatsQuery query;
    query.slug = "s";
    query.from = "1000";
    query.to = "2000";
    query.bucket = "hour";
    BOOST_CHECK_EQUAL(
        stats_args_json(query),
        R"({"slug":"s","from":"1000","to":"2000","bucket":"hour"})");
}

/**
 * [Unit][CLI] Hostile strings (quotes, newline, UTF-8) survive a round-trip
 * through the converter and the sanctioned scanner/decoder.
 *
 * On failure first check: args_builder escaping path; decode_json_string on the
 * scanned raw token.
 */
BOOST_AUTO_TEST_CASE(hostile_strings_round_trip_through_scanner)
{
    const std::string hostile =
        "a\"b\\c\nd\t\xC3\xA9 end";  // quote, backslash, newline, tab, UTF-8

    CreateLinkCommand command;
    command.target_url = hostile;
    command.slug = hostile;

    const std::string json = create_args_json(command);
    const url_shortener::commands::scan_result scanned =
        url_shortener::commands::scan_object(json);
    BOOST_REQUIRE(scanned.ok);

    const url_shortener::commands::json_member* target =
        url_shortener::commands::find_member(scanned.members, "target_url");
    BOOST_REQUIRE(target != nullptr);
    const auto decoded =
        url_shortener::commands::decode_json_string(target->raw);
    BOOST_REQUIRE(decoded.has_value());
    BOOST_CHECK_EQUAL(*decoded, hostile);
}

/**
 * [Unit][CLI] command_name_for maps every verb to its dotted registry name.
 */
BOOST_AUTO_TEST_CASE(command_name_for_all_verbs)
{
    BOOST_CHECK_EQUAL(command_name_for(LinkCliVerb::create), "link.create");
    BOOST_CHECK_EQUAL(command_name_for(LinkCliVerb::get), "link.get");
    BOOST_CHECK_EQUAL(command_name_for(LinkCliVerb::update), "link.update");
    BOOST_CHECK_EQUAL(command_name_for(LinkCliVerb::del), "link.delete");
    BOOST_CHECK_EQUAL(command_name_for(LinkCliVerb::enable), "link.enable");
    BOOST_CHECK_EQUAL(command_name_for(LinkCliVerb::disable), "link.disable");
    BOOST_CHECK_EQUAL(command_name_for(LinkCliVerb::restore), "link.restore");
    BOOST_CHECK_EQUAL(command_name_for(LinkCliVerb::preview), "link.preview");
    BOOST_CHECK_EQUAL(command_name_for(LinkCliVerb::stats), "link.stats");
}

// --------------------------------------------------------------------------
//  Group 2: ArgSpec coverage against the real registered descriptors
// --------------------------------------------------------------------------

namespace
{
/// Owns a registry with the nine link commands registered over fakes, so the
/// coverage tests run against the exact ArgSpecs production registers.
struct RegistryFixture
{
    RegistryFixture()
        : config(test_fakes::makeTestConfig()),
          service(store, stats_reader, config)
    {
        url_shortener::commands::register_link_commands(registry, service);
    }

    /// The declared arg names of the descriptor for @p command_name.
    std::set<std::string> arg_names(std::string_view command_name) const
    {
        const auto* descriptor = registry.find(command_name);
        BOOST_REQUIRE_MESSAGE(descriptor != nullptr,
                              std::string(command_name) + " not registered");
        std::set<std::string> names;
        for (const auto& spec : descriptor->args) {
            names.insert(spec.name);
        }
        return names;
    }

    /// Assert @p args_json validates against the named descriptor's ArgSpecs.
    void check_validates(std::string_view command_name,
                         const std::string& args_json) const
    {
        const auto* descriptor = registry.find(command_name);
        BOOST_REQUIRE(descriptor != nullptr);
        url_shortener::commands::CommandError error;
        const bool ok = url_shortener::commands::validate_args(
            descriptor->args, args_json, nullptr, &error);
        BOOST_CHECK_MESSAGE(ok,
                            std::string(command_name) + " validate_args: " +
                                error.message + " for " + args_json);
    }

    test_fakes::FakeLinkStore store;
    test_fakes::FakeLinkStatsReader stats_reader;
    ServerConfig config;
    url_shortener::app::LinkCommandService service;
    url_shortener::commands::CommandRegistry registry;
};
}  // namespace

/**
 * [Unit][CLI] create: a maximal DTO's output validates and its emitted key set
 * equals the descriptor's eight declared arg names (every arg reachable, no
 * unknown arg). Adding an arg without a flag would break this equality.
 */
BOOST_FIXTURE_TEST_CASE(coverage_create, RegistryFixture)
{
    const std::string json = create_args_json(maximal_create());
    check_validates("link.create", json);
    BOOST_CHECK(emitted_keys(json) == arg_names("link.create"));
}

/**
 * [Unit][CLI] update: a maximal DTO's output validates and its emitted key set
 * equals the descriptor's six declared arg names.
 */
BOOST_FIXTURE_TEST_CASE(coverage_update, RegistryFixture)
{
    const std::string json = update_args_json(maximal_update());
    check_validates("link.update", json);
    BOOST_CHECK(emitted_keys(json) == arg_names("link.update"));
}

/**
 * [Unit][CLI] get/preview: each variant validates, and the union of the slug
 * and id variants equals the descriptor's {slug, id} arg names for both verbs.
 */
BOOST_FIXTURE_TEST_CASE(coverage_get_and_preview, RegistryFixture)
{
    GetLinkQuery by_slug;
    by_slug.by = GetLinkBy::slug;
    by_slug.value = "abc";
    GetLinkQuery by_id;
    by_id.by = GetLinkBy::id;
    by_id.value = "xyz";

    const std::string slug_json = get_args_json(by_slug);
    const std::string id_json = get_args_json(by_id);

    for (const char* name : {"link.get", "link.preview"}) {
        check_validates(name, slug_json);
        check_validates(name, id_json);
        std::set<std::string> union_keys = emitted_keys(slug_json);
        for (const auto& key : emitted_keys(id_json)) {
            union_keys.insert(key);
        }
        BOOST_CHECK(union_keys == arg_names(name));
    }
}

/**
 * [Unit][CLI] stats: a full DTO's output validates and its emitted key set
 * equals the descriptor's four declared arg names.
 */
BOOST_FIXTURE_TEST_CASE(coverage_stats, RegistryFixture)
{
    GetLinkStatsQuery query;
    query.slug = "abc";
    query.from = "1000";
    query.to = "2000";
    query.bucket = "hour";
    const std::string json = stats_args_json(query);
    check_validates("link.stats", json);
    BOOST_CHECK(emitted_keys(json) == arg_names("link.stats"));
}

/**
 * [Unit][CLI] lifecycle verbs (delete/enable/disable/restore): the single-slug
 * output validates and its emitted key set equals the {slug} arg name.
 */
BOOST_FIXTURE_TEST_CASE(coverage_lifecycle, RegistryFixture)
{
    const std::string json = slug_args_json("abc");
    for (const char* name :
         {"link.delete", "link.enable", "link.disable", "link.restore"}) {
        check_validates(name, json);
        BOOST_CHECK(emitted_keys(json) == arg_names(name));
    }
}

// --------------------------------------------------------------------------
//  Group 3: end-to-end through the parser
// --------------------------------------------------------------------------

/**
 * [Unit][CLI] A real argv per verb through CliParser::parse, then the matching
 * converter, yields the expected `args` JSON text (C4: flags unchanged).
 */
BOOST_AUTO_TEST_CASE(end_to_end_argv_to_args_json)
{
    struct Case
    {
        std::vector<std::string> argv;
        std::string expected;
    };

    const std::vector<Case> cases = {
        {{"url_shortener", "link", "create", "--url",
          "https://example.com/path"},
         R"({"target_url":"https://example.com/path"})"},
        {{"url_shortener", "link", "get", "--slug", "abc"},
         R"({"slug":"abc"})"},
        {{"url_shortener", "link", "preview", "--id", "xyz"},
         R"({"id":"xyz"})"},
        {{"url_shortener", "link", "update", "--slug", "abc", "--expires-at",
          "clear"},
         R"({"slug":"abc","expires_at":null})"},
        {{"url_shortener", "link", "delete", "--slug", "abc"},
         R"({"slug":"abc"})"},
        {{"url_shortener", "link", "enable", "--slug", "abc"},
         R"({"slug":"abc"})"},
        {{"url_shortener", "link", "disable", "--slug", "abc"},
         R"({"slug":"abc"})"},
        {{"url_shortener", "link", "restore", "--slug", "abc"},
         R"({"slug":"abc"})"},
        {{"url_shortener", "link", "stats", "--slug", "abc", "--from", "1",
          "--to", "2", "--bucket", "hour"},
         R"({"slug":"abc","from":"1","to":"2","bucket":"hour"})"},
    };

    for (const auto& test_case : cases) {
        ArgvBuilder args(test_case.argv);
        CliParser parser;
        const ParseResult result = parser.parse(args.argc(), args.argv());
        BOOST_REQUIRE(result.command.has_value());
        BOOST_CHECK_EQUAL(args_json_for(*result.command), test_case.expected);
    }
}
