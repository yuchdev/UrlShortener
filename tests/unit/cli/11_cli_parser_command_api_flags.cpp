/**
 * @file 11_cli_parser_command_api_flags.cpp
 * @brief Unit tests: the opt-in command API gate flags
 * `--command-api-enabled` and `--command-api-role` parse, validate and default
 * securely (off / user), coexist with server options, and are described in
 * `--help` with the loopback-only and reverse-proxy caveats.
 *
 * Milestone 0006, Task 04.0, subtask 08 (threat model T-64/T-65).
 */
#define BOOST_TEST_MODULE CliParserCommandApiFlags
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include <boost/test/unit_test.hpp>
#include <url_shortener/cli/cli_parser.hpp>

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

    int argc() const { return static_cast<int>(m_pointers_.size()); }

    char** argv() { return m_pointers_.data(); }

private:
    std::vector<std::string> m_storage_;
    std::vector<char*> m_pointers_;
};

/// Parse a server-mode invocation and return the resulting config.
ServerConfig parseConfig(std::vector<std::string> args)
{
    ArgvBuilder argv(std::move(args));
    return CliParser{}.parse(argv.argc(), argv.argv()).config;
}
}  // namespace

/**
 * [Unit][CLI] Secure defaults: with no command-API flags the gate is off and
 * the role is the read-only `user`. This default is security-load-bearing
 * (threat model T-64): the whole new HTTP surface stays unreachable.
 *
 * If this breaks, first check:
 *   - ServerConfig initialises command_api_enabled=false, role="user".
 *   - No code path flips either default during server-mode parsing.
 */
BOOST_AUTO_TEST_CASE(defaults_are_off_and_user)
{
    const ServerConfig cfg = parseConfig({"url_shortener"});
    BOOST_CHECK_EQUAL(cfg.command_api_enabled, false);
    BOOST_CHECK_EQUAL(cfg.command_api_role, std::string("user"));
}

/**
 * [Unit][CLI] `--command-api-enabled` accepts the shared BOOL grammar exactly
 * like the other boolean flags (true/false/1/0).
 */
BOOST_AUTO_TEST_CASE(enabled_flag_accepts_bool_tokens)
{
    BOOST_CHECK_EQUAL(
        parseConfig({"url_shortener", "--command-api-enabled", "true"})
            .command_api_enabled,
        true);
    BOOST_CHECK_EQUAL(
        parseConfig({"url_shortener", "--command-api-enabled", "false"})
            .command_api_enabled,
        false);
    BOOST_CHECK_EQUAL(
        parseConfig({"url_shortener", "--command-api-enabled", "1"})
            .command_api_enabled,
        true);
    BOOST_CHECK_EQUAL(
        parseConfig({"url_shortener", "--command-api-enabled", "0"})
            .command_api_enabled,
        false);
}

/**
 * [Unit][CLI] An unparseable boolean for `--command-api-enabled` is rejected
 * with std::invalid_argument, just like the other BOOL flags.
 */
BOOST_AUTO_TEST_CASE(enabled_flag_rejects_invalid_bool)
{
    BOOST_CHECK_THROW(
        parseConfig({"url_shortener", "--command-api-enabled", "maybe"}),
        std::invalid_argument);
}

/**
 * [Unit][CLI] `--command-api-role` accepts exactly the two documented roles.
 */
BOOST_AUTO_TEST_CASE(role_flag_accepts_user_and_admin)
{
    BOOST_CHECK_EQUAL(
        parseConfig({"url_shortener", "--command-api-role", "user"})
            .command_api_role,
        std::string("user"));
    BOOST_CHECK_EQUAL(
        parseConfig({"url_shortener", "--command-api-role", "admin"})
            .command_api_role,
        std::string("admin"));
}

/**
 * [Unit][CLI] `--command-api-role` is case-sensitive and closed: `Admin`,
 * `root` and an explicit empty value are all rejected (threat model T-64 — a
 * typo must never silently widen or narrow the role).
 *
 * If this breaks, first check:
 *   - The validation uses vm.count (not emptiness) as the "provided" sentinel,
 *     so `--command-api-role ""` reaches the user/admin check and throws.
 */
BOOST_AUTO_TEST_CASE(role_flag_rejects_other_values)
{
    BOOST_CHECK_THROW(
        parseConfig({"url_shortener", "--command-api-role", "Admin"}),
        std::invalid_argument);
    BOOST_CHECK_THROW(
        parseConfig({"url_shortener", "--command-api-role", "root"}),
        std::invalid_argument);
    BOOST_CHECK_THROW(
        parseConfig({"url_shortener", "--command-api-role", ""}),
        std::invalid_argument);
}

/**
 * [Unit][CLI] The gate flags combine with ordinary server options without
 * disturbing them (e.g. `--http-port 9000`).
 */
BOOST_AUTO_TEST_CASE(flags_combine_with_server_options)
{
    const ServerConfig cfg = parseConfig({"url_shortener",
        "--http-port", "9000",
        "--command-api-enabled", "true",
        "--command-api-role", "admin"});
    BOOST_CHECK_EQUAL(cfg.http_port, 9000);
    BOOST_CHECK_EQUAL(cfg.command_api_enabled, true);
    BOOST_CHECK_EQUAL(cfg.command_api_role, std::string("admin"));
}

/**
 * [Unit][CLI] The new server options do not disturb the `link <verb>`
 * short-circuit: `link create` still parses to a command and leaves the gate at
 * its secure defaults (the link path never reads these flags).
 */
BOOST_AUTO_TEST_CASE(link_short_circuit_is_undisturbed)
{
    ArgvBuilder args({"url_shortener", "link", "create",
        "--url", "https://example.com/path"});
    const ParseResult result = CliParser{}.parse(args.argc(), args.argv());

    BOOST_REQUIRE(result.command.has_value());
    BOOST_CHECK(result.command->verb == LinkCliVerb::create);
    BOOST_CHECK_EQUAL(result.config.command_api_enabled, false);
    BOOST_CHECK_EQUAL(result.config.command_api_role, std::string("user"));
}

/**
 * [Unit][CLI] The generic `cmd <name>` short-circuit, like `link`, never
 * touches the command-API gate: `cmd link.list` routes to cmd mode (engaging
 * `ParseResult::cmd`, not server mode) and leaves the gate at its secure
 * defaults (off / user). The gate flags belong to server mode only (the
 * one-shot `cmd` session's role comes from `--read-only`, not these flags).
 *
 * If this breaks, first check:
 *   - The `argv[1] == "cmd"` branch returns before any server-flag parsing.
 *   - ServerConfig initialises command_api_enabled=false, role="user".
 */
BOOST_AUTO_TEST_CASE(cmd_short_circuit_is_undisturbed)
{
    ArgvBuilder args({"url_shortener", "cmd", "link.list"});
    const ParseResult result = CliParser{}.parse(args.argc(), args.argv());

    BOOST_REQUIRE(result.cmd.has_value());
    BOOST_CHECK(!result.command.has_value());
    BOOST_CHECK_EQUAL(result.cmd->command, std::string("link.list"));
    BOOST_CHECK_EQUAL(result.config.command_api_enabled, false);
    BOOST_CHECK_EQUAL(result.config.command_api_role, std::string("user"));
}

/**
 * [Unit][CLI] `--help` lists both gate flags, states the default is off, and
 * carries the loopback-only / reverse-proxy / X-Forwarded-For caveats (threat
 * model T-65 mitigation is the help text itself).
 *
 * The wrapped lines are matched with caveat keywords rather than whole
 * sentences because Boost.ProgramOptions wraps the description at 80 columns.
 */
BOOST_AUTO_TEST_CASE(help_lists_flags_and_caveats)
{
    ArgvBuilder args({"url_shortener", "--help"});
    const ParseResult result = CliParser{}.parse(args.argc(), args.argv());

    BOOST_REQUIRE(result.help_requested);
    const std::string& help = result.help_text;

    BOOST_CHECK(help.find("--command-api-enabled") != std::string::npos);
    BOOST_CHECK(help.find("--command-api-role") != std::string::npos);
    // Default-off is stated.
    BOOST_CHECK(help.find("off by") != std::string::npos);
    BOOST_CHECK(help.find("default: false") != std::string::npos);
    // admin is loopback-only, with the same-host reverse-proxy caveat.
    BOOST_CHECK(help.find("loopback") != std::string::npos);
    BOOST_CHECK(help.find("reverse") != std::string::npos);
    BOOST_CHECK(help.find("X-Forwarded-For") != std::string::npos);
}
