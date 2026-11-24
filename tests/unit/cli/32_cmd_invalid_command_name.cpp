/**
 * @file 32_cmd_invalid_command_name.cpp
 * @brief Unit tests pinning that `cmd` maps a syntactically invalid command name
 * to the catalog's `invalid_envelope` envelope (exit 2), never a crash.
 *
 * Type: unit (in-process, injected std::istringstream/std::ostringstream; no
 * network, no subprocess).
 *
 * Scenario:
 *   DispatchCmdCommand receives the command name verbatim from the CLI parser
 *   (the parser no longer rejects a malformed name). An upper-case name, an
 *   empty name, a name with an embedded slash, and an over-length name all fail
 *   @ref url_shortener::commands::is_valid_command_name before the registry
 *   lookup, so the dispatcher must print an `"ok":false` /
 *   `error.code == "invalid_envelope"` envelope on stdout, return exit 2, and
 *   write nothing (no FATAL line) to stderr. A well-formed but unknown name is a
 *   different outcome (`unknown_command`), proven in `08_cmd_dispatch.cpp`.
 *
 * Boundaries:
 *   - Upper-case / empty / slash / 200-byte names -> exit 2, invalid_envelope.
 *   - id echo follows the CLI rule: `--id` when set, else the minted `cli-1`.
 *   - `--raw` prints only a value-free `cmd failed: invalid_envelope: ...` line
 *     on stderr with empty stdout and the same exit 2.
 *   - stderr never carries a `FATAL` level line (the bug produced one).
 *
 * On failure first check:
 *   - That cmd_dispatch.cpp still guards the name before the registry lookup and
 *     routes the exit code through commands::exit_code_for (invalid_envelope ->
 *     2), and that the CLI parser still carries a malformed name verbatim rather
 *     than throwing.
 *
 * Regression for: `url_shortener cmd LINK.CREATE --json '{}'` crashing with a
 * FATAL log line and exit 1 instead of returning the exit-2 envelope.
 */
#define BOOST_TEST_MODULE CliCmdInvalidCommandName
#include <boost/test/unit_test.hpp>

#include <sstream>
#include <string>

#include <url_shortener/cli/cli_parser.hpp>
#include <url_shortener/cli/cmd_dispatch.hpp>
#include <url_shortener/core/config.hpp>

namespace
{
/// Minimal ServerConfig; the invalid-name guard runs before the runtime is
/// acquired, so no storage backend or short-URL rendering is exercised here.
ServerConfig makeConfig()
{
    ServerConfig config;
    config.shortener_base_domain = "http://sho.rt";
    config.shortener_generated_slug_length = 7;
    return config;
}

/// Outcome of one dispatch: exit code plus captured stdout/stderr text.
struct CmdResult
{
    int rc = 0;
    std::string out;
    std::string err;
};

/// Run DispatchCmdCommand against captured streams (empty stdin).
CmdResult run(const CmdCliCommand& command, const ServerConfig& config)
{
    std::istringstream in;
    std::ostringstream out;
    std::ostringstream err;
    CmdResult result;
    result.rc = url_shortener::cli::DispatchCmdCommand(command, config, in, out,
                                                       err);
    result.out = out.str();
    result.err = err.str();
    return result;
}

bool contains(const std::string& haystack, const std::string& needle)
{
    return haystack.find(needle) != std::string::npos;
}

/// Build a `cmd <name>` invocation carrying an empty `--json {}` body.
CmdCliCommand named(const std::string& name)
{
    CmdCliCommand command;
    command.command = name;
    command.json_args = "{}";
    return command;
}
}  // namespace

/**
 * [Unit][CLI] Every syntactically invalid command name yields exit 2 and an
 * `invalid_envelope` envelope on stdout, with no FATAL line on stderr.
 */
BOOST_AUTO_TEST_CASE(invalid_names_return_invalid_envelope_envelope)
{
    const ServerConfig config = makeConfig();
    const std::string over_length(200, 'a');  // valid chars, over the 64 cap.

    for (const std::string& name :
         {std::string("LINK.CREATE"), std::string(""), std::string("a/b"),
          std::string("has space"), over_length})
    {
        const CmdResult r = run(named(name), config);

        BOOST_CHECK_EQUAL(r.rc, 2);
        BOOST_CHECK(contains(r.out, "\"ok\":false"));
        BOOST_CHECK(contains(r.out, "\"code\":\"invalid_envelope\""));
        // The rejected name is never echoed (threat model T-77), and the bug's
        // FATAL log line must be gone from the default (non-raw) path's stderr.
        BOOST_CHECK(r.err.empty());
        BOOST_CHECK(!contains(r.out, name.empty() ? "\x01never" : name));
    }
}

/**
 * [Unit][CLI] The invalid-name envelope echoes the minted `cli-1` id by default
 * and the `--id` value when one is supplied (the as-built CLI id rule).
 */
BOOST_AUTO_TEST_CASE(invalid_name_echoes_id_per_cli_rule)
{
    const ServerConfig config = makeConfig();

    const CmdResult minted = run(named("LINK.CREATE"), config);
    BOOST_CHECK_EQUAL(minted.rc, 2);
    BOOST_CHECK(contains(minted.out, "\"id\":\"cli-1\""));

    CmdCliCommand with_id = named("LINK.CREATE");
    with_id.request_id = "corr-42";
    const CmdResult echoed = run(with_id, config);
    BOOST_CHECK_EQUAL(echoed.rc, 2);
    BOOST_CHECK(contains(echoed.out, "\"id\":\"corr-42\""));
}

/**
 * [Unit][CLI] Under `--raw` an invalid name prints only a value-free
 * `cmd failed: invalid_envelope: ...` line on stderr (empty stdout), same exit
 * 2, and still no FATAL line.
 */
BOOST_AUTO_TEST_CASE(invalid_name_raw_mode_is_value_free_stderr)
{
    const ServerConfig config = makeConfig();
    CmdCliCommand command = named("LINK.CREATE");
    command.raw = true;

    const CmdResult r = run(command, config);

    BOOST_CHECK_EQUAL(r.rc, 2);
    BOOST_CHECK(r.out.empty());
    BOOST_CHECK(contains(r.err, "cmd failed: invalid_envelope:"));
    BOOST_CHECK(!contains(r.err, "FATAL"));
    BOOST_CHECK(!contains(r.err, "LINK.CREATE"));
}

/**
 * [Unit][CLI] A malformed `--arg` operand (the parser's `malformed_arg` marker)
 * is rejected by the dispatcher with the same `invalid_envelope` envelope on
 * stdout, exit 2, and no FATAL line - even when the command name is valid. The
 * id echo follows the CLI rule (`cli-1` by default, `--id` when set).
 */
BOOST_AUTO_TEST_CASE(malformed_arg_returns_invalid_envelope_envelope)
{
    const ServerConfig config = makeConfig();

    CmdCliCommand command;
    command.command = "link.get";
    command.malformed_arg = true;  // e.g. `--arg foo` or `--arg =v`.

    const CmdResult r = run(command, config);

    BOOST_CHECK_EQUAL(r.rc, 2);
    BOOST_CHECK(contains(r.out, "\"ok\":false"));
    BOOST_CHECK(contains(r.out, "\"code\":\"invalid_envelope\""));
    BOOST_CHECK(contains(r.out, "\"id\":\"cli-1\""));
    BOOST_CHECK(r.err.empty());

    CmdCliCommand with_id = command;
    with_id.request_id = "corr-7";
    const CmdResult echoed = run(with_id, config);
    BOOST_CHECK_EQUAL(echoed.rc, 2);
    BOOST_CHECK(contains(echoed.out, "\"id\":\"corr-7\""));
}

/**
 * [Unit][CLI] A well-formed `--arg k=` (empty VALUE) does NOT set the marker and
 * is coerced normally, so the dispatcher does not short-circuit to
 * `invalid_envelope`: the request reaches the registry and resolves on the
 * command's own argument rules (here `link.get` with no `slug` -> `invalid_field`,
 * NOT `invalid_envelope`). This pins the empty-value-stays-valid decision.
 */
BOOST_AUTO_TEST_CASE(empty_arg_value_is_not_malformed)
{
    const ServerConfig config = makeConfig();

    CmdCliCommand command;
    command.command = "link.get";
    command.arg_pairs = {{"slug", ""}};  // `--arg slug=` -> empty-string slug.
    // malformed_arg stays false (an empty VALUE is well-formed).

    const CmdResult r = run(command, config);

    // Whatever the exact registry verdict, it must NOT be the dispatcher's
    // invalid_envelope short-circuit, and must never crash.
    BOOST_CHECK(!contains(r.out, "\"code\":\"invalid_envelope\""));
    BOOST_CHECK(!contains(r.err, "FATAL"));
}
