#define BOOST_TEST_MODULE TuiEquivalentCmdTest

#include <string>

#include <boost/test/unit_test.hpp>

#include <url_shortener/commands/envelope.hpp>
#include <url_shortener/tui/equivalent_cmd.hpp>

/**
 * @file 06_equivalent_cmd_test.cpp
 * @brief Unit tests for the `Equivalent:` CLI / envelope renderer (D5 footer).
 *
 * Type: unit (model library, links url_shortener_tui_model).
 * Scenario: the CLI line matches the command catalog's example form; a quote in
 *           the args is single-quote-escaped so it cannot break out; empty args
 *           render as `--json '{}'`; the envelope carries `"v":1`; and a
 *           redact-worthy argument key never has its value echoed (T-125).
 * Boundaries: a value containing a single quote; an empty args object.
 * On failure first check: `shell_single_quote` / `redact_args` in
 *           `src/tui/equivalent_cmd.cpp`.
 */

using url_shortener::tui::equivalent_cli;
using url_shortener::tui::equivalent_envelope;

namespace cmd = url_shortener::commands;

namespace
{

cmd::CommandRequest request(const std::string& command,
                            const std::string& args_json)
{
    cmd::CommandRequest r;
    r.v = 1;
    r.command = command;
    r.args_json = args_json;
    return r;
}

}  // namespace

// ---------------------------------------------------------------------------
// The CLI line equals the command catalog's example form.
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(cli_line_matches_catalog_form)
{
    const cmd::CommandRequest r = request(
        "link.create",
        "{\"target_url\":\"https://example.com/docs\",\"slug\":\"docs\"}");
    BOOST_TEST(equivalent_cli(r) ==
               "url_shortener cmd link.create --json "
               "'{\"target_url\":\"https://example.com/docs\",\"slug\":"
               "\"docs\"}'");
}

// ---------------------------------------------------------------------------
// A single quote in an argument value is escaped as '\'' and cannot break out.
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(single_quote_is_shell_escaped)
{
    const cmd::CommandRequest r =
        request("link.update", "{\"slug\":\"o'brien\"}");
    const std::string line = equivalent_cli(r);

    // The embedded quote is rendered as the POSIX break/escape/reopen sequence.
    BOOST_TEST(line.find("'\\''") != std::string::npos);
    // The line still begins and ends with the single-quoted payload.
    BOOST_TEST(line.find("--json '") != std::string::npos);
    BOOST_TEST(line.back() == '\'');
}

// ---------------------------------------------------------------------------
// Empty args render as --json '{}'.
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(empty_args_render_as_empty_object)
{
    const cmd::CommandRequest r = request("link.list", "{}");
    BOOST_TEST(equivalent_cli(r) == "url_shortener cmd link.list --json '{}'");
}

// ---------------------------------------------------------------------------
// The envelope carries "v":1 and the verbatim args.
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(envelope_carries_version_and_args)
{
    const cmd::CommandRequest r =
        request("link.create", "{\"target_url\":\"https://e.co\"}");
    const std::string env = equivalent_envelope(r);

    BOOST_TEST(env.find("\"v\":1") != std::string::npos);
    BOOST_TEST(env.find("\"command\":\"link.create\"") != std::string::npos);
    BOOST_TEST(env.find("\"target_url\":\"https://e.co\"") !=
               std::string::npos);
}

// ---------------------------------------------------------------------------
// T-125: a redact-worthy argument key never has its value echoed, in either the
// CLI line or the envelope, and the line stays single-quote-safe.
// On failure first check: the key matcher in `is_redact_worthy`.
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(redact_worthy_value_never_echoed)
{
    const cmd::CommandRequest r = request(
        "x.y", "{\"password\":\"s3cret\",\"slug\":\"docs\"}");

    const std::string line = equivalent_cli(r);
    BOOST_TEST(line.find("s3cret") == std::string::npos);
    BOOST_TEST(line.find("\"***\"") != std::string::npos);
    BOOST_TEST(line.find("docs") != std::string::npos);  // non-secret kept
    BOOST_TEST(line.back() == '\'');  // still single-quote-closed

    const std::string env = equivalent_envelope(r);
    BOOST_TEST(env.find("s3cret") == std::string::npos);
    BOOST_TEST(env.find("\"***\"") != std::string::npos);
}

// ---------------------------------------------------------------------------
// T-125 (recursive): a redact-worthy key NESTED in an object is also redacted.
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(redaction_recurses_into_nested_object)
{
    const cmd::CommandRequest r =
        request("x.y", "{\"meta\":{\"api_key\":\"SECRETVAL\"}}");

    const std::string line = equivalent_cli(r);
    BOOST_TEST(line.find("SECRETVAL") == std::string::npos);
    BOOST_TEST(line.find("\"***\"") != std::string::npos);
    BOOST_TEST(line.find("meta") != std::string::npos);  // structure kept
    BOOST_TEST(line.back() == '\'');

    BOOST_TEST(equivalent_envelope(r).find("SECRETVAL") == std::string::npos);
}

// ---------------------------------------------------------------------------
// T-125 (recursive): a secret inside an array-of-objects is redacted while a
// non-secret sibling element is preserved.
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(redaction_recurses_into_array_of_objects)
{
    const cmd::CommandRequest r = request(
        "x.y", "{\"items\":[{\"token\":\"T0PSECRET\"},{\"slug\":\"keep\"}]}");

    const std::string line = equivalent_cli(r);
    BOOST_TEST(line.find("T0PSECRET") == std::string::npos);
    BOOST_TEST(line.find("\"***\"") != std::string::npos);
    BOOST_TEST(line.find("keep") != std::string::npos);  // non-secret kept
}

// ---------------------------------------------------------------------------
// T-125: a mixed-case redact-worthy key is matched case-insensitively.
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(redaction_is_case_insensitive)
{
    const cmd::CommandRequest r =
        request("x.y", "{\"API_KEY\":\"XSECRET\"}");
    BOOST_TEST(equivalent_cli(r).find("XSECRET") == std::string::npos);
    BOOST_TEST(equivalent_cli(r).find("\"***\"") != std::string::npos);
}

// ---------------------------------------------------------------------------
// T-125: nesting past the depth cap fails closed - a secret buried below the
// cap never leaks (the whole over-deep subtree is redacted).
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(deep_nesting_fails_closed)
{
    // Build ~40 levels of {"a":{"a":{...{"api_key":"DEEPSECRET"}...}}}.
    std::string json = "{\"api_key\":\"DEEPSECRET\"}";
    for (int i = 0; i < 40; ++i)
    {
        json = "{\"a\":" + json + "}";
    }
    const cmd::CommandRequest r = request("x.y", json);

    BOOST_TEST(equivalent_cli(r).find("DEEPSECRET") == std::string::npos);
    BOOST_TEST(equivalent_envelope(r).find("DEEPSECRET") == std::string::npos);
}
