/**
 * @file 08_cmd_dispatch.cpp
 * @brief Unit tests for DispatchCmdCommand: the generic `cmd <name>` CLI adapter
 * (Task 04.0 subtask 13).
 *
 * Type: unit (in-process, injected std::istringstream/std::ostringstream; no
 * network, no subprocess).
 *
 * Scenario:
 *   DispatchCmdCommand assembles a request envelope from `--json`/stdin/`--arg`,
 *   runs it through the one shared CommandRegistry as the local operator, prints
 *   the response envelope (or only `result` under `--raw`) on `out`, and returns
 *   the shared-table exit code. These tests exercise that entry point directly
 *   with captured streams, asserting stdout shape, stderr diagnostics and exit
 *   codes without starting a server.
 *
 * Boundaries:
 *   - Success path: `link.create` via `--json` and via `--arg` yield exit 0 and
 *     an `"ok":true` envelope carrying `result.slug`; `--raw` prints only the
 *     result; `--id` is echoed; `command.list`/`system.info` succeed.
 *   - Input-shape rejection (never executed): non-object `--json`, extra-member
 *     smuggling, oversized stdin -> exit 2 with a value-free stderr line.
 *   - Execution errors: invalid URL -> 2 invalid_url; unknown command -> 2;
 *     missing link -> 1 not_found; reserved slug -> 2; wrong-typed arg -> 2
 *     invalid_field; `--dry-run` on a read command -> 2 unsupported.
 *   - Authorization: `--read-only` + a write -> 5 not_authorized; + a read -> 0.
 *   - Mutation semantics: `--dry-run` create does not persist (same-process get
 *     -> not_found).
 *   - Help: `<name> --help` prints the descriptor help and exits 0.
 *   - Safety: no output ever echoes the `--json` body; the function never throws.
 *
 * On failure first check:
 *   - That cmd_dispatch.cpp still routes every exit code through
 *     commands::exit_code_for and never introduces a second table.
 *   - That the process-wide in-memory store is shared across cases in this
 *     binary: reused slugs collide, so each case below uses a unique slug.
 *   - That logging stays disabled by default (the injected err stream must not
 *     receive a `command_audit` line on a successful mutation).
 */
#define BOOST_TEST_MODULE CliCmdDispatch
#include <boost/test/unit_test.hpp>

#include <sstream>
#include <string>

#include <url_shortener/cli/cli_parser.hpp>
#include <url_shortener/cli/cmd_dispatch.hpp>
#include <url_shortener/core/config.hpp>

namespace
{
/// Minimal ServerConfig sufficient for the command registry and short-URL
/// rendering the create path performs.
ServerConfig makeConfig()
{
    ServerConfig config;
    config.shortener_base_domain = "http://sho.rt";
    config.shortener_generated_slug_length = 7;
    config.shortener_default_redirect_type = "temporary";
    config.shortener_allow_private_targets = false;
    return config;
}

/// Outcome of one dispatch: exit code plus captured stdout/stderr text.
struct CmdResult
{
    int rc = 0;
    std::string out;
    std::string err;
};

/// Run DispatchCmdCommand against captured streams, feeding @p stdin_text to the
/// child's input stream.
CmdResult run(const CmdCliCommand& command, const ServerConfig& config,
              const std::string& stdin_text = "")
{
    std::istringstream in(stdin_text);
    std::ostringstream out;
    std::ostringstream err;
    CmdResult result;
    result.rc = url_shortener::cli::DispatchCmdCommand(command, config, in, out,
                                                       err);
    result.out = out.str();
    result.err = err.str();
    return result;
}

/// Build a `cmd link.create` with a `--json` body.
CmdCliCommand jsonCreate(const std::string& slug, const std::string& url)
{
    CmdCliCommand command;
    command.command = "link.create";
    command.json_args =
        "{\"target_url\":\"" + url + "\",\"slug\":\"" + slug + "\"}";
    return command;
}

bool contains(const std::string& haystack, const std::string& needle)
{
    return haystack.find(needle) != std::string::npos;
}
}  // namespace

BOOST_AUTO_TEST_CASE(json_create_success_envelope)
{
    const ServerConfig config = makeConfig();
    const CmdResult r =
        run(jsonCreate("cmdj1", "https://example.com/a"), config);

    BOOST_CHECK_EQUAL(r.rc, 0);
    BOOST_CHECK(contains(r.out, "\"ok\":true"));
    BOOST_CHECK(contains(r.out, "\"slug\":\"cmdj1\""));
    // D14/T-80: a successful mutation must not leak an audit line to stderr.
    BOOST_CHECK(r.err.empty());
}

BOOST_AUTO_TEST_CASE(arg_create_matches_json_create)
{
    const ServerConfig config = makeConfig();

    CmdCliCommand command;
    command.command = "link.create";
    command.arg_pairs = {{"target_url", "https://example.com/a"},
                         {"slug", "cmda1"}};
    const CmdResult r = run(command, config);

    BOOST_CHECK_EQUAL(r.rc, 0);
    BOOST_CHECK(contains(r.out, "\"ok\":true"));
    BOOST_CHECK(contains(r.out, "\"slug\":\"cmda1\""));
}

BOOST_AUTO_TEST_CASE(raw_prints_only_result)
{
    const ServerConfig config = makeConfig();
    CmdCliCommand command = jsonCreate("cmdraw1", "https://example.com/a");
    command.raw = true;
    const CmdResult r = run(command, config);

    BOOST_CHECK_EQUAL(r.rc, 0);
    // --raw prints only the result object: a slug, no envelope "ok" field.
    BOOST_CHECK(contains(r.out, "\"slug\":\"cmdraw1\""));
    BOOST_CHECK(!contains(r.out, "\"ok\":"));
    BOOST_CHECK_EQUAL(r.out.front(), '{');
}

BOOST_AUTO_TEST_CASE(id_is_echoed)
{
    const ServerConfig config = makeConfig();
    CmdCliCommand command = jsonCreate("cmdid1", "https://example.com/a");
    command.request_id = "c-7";
    const CmdResult r = run(command, config);

    BOOST_CHECK_EQUAL(r.rc, 0);
    BOOST_CHECK(contains(r.out, "\"id\":\"c-7\""));
}

BOOST_AUTO_TEST_CASE(invalid_url_exits_two_with_envelope)
{
    const ServerConfig config = makeConfig();
    // The sentinel must never appear in any output (threat model T-77).
    const CmdResult r =
        run(jsonCreate("cmdbad1", "ht!tp://SENTINEL"), config);

    BOOST_CHECK_EQUAL(r.rc, 2);
    BOOST_CHECK(contains(r.out, "\"ok\":false"));
    BOOST_CHECK(contains(r.out, "invalid_url"));
    BOOST_CHECK(!contains(r.out, "SENTINEL"));
    BOOST_CHECK(!contains(r.err, "SENTINEL"));
}

BOOST_AUTO_TEST_CASE(unknown_slug_get_exits_one)
{
    const ServerConfig config = makeConfig();
    CmdCliCommand command;
    command.command = "link.get";
    command.arg_pairs = {{"slug", "cmdabsent1"}};
    const CmdResult r = run(command, config);

    BOOST_CHECK_EQUAL(r.rc, 1);
    BOOST_CHECK(contains(r.out, "not_found"));
}

BOOST_AUTO_TEST_CASE(unknown_command_exits_two)
{
    const ServerConfig config = makeConfig();
    CmdCliCommand command;
    command.command = "unknown.command";
    const CmdResult r = run(command, config);

    BOOST_CHECK_EQUAL(r.rc, 2);
    BOOST_CHECK(contains(r.out, "unknown_command"));
}

BOOST_AUTO_TEST_CASE(malformed_json_bodies_rejected_before_execution)
{
    const ServerConfig config = makeConfig();

    for (const std::string& body :
         {std::string("not json"), std::string("[1]"),
          std::string("{\"a\":1},\"command\":\"x\"")})
    {
        CmdCliCommand command;
        command.command = "link.create";
        command.json_args = body;
        const CmdResult r = run(command, config);

        BOOST_CHECK_EQUAL(r.rc, 2);
        // Nothing executed: stdout stays empty, stderr is the fixed message.
        BOOST_CHECK(r.out.empty());
        BOOST_CHECK(contains(r.err, "invalid_envelope"));
        BOOST_CHECK(contains(r.err, "args must be a JSON object"));
        // The smuggling attempt is never echoed back.
        BOOST_CHECK(!contains(r.err, "\"command\":\"x\""));
    }
}

BOOST_AUTO_TEST_CASE(wrong_typed_arg_exits_two_invalid_field)
{
    const ServerConfig config = makeConfig();
    CmdCliCommand command;
    command.command = "link.list";
    command.arg_pairs = {{"limit", "abc"}};  // integer arg, non-numeric token
    const CmdResult r = run(command, config);

    BOOST_CHECK_EQUAL(r.rc, 2);
    BOOST_CHECK(contains(r.out, "invalid_field"));
}

BOOST_AUTO_TEST_CASE(reserved_slug_exits_two)
{
    const ServerConfig config = makeConfig();
    const CmdResult r =
        run(jsonCreate("api", "https://example.com/a"), config);

    BOOST_CHECK_EQUAL(r.rc, 2);
    BOOST_CHECK(contains(r.out, "reserved_slug"));
}

BOOST_AUTO_TEST_CASE(read_only_blocks_write_allows_read)
{
    const ServerConfig config = makeConfig();

    CmdCliCommand write = jsonCreate("cmdro1", "https://example.com/a");
    write.read_only = true;
    const CmdResult w = run(write, config);
    BOOST_CHECK_EQUAL(w.rc, 5);
    BOOST_CHECK(contains(w.out, "not_authorized"));

    CmdCliCommand read;
    read.command = "link.list";
    read.read_only = true;
    const CmdResult rd = run(read, config);
    BOOST_CHECK_EQUAL(rd.rc, 0);
    BOOST_CHECK(contains(rd.out, "\"ok\":true"));
}

BOOST_AUTO_TEST_CASE(dry_run_create_does_not_persist)
{
    const ServerConfig config = makeConfig();

    CmdCliCommand create = jsonCreate("cmddry1", "https://example.com/a");
    create.dry_run = true;
    const CmdResult c = run(create, config);
    BOOST_CHECK_EQUAL(c.rc, 0);

    // The dry-run must not have stored the link: a same-process get misses.
    CmdCliCommand get;
    get.command = "link.get";
    get.arg_pairs = {{"slug", "cmddry1"}};
    const CmdResult g = run(get, config);
    BOOST_CHECK_EQUAL(g.rc, 1);
    BOOST_CHECK(contains(g.out, "not_found"));
}

BOOST_AUTO_TEST_CASE(dry_run_on_read_command_unsupported)
{
    const ServerConfig config = makeConfig();
    CmdCliCommand command;
    command.command = "link.get";
    command.arg_pairs = {{"slug", "cmddry2"}};
    command.dry_run = true;
    const CmdResult r = run(command, config);

    BOOST_CHECK_EQUAL(r.rc, 2);
    BOOST_CHECK(contains(r.out, "unsupported"));
}

BOOST_AUTO_TEST_CASE(json_from_stdin_is_read)
{
    const ServerConfig config = makeConfig();
    CmdCliCommand command;
    command.command = "link.create";
    command.json_from_stdin = true;
    const CmdResult r = run(
        command, config,
        "{\"target_url\":\"https://example.com/s\",\"slug\":\"cmdstdin1\"}");

    BOOST_CHECK_EQUAL(r.rc, 0);
    BOOST_CHECK(contains(r.out, "\"slug\":\"cmdstdin1\""));
}

BOOST_AUTO_TEST_CASE(oversized_stdin_rejected)
{
    ServerConfig config = makeConfig();
    config.max_request_body_bytes = 16;  // tiny cap for the test

    CmdCliCommand command;
    command.command = "link.create";
    command.json_from_stdin = true;
    const std::string big(100, 'x');  // exceeds the 16-byte cap
    const CmdResult r = run(command, config, big);

    BOOST_CHECK_EQUAL(r.rc, 2);
    BOOST_CHECK(r.out.empty());
    BOOST_CHECK(contains(r.err, "args too large"));
    // The oversized body is never echoed.
    BOOST_CHECK(!contains(r.err, "xxxxxxxx"));
}

BOOST_AUTO_TEST_CASE(raw_failure_empty_stdout_stderr_line)
{
    const ServerConfig config = makeConfig();
    CmdCliCommand command = jsonCreate("cmdrawfail1", "ht!tp://bad");
    command.raw = true;
    const CmdResult r = run(command, config);

    BOOST_CHECK_EQUAL(r.rc, 2);
    BOOST_CHECK(r.out.empty());
    BOOST_CHECK(contains(r.err, "cmd failed: invalid_url"));
}

BOOST_AUTO_TEST_CASE(builtin_commands_succeed)
{
    const ServerConfig config = makeConfig();

    CmdCliCommand list;
    list.command = "command.list";
    const CmdResult l = run(list, config);
    BOOST_CHECK_EQUAL(l.rc, 0);
    BOOST_CHECK(contains(l.out, "\"ok\":true"));

    CmdCliCommand info;
    info.command = "system.info";
    const CmdResult i = run(info, config);
    BOOST_CHECK_EQUAL(i.rc, 0);
    BOOST_CHECK(contains(i.out, "\"ok\":true"));
}

BOOST_AUTO_TEST_CASE(help_prints_descriptor_and_exits_zero)
{
    const ServerConfig config = makeConfig();
    CmdCliCommand command;
    command.command = "link.create";
    command.help = true;
    const CmdResult r = run(command, config);

    BOOST_CHECK_EQUAL(r.rc, 0);
    BOOST_CHECK(contains(r.out, "Usage:"));
    BOOST_CHECK(contains(r.out, "Permission:"));
}

BOOST_AUTO_TEST_CASE(dispatch_never_throws)
{
    const ServerConfig config = makeConfig();
    CmdCliCommand command;
    command.command = "link.create";
    command.json_args = std::string("\x01\x02 not an object");
    std::istringstream in;
    std::ostringstream out;
    std::ostringstream err;
    BOOST_CHECK_NO_THROW(url_shortener::cli::DispatchCmdCommand(
        command, config, in, out, err));
}
