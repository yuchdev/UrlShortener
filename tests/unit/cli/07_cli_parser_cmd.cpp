/**
 * @file 07_cli_parser_cmd.cpp
 * @brief Unit tests for the generic one-shot `url_shortener cmd <name>
 * [options]` parsing added to `CliParser::parse`.
 *
 * Scenario: `cmd` turns argv into a plain @ref CmdCliCommand (strings and
 * booleans) carried on `ParseResult::cmd`; nothing is executed and the registry
 * is not consulted. The command name is shape-checked only (@ref
 * is_valid_command_name); `--json`/`--arg` values are stored verbatim. The mode
 * is additive: `link <verb>` and server-flag parsing are byte-for-byte
 * unchanged, and `cmd` is recognised only as argv[1].
 *
 * Boundaries: `--json` and `--arg` are mutually exclusive; `--json -` reads
 * stdin later; `--id` is 1..request_id_max_length bytes; `--log-file` and
 * `--verbose` are mutually exclusive; names must be dotted and <=64 bytes;
 * every error message is value-free (never echoes supplied JSON/arg values,
 * threat model T-77).
 *
 * On failure first check: whether the token loop treated the first bare token
 * as the name (options may precede or follow it) and whether `ParseResult::cmd`
 * and `ParseResult::command` stay mutually exclusive.
 *
 * Milestone 0006, Task 04.0, subtask 12 (threat model T-77/T-79/T-80/T-81).
 */
#define BOOST_TEST_MODULE CliParserCmd
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include <boost/test/unit_test.hpp>

#include <url_shortener/cli/cli_parser.hpp>
#include <url_shortener/observability/logging_config.hpp>

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

/// Parse an invocation and return the whole ParseResult.
ParseResult parse(std::vector<std::string> args)
{
    ArgvBuilder argv(std::move(args));
    return CliParser{}.parse(argv.argc(), argv.argv());
}
}  // namespace

/**
 * [Unit][CLI] A canonical `cmd link.get --json <text>` engages
 * `ParseResult::cmd` (never `ParseResult::command`), carries the name and the
 * JSON body verbatim, and leaves the args list empty.
 */
BOOST_AUTO_TEST_CASE(cmd_json_sets_fields_and_engages_cmd)
{
    const ParseResult r = parse(
        {"url_shortener", "cmd", "link.get", "--json", "{\"slug\":\"a\"}"});

    BOOST_REQUIRE(r.cmd.has_value());
    BOOST_CHECK(!r.command.has_value());
    BOOST_CHECK_EQUAL(r.cmd->command, std::string("link.get"));
    BOOST_REQUIRE(r.cmd->json_args.has_value());
    BOOST_CHECK_EQUAL(*r.cmd->json_args, std::string("{\"slug\":\"a\"}"));
    BOOST_CHECK(!r.cmd->json_from_stdin);
    BOOST_CHECK(r.cmd->arg_pairs.empty());
}

/**
 * [Unit][CLI] Repeated `--arg` operands are kept in command-line order as raw
 * name/value halves.
 */
BOOST_AUTO_TEST_CASE(repeated_arg_keeps_order)
{
    const ParseResult r = parse({"url_shortener", "cmd", "link.list",
        "--arg", "limit=10", "--arg", "offset=5", "--arg", "status=all"});

    BOOST_REQUIRE(r.cmd.has_value());
    BOOST_REQUIRE_EQUAL(r.cmd->arg_pairs.size(), 3u);
    BOOST_CHECK_EQUAL(r.cmd->arg_pairs[0].first, std::string("limit"));
    BOOST_CHECK_EQUAL(r.cmd->arg_pairs[0].second, std::string("10"));
    BOOST_CHECK_EQUAL(r.cmd->arg_pairs[1].first, std::string("offset"));
    BOOST_CHECK_EQUAL(r.cmd->arg_pairs[2].first, std::string("status"));
    BOOST_CHECK_EQUAL(r.cmd->arg_pairs[2].second, std::string("all"));
}

/**
 * [Unit][CLI] `--json -` selects stdin and leaves `json_args` unset.
 */
BOOST_AUTO_TEST_CASE(json_dash_sets_stdin_flag)
{
    const ParseResult r =
        parse({"url_shortener", "cmd", "link.get", "--json", "-"});

    BOOST_REQUIRE(r.cmd.has_value());
    BOOST_CHECK(r.cmd->json_from_stdin);
    BOOST_CHECK(!r.cmd->json_args.has_value());
}

/**
 * [Unit][CLI] Every boolean flag sets its field individually.
 */
BOOST_AUTO_TEST_CASE(boolean_flags_individually)
{
    BOOST_CHECK(parse({"url_shortener", "cmd", "link.get", "--raw"})
                    .cmd->raw);
    BOOST_CHECK(parse({"url_shortener", "cmd", "link.get", "--dry-run"})
                    .cmd->dry_run);
    BOOST_CHECK(parse({"url_shortener", "cmd", "link.get", "--async"})
                    .cmd->async);
    BOOST_CHECK(parse({"url_shortener", "cmd", "link.get", "--no-wait"})
                    .cmd->no_wait);
    BOOST_CHECK(parse({"url_shortener", "cmd", "link.get", "--read-only"})
                    .cmd->read_only);
}

/**
 * [Unit][CLI] The boolean flags combine without interfering.
 */
BOOST_AUTO_TEST_CASE(boolean_flags_combined)
{
    const ParseResult r = parse({"url_shortener", "cmd", "link.get",
        "--raw", "--dry-run", "--async", "--no-wait", "--read-only"});

    BOOST_REQUIRE(r.cmd.has_value());
    BOOST_CHECK(r.cmd->raw);
    BOOST_CHECK(r.cmd->dry_run);
    BOOST_CHECK(r.cmd->async);
    BOOST_CHECK(r.cmd->no_wait);
    BOOST_CHECK(r.cmd->read_only);
}

/**
 * [Unit][CLI] `--id` carries a correlation id verbatim.
 */
BOOST_AUTO_TEST_CASE(id_flag_sets_request_id)
{
    const ParseResult r =
        parse({"url_shortener", "cmd", "link.get", "--id", "abc"});

    BOOST_REQUIRE(r.cmd.has_value());
    BOOST_REQUIRE(r.cmd->request_id.has_value());
    BOOST_CHECK_EQUAL(*r.cmd->request_id, std::string("abc"));
}

/**
 * [Unit][CLI] `--log-file` and `--verbose` set the one-shot logging policy, and
 * `one_shot_logging_config` maps the three states (none/file/verbose) to the
 * documented sinks (decision log D14, threat model T-80).
 */
BOOST_AUTO_TEST_CASE(logging_flags_and_config_mapping)
{
    using url_shortener::observability::LogSink;

    const ParseResult file_r = parse(
        {"url_shortener", "cmd", "link.get", "--log-file", "x.log"});
    BOOST_REQUIRE(file_r.cmd.has_value());
    BOOST_REQUIRE(file_r.cmd->logging.log_file.has_value());
    BOOST_CHECK_EQUAL(*file_r.cmd->logging.log_file, std::string("x.log"));
    const auto file_cfg = one_shot_logging_config(file_r.cmd->logging);
    BOOST_CHECK(file_cfg.enabled);
    BOOST_CHECK(file_cfg.sink == LogSink::File);
    BOOST_CHECK_EQUAL(file_cfg.file_path, std::string("x.log"));

    const ParseResult verbose_r =
        parse({"url_shortener", "cmd", "link.get", "--verbose"});
    BOOST_REQUIRE(verbose_r.cmd.has_value());
    BOOST_CHECK(verbose_r.cmd->logging.verbose);
    const auto verbose_cfg = one_shot_logging_config(verbose_r.cmd->logging);
    BOOST_CHECK(verbose_cfg.enabled);
    BOOST_CHECK(verbose_cfg.sink == LogSink::Stderr);

    const OneShotLogging none;
    const auto none_cfg = one_shot_logging_config(none);
    BOOST_CHECK(!none_cfg.enabled);
}

/**
 * [Unit][CLI] `--log-file` and `--verbose` together are rejected.
 */
BOOST_AUTO_TEST_CASE(log_file_and_verbose_conflict)
{
    BOOST_CHECK_THROW(
        parse({"url_shortener", "cmd", "link.get",
            "--log-file", "x.log", "--verbose"}),
        std::invalid_argument);
}

/**
 * [Unit][CLI] `--base-domain` and `--allow-private-targets` reach
 * `ParseResult::config` using the shared helpers.
 */
BOOST_AUTO_TEST_CASE(service_config_flags_reach_config)
{
    const ParseResult r = parse({"url_shortener", "cmd", "link.create",
        "--base-domain", "https://sho.rt",
        "--allow-private-targets", "true"});

    BOOST_REQUIRE(r.cmd.has_value());
    BOOST_CHECK_EQUAL(
        r.config.shortener_base_domain, std::string("https://sho.rt"));
    BOOST_CHECK(r.config.shortener_allow_private_targets);
}

/**
 * [Unit][CLI] Options may appear before and after the command name; the first
 * bare token is the name regardless of position.
 */
BOOST_AUTO_TEST_CASE(options_before_and_after_name)
{
    const ParseResult r = parse({"url_shortener", "cmd",
        "--raw", "link.get", "--dry-run"});

    BOOST_REQUIRE(r.cmd.has_value());
    BOOST_CHECK_EQUAL(r.cmd->command, std::string("link.get"));
    BOOST_CHECK(r.cmd->raw);
    BOOST_CHECK(r.cmd->dry_run);
}

/**
 * [Unit][CLI] `cmd` with no name is a usage error.
 */
BOOST_AUTO_TEST_CASE(cmd_alone_is_error)
{
    BOOST_CHECK_THROW(parse({"url_shortener", "cmd"}), std::invalid_argument);
}

/**
 * [Unit][CLI] Names failing the dotted-name shape (or the length cap) are
 * rejected: upper case, single segment, doubled dot, empty, and 65 bytes.
 */
BOOST_AUTO_TEST_CASE(bad_names_are_rejected)
{
    BOOST_CHECK_THROW(
        parse({"url_shortener", "cmd", "Link.Get"}), std::invalid_argument);
    BOOST_CHECK_THROW(
        parse({"url_shortener", "cmd", "link"}), std::invalid_argument);
    BOOST_CHECK_THROW(
        parse({"url_shortener", "cmd", "link..get"}), std::invalid_argument);
    BOOST_CHECK_THROW(
        parse({"url_shortener", "cmd", ""}), std::invalid_argument);

    const std::string name_65 = "a." + std::string(63, 'a');
    BOOST_REQUIRE_EQUAL(name_65.size(), 65u);
    BOOST_CHECK_THROW(
        parse({"url_shortener", "cmd", name_65}), std::invalid_argument);
}

/**
 * [Unit][CLI] An unknown option is rejected.
 */
BOOST_AUTO_TEST_CASE(unknown_option_is_rejected)
{
    BOOST_CHECK_THROW(
        parse({"url_shortener", "cmd", "link.get", "--nope"}),
        std::invalid_argument);
}

/**
 * [Unit][CLI] `--json` errors: missing value, repeated, empty, and combined
 * with `--arg`.
 */
BOOST_AUTO_TEST_CASE(json_flag_errors)
{
    // Missing value (last token).
    BOOST_CHECK_THROW(
        parse({"url_shortener", "cmd", "link.get", "--json"}),
        std::invalid_argument);
    // Repeated.
    BOOST_CHECK_THROW(
        parse({"url_shortener", "cmd", "link.get",
            "--json", "{\"a\":1}", "--json", "{\"b\":2}"}),
        std::invalid_argument);
    // Empty value.
    BOOST_CHECK_THROW(
        parse({"url_shortener", "cmd", "link.get", "--json", ""}),
        std::invalid_argument);
    // Combined with --arg.
    BOOST_CHECK_THROW(
        parse({"url_shortener", "cmd", "link.get",
            "--json", "{\"a\":1}", "--arg", "x=y"}),
        std::invalid_argument);
}

/**
 * [Unit][CLI] `--id` errors: empty value and over the length cap (65 bytes vs
 * the default request_id_max_length of 64).
 */
BOOST_AUTO_TEST_CASE(id_flag_errors)
{
    BOOST_CHECK_THROW(
        parse({"url_shortener", "cmd", "link.get", "--id", ""}),
        std::invalid_argument);
    BOOST_CHECK_THROW(
        parse({"url_shortener", "cmd", "link.get",
            "--id", std::string(65, 'x')}),
        std::invalid_argument);
}

/**
 * [Unit][CLI] A second positional after the command name is an error.
 */
BOOST_AUTO_TEST_CASE(stray_positional_is_error)
{
    BOOST_CHECK_THROW(
        parse({"url_shortener", "cmd", "link.get", "extra"}),
        std::invalid_argument);
}

/**
 * [Unit][CLI] No parse error ever echoes the supplied JSON (threat model T-77).
 * A repeated `--json` carrying a recognisable sentinel must not leak it.
 */
BOOST_AUTO_TEST_CASE(error_text_omits_supplied_json)
{
    const std::string sentinel = "do-not-leak-sentinel-42";
    try {
        parse({"url_shortener", "cmd", "link.get",
            "--json", "{\"k\":\"" + sentinel + "\"}",
            "--json", "{\"b\":2}"});
        BOOST_FAIL("expected std::invalid_argument");
    }
    catch (const std::invalid_argument& e) {
        BOOST_CHECK(std::string(e.what()).find(sentinel) == std::string::npos);
    }
}

/**
 * [Unit][CLI] `cmd --help` (no name) sets `help_requested` with usage text
 * mentioning the key options, and engages neither `cmd` nor `command`.
 */
BOOST_AUTO_TEST_CASE(cmd_help_no_name_is_help_request)
{
    const ParseResult r = parse({"url_shortener", "cmd", "--help"});

    BOOST_REQUIRE(r.help_requested);
    BOOST_CHECK(!r.cmd.has_value());
    BOOST_CHECK(!r.command.has_value());
    const std::string& help = r.help_text;
    BOOST_CHECK(help.find("--json") != std::string::npos);
    BOOST_CHECK(help.find("--arg") != std::string::npos);
    BOOST_CHECK(help.find("--raw") != std::string::npos);
    BOOST_CHECK(help.find("--no-wait") != std::string::npos);
    BOOST_CHECK(help.find("--read-only") != std::string::npos);
}

/**
 * [Unit][CLI] `cmd <name> --help` carries help on the command (for the
 * dispatcher to render), not a top-level help request.
 */
BOOST_AUTO_TEST_CASE(cmd_name_help_sets_command_help)
{
    const ParseResult r =
        parse({"url_shortener", "cmd", "link.get", "--help"});

    BOOST_REQUIRE(r.cmd.has_value());
    BOOST_CHECK(r.cmd->help);
    BOOST_CHECK(!r.help_requested);
    BOOST_CHECK_EQUAL(r.cmd->command, std::string("link.get"));
}

/**
 * [Unit][CLI] The `link <verb>` short-circuit is untouched by the new mode:
 * `link get --slug a` still parses to `ParseResult::command` and leaves
 * `ParseResult::cmd` empty.
 */
BOOST_AUTO_TEST_CASE(link_mode_unchanged)
{
    const ParseResult r =
        parse({"url_shortener", "link", "get", "--slug", "a"});

    BOOST_REQUIRE(r.command.has_value());
    BOOST_CHECK(!r.cmd.has_value());
    BOOST_CHECK(r.command->verb == LinkCliVerb::get);
}

/**
 * [Unit][CLI] Server-flag parsing is untouched: `--http-port 9000` still sets
 * the port and engages neither one-shot mode.
 */
BOOST_AUTO_TEST_CASE(server_mode_unchanged)
{
    const ParseResult r = parse({"url_shortener", "--http-port", "9000"});

    BOOST_CHECK(!r.cmd.has_value());
    BOOST_CHECK(!r.command.has_value());
    BOOST_CHECK_EQUAL(r.config.http_port, 9000);
}

/**
 * [Unit][CLI] `cmd` is recognised only as argv[1]: a trailing `cmd` in server
 * mode stays a server-flag (positional) error, exactly as before.
 */
BOOST_AUTO_TEST_CASE(cmd_only_recognized_as_argv1)
{
    BOOST_CHECK_THROW(
        parse({"url_shortener", "--http-port", "9000", "cmd"}),
        std::invalid_argument);
}
