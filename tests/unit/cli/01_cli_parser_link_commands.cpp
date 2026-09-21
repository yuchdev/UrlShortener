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
        "--target-url", "https://example.com/path"});
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
        ArgvBuilder args({"url_shortener", "link", "get"});
        const auto r = CliParser{}.parse(args.argc(), args.argv());
        BOOST_REQUIRE(r.command.has_value());
        BOOST_CHECK(r.command->verb == LinkCliVerb::get);
        BOOST_CHECK(std::holds_alternative<url_shortener::app::GetLinkQuery>(
            r.command->payload));
    }
    {
        ArgvBuilder args({"url_shortener", "link", "delete"});
        const auto r = CliParser{}.parse(args.argc(), args.argv());
        BOOST_REQUIRE(r.command.has_value());
        BOOST_CHECK(r.command->verb == LinkCliVerb::del);
        BOOST_CHECK(
            std::holds_alternative<url_shortener::app::DeleteLinkCommand>(
                r.command->payload));
    }
    {
        ArgvBuilder args({"url_shortener", "link", "enable"});
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
        ArgvBuilder args({"url_shortener", "link", "disable"});
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
        ArgvBuilder args({"url_shortener", "link", "preview"});
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
