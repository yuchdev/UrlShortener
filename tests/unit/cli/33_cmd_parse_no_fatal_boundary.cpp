/**
 * @file 33_cmd_parse_no_fatal_boundary.cpp
 * @brief Exception-boundary unit tests for `url_shortener cmd <...>`: no argv can
 * make the parse layer throw a type the entry point does not already contain, so
 * no malformed `cmd` invocation reaches `main`'s FATAL `unhandled_exception`.
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
 * handler. `DispatchCmdCommand` additionally wraps execution in `catch(...)`, so
 * the dispatch half cannot throw out either.
 *
 * Boundaries:
 *   - Dispatchable (no throw): invalid command name, `--arg foo` (no `=`),
 *     `--arg =v` (empty key), `--arg k=` (empty VALUE, well-formed), valid name.
 *   - Usage error (throws `std::invalid_argument`): missing name, unknown
 *     option, a flag missing its value, repeated `--json`, `--json` with
 *     `--arg`, stray positional, `--log-file` with `--verbose`.
 *
 * On failure first check:
 *   - That `parseCmdCommand` still routes a malformed name / `--arg` through a
 *     marker (no throw) and every remaining usage error throws
 *     `std::invalid_argument` (not a bare `throw` of another type), and that the
 *     `main` `cmd` branch still catches `std::exception` before the FATAL
 *     handler.
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
    return CliParser{}.parse(argv.argc(), argv.argv());
}
}  // namespace

/**
 * [Unit][CLI] A malformed command name or `--arg` operand never throws from the
 * parse layer: it is carried through as a dispatchable `ParseResult::cmd` so the
 * dispatcher can emit the `invalid_envelope` envelope. None of these reaches
 * `main`'s FATAL handler because they do not throw at all.
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
 * and turns into a non-FATAL exit-2 diagnostic. Proving the type is uniform (and
 * never an unhandled non-std type) is what keeps these off the FATAL path.
 */
BOOST_AUTO_TEST_CASE(usage_errors_throw_invalid_argument_only)
{
    const std::vector<std::vector<std::string>> usage_errors = {
        {"url_shortener", "cmd"},                               // missing name
        {"url_shortener", "cmd", "link.get", "--nope"},         // unknown option
        {"url_shortener", "cmd", "link.get", "--json"},         // missing value
        {"url_shortener", "cmd", "link.get", "--json",
         "{\"a\":1}", "--json", "{\"b\":2}"},                   // repeated --json
        {"url_shortener", "cmd", "link.get", "--json",
         "{\"a\":1}", "--arg", "x=y"},                          // json + arg
        {"url_shortener", "cmd", "link.get", "--id"},           // missing value
        {"url_shortener", "cmd", "link.get", "extra"},          // stray positional
        {"url_shortener", "cmd", "link.get", "--log-file", "x",
         "--verbose"},                                          // conflict
    };

    for (const auto& argv : usage_errors) {
        BOOST_CHECK_THROW(parse(argv), std::invalid_argument);
    }
}
