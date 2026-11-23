/**
 * @file 33_cmd_parse_no_fatal_boundary.cpp
 * @brief Exception-boundary unit tests for `url_shortener cmd <...>` and for
 * every other top-level mode: no argv can make the parse layer throw a type the
 * entry point does not already contain, so no malformed invocation reaches
 * `main`'s FATAL `unhandled_exception` handler (exit 1). For non-`cmd` modes
 * the parser classifies usage errors as the dedicated `UsageError` type, which
 * the entry point turns into a concise `error: ...` diagnostic on stderr with
 * exit 2; the carried message is control-sanitized (CWE-117).
 *
 * Type: unit (in-process; drives `CliParser::parse` over crafted argv vectors;
 * no network, no subprocess).
 *
 * Scenario (user ruling): `cmd` must never exit with a FATAL log line on
 * malformed input. The entry point contains every `cmd` failure two ways and
 * this table pins which way each malformed argv takes:
 *   1. the parse layer carries a malformed command NAME or `--arg` operand
 *      through WITHOUT throwing, so the dispatcher emits an `invalid_envelope`
 *      envelope (exit 2); OR
 *   2. a genuine CLI usage error throws `std::invalid_argument` - a
 *      `std::exception` the `main` `cmd` branch catches and reports as a
 *      value-free usage error on stderr with exit 2 (never the FATAL path).
 * Either way the exception never escapes to `main`'s `unhandled_exception`
 * handler. `DispatchCmdCommand` additionally wraps execution in `catch(...)`,
 * so the dispatch half cannot throw out either.
 *
 * Boundaries:
 *   - Dispatchable (no throw): invalid command name, `--arg foo` (no `=`),
 *     `--arg =v` (empty key), `--arg k=` (empty VALUE, well-formed), valid
 * name.
 *   - Usage error (throws `std::invalid_argument`): missing name, unknown
 *     option, a flag missing its value, repeated `--json`, `--json` with
 *     `--arg`, stray positional, `--log-file` with `--verbose`, an over-length
 *     `--id` value (65 bytes, past the 64-byte request-id cap).
 *   - Output-injection boundary: an unknown-option NAME carrying control bytes
 *     (LF, CR, ESC, a C0 byte) still throws `std::invalid_argument`, and the
 *     echoed diagnostic is scrubbed to printable ASCII so no byte can forge a
 *     log line or terminal escape at the `main` stderr boundary (CWE-117).
 *
 * On failure first check:
 *   - That `parseCmdCommand` still routes a malformed name / `--arg` through a
 *     marker (no throw) and every remaining usage error throws
 *     `std::invalid_argument` (not a bare `throw` of another type), and that
 * the `main` `cmd` branch still catches `std::exception` before the FATAL
 *     handler.
 *   - If a control byte survives into `what()`, check `sanitizeEchoedOption` in
 *     `src/cli_parser.cpp` is still applied to the unknown-option stem at every
 *     throw site (cmd/serve-stdio/tui).
 */
#define BOOST_TEST_MODULE CliCmdParseNoFatalBoundary
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include <boost/test/unit_test.hpp>
#include <url_shortener/cli/cli_parser.hpp>

namespace
{
/// Build a mutable argv from strings for CliParser::parse() (needs char*[]).
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

ParseResult parse(std::vector<std::string> args)
{
    ArgvBuilder argv(std::move(args));
    return CliParser {}.parse(argv.argc(), argv.argv());
}
}  // namespace

/**
 * [Unit][CLI] A malformed command name or `--arg` operand never throws from the
 * parse layer: it is carried through as a dispatchable `ParseResult::cmd` so
 * the dispatcher can emit the `invalid_envelope` envelope. None of these
 * reaches `main`'s FATAL handler because they do not throw at all.
 */
BOOST_AUTO_TEST_CASE(name_and_arg_faults_are_dispatchable_not_thrown)
{
    // Invalid command name - carried verbatim.
    {
        const ParseResult r = parse({"url_shortener", "cmd", "LINK.CREATE"});
        BOOST_REQUIRE(r.cmd.has_value());
        BOOST_CHECK(!r.cmd->malformed_arg);
        BOOST_CHECK_EQUAL(r.cmd->command, std::string("LINK.CREATE"));
    }
    // `--arg foo` (no `=`) - malformed_arg marker set, operand dropped.
    {
        const ParseResult r =
            parse({"url_shortener", "cmd", "link.get", "--arg", "foo"});
        BOOST_REQUIRE(r.cmd.has_value());
        BOOST_CHECK(r.cmd->malformed_arg);
        BOOST_CHECK(r.cmd->arg_pairs.empty());
    }
    // `--arg =v` (empty key) - malformed_arg marker set.
    {
        const ParseResult r =
            parse({"url_shortener", "cmd", "link.get", "--arg", "=v"});
        BOOST_REQUIRE(r.cmd.has_value());
        BOOST_CHECK(r.cmd->malformed_arg);
        BOOST_CHECK(r.cmd->arg_pairs.empty());
    }
    // `--arg k=` (empty VALUE) - well-formed, NOT a malformed operand.
    {
        const ParseResult r =
            parse({"url_shortener", "cmd", "link.get", "--arg", "k="});
        BOOST_REQUIRE(r.cmd.has_value());
        BOOST_CHECK(!r.cmd->malformed_arg);
        BOOST_REQUIRE_EQUAL(r.cmd->arg_pairs.size(), 1u);
        BOOST_CHECK_EQUAL(r.cmd->arg_pairs.front().first, std::string("k"));
        BOOST_CHECK(r.cmd->arg_pairs.front().second.empty());
    }
    // A fully valid invocation, for contrast.
    {
        const ParseResult r = parse({"url_shortener", "cmd", "link.get"});
        BOOST_REQUIRE(r.cmd.has_value());
        BOOST_CHECK(!r.cmd->malformed_arg);
    }
}

/**
 * [Unit][CLI] Every remaining `cmd` usage error throws exactly
 * `std::invalid_argument` - a `std::exception` the `main` `cmd` branch catches
 * and turns into a non-FATAL exit-2 diagnostic. Proving the type is uniform
 * (and never an unhandled non-std type) is what keeps these off the FATAL path.
 */
BOOST_AUTO_TEST_CASE(usage_errors_throw_invalid_argument_only)
{
    const std::vector<std::vector<std::string>> usage_errors = {
        {"url_shortener", "cmd"},  // missing name
        {"url_shortener", "cmd", "link.get", "--nope"},  // unknown option
        {"url_shortener", "cmd", "link.get", "--json"},  // missing value
        {"url_shortener",
         "cmd",
         "link.get",
         "--json",
         "{\"a\":1}",
         "--json",
         "{\"b\":2}"},  // repeated --json
        {"url_shortener",
         "cmd",
         "link.get",
         "--json",
         "{\"a\":1}",
         "--arg",
         "x=y"},  // json + arg
        {"url_shortener", "cmd", "link.get", "--id"},  // missing value
        {"url_shortener", "cmd", "link.get", "extra"},  // stray positional
        {"url_shortener",
         "cmd",
         "link.get",
         "--log-file",
         "x",
         "--verbose"},  // conflict
    };

    for (const auto& argv : usage_errors) {
        BOOST_CHECK_THROW(parse(argv), std::invalid_argument);
    }
}

/**
 * [Unit][CLI] An unknown `cmd` option whose NAME carries control bytes still
 * throws `std::invalid_argument`, and the diagnostic the `main` boundary echoes
 * to stderr is scrubbed: no CR, LF, ESC or other control byte survives into
 * `what()`, so a crafted option name cannot forge a log line or inject a
 * terminal escape sequence (CWE-117). An over-length `--id` value is pinned
 * here too, as a usage error that likewise never reaches the FATAL path.
 */
BOOST_AUTO_TEST_CASE(unknown_option_name_is_control_sanitized_in_message)
{
    // An over-length --id (the default request-id cap is 64 bytes) is a usage
    // error, not a dispatchable envelope and not a FATAL crash.
    BOOST_CHECK_THROW(
        parse(
            {"url_shortener", "cmd", "link.get", "--id", std::string(65, 'a')}),
        std::invalid_argument);

    // An unknown option whose stem embeds LF, CR, ESC and a C0 control byte -
    // the exact CWE-117 injection vector from the 0006 08.0 review.
    const std::string hostile = std::string("--evil") + '\n' + "FATAL injected"
        + '\r' + '\x1b' + "[31m" + '\x01' + "x";
    bool threw = false;
    try {
        parse({"url_shortener", "cmd", "link.get", hostile});
    } catch (const std::invalid_argument& e) {
        threw = true;
        const std::string what = e.what();
        // No raw control byte survives into the echoed diagnostic.
        BOOST_CHECK(what.find('\n') == std::string::npos);
        BOOST_CHECK(what.find('\r') == std::string::npos);
        BOOST_CHECK(what.find('\x1b') == std::string::npos);
        BOOST_CHECK(what.find('\x01') == std::string::npos);
        // The scrubbed stem is still a recognizable, useful diagnostic.
        BOOST_CHECK(what.find("Unknown cmd option: ") != std::string::npos);
    }
    BOOST_CHECK(threw);
}

/**
 * [Unit][CLI] A usage error in any NON-`cmd` top-level mode (server, `link`,
 * `serve-stdio`, `tui`) is classified as the dedicated `UsageError` type, which
 * the `main` boundary turns into a concise `error: ...` + `--help` hint on
 * stderr with exit 2 - never the FATAL `unhandled_exception` path (exit 1).
 * `UsageError` derives from `std::invalid_argument`, so the legacy base-type
 * expectation (e.g. in 01_cli_parser_link_commands.cpp) still holds. This is
 * the generalisation of the `cmd` boundary to every mode: an unknown
 * subcommand, an unknown `link` verb, a missing verb, an unknown flag, and a
 * flag missing its value all take the usage path.
 *
 * On failure first check: that `CliParser::parse` still wraps a parse-layer
 * `std::invalid_argument`/`std::out_of_range` into `UsageError`, and that no
 * mode escapes that wrapper (e.g. a sub-parser throwing a non-std exception).
 */
BOOST_AUTO_TEST_CASE(top_level_usage_errors_classified_as_usage_error)
{
    const std::vector<std::vector<std::string>> usage_errors = {
        {"url_shortener", "nonsense"},  // unknown subcommand
        {"url_shortener", "link", "bogus"},  // unknown link verb
        {"url_shortener", "link"},  // missing link verb
        {"url_shortener", "--bogus-flag"},  // unknown server flag
        {"url_shortener", "--http-port"},  // flag missing its value
        {"url_shortener", "link", "get", "--bogus"},  // unknown link flag
        {"url_shortener",
         "serve-stdio",
         "--bogus"},  // unknown serve-stdio flag
        {"url_shortener", "tui", "--bogus"},  // unknown tui flag
    };

    for (const auto& argv : usage_errors) {
        BOOST_CHECK_THROW(parse(argv), UsageError);
        // Still observable through the base type legacy callers/tests catch.
        BOOST_CHECK_THROW(parse(argv), std::invalid_argument);
    }
}

/**
 * [Unit][CLI] A non-`cmd` usage error whose message embeds an argv-derived
 * token carrying control bytes - here an unknown SERVER flag, whose raw name
 * Boost.ProgramOptions interpolates into its `what()` with no scrubbing of its
 * own - is control-sanitized before it leaves `CliParser::parse`: no CR, LF,
 * ESC or other control byte survives into `UsageError::what()`, so the `error:
 * ...` line the entry point echoes to stderr cannot forge a log line or inject
 * a terminal escape sequence (CWE-117). This closes the injection vector for
 * the modes whose diagnostics are NOT individually sanitized at the throw site
 * the way the cmd/serve-stdio/tui unknown-option stems are.
 *
 * On failure first check: that `CliParser::parse`'s wrapper runs the caught
 * message through `sanitizeDiagnostic` in `src/cli_parser.cpp` before
 * re-throwing it as a `UsageError`.
 */
BOOST_AUTO_TEST_CASE(top_level_usage_error_message_is_control_sanitized)
{
    // An unknown server flag whose name embeds LF/CR/ESC and a C0 control byte
    // - the CWE-117 injection vector generalised beyond the cmd sub-parser.
    const std::string hostile = std::string("--evil") + '\n' + "FATAL injected"
        + '\r' + '\x1b' + "[31m" + '\x01' + "x";
    bool threw = false;
    try {
        parse({"url_shortener", hostile});
    } catch (const UsageError& e) {
        threw = true;
        const std::string what = e.what();
        BOOST_CHECK(what.find('\n') == std::string::npos);
        BOOST_CHECK(what.find('\r') == std::string::npos);
        BOOST_CHECK(what.find('\x1b') == std::string::npos);
        BOOST_CHECK(what.find('\x01') == std::string::npos);
    }
    BOOST_CHECK(threw);
}
