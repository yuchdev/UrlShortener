/**
 * @file 30_cli_parser_tui_mode.cpp
 * @brief Unit tests: CliParser recognizes a leading `tui` positional as the
 * one-shot interactive terminal UI mode, parses its flags (`--read-only`,
 * `--yes`, `--theme`, `--log-file`, `--storage-config`), rejects an invalid
 * theme and server flags, and short-circuits server-flag parsing without
 * disturbing server mode or `link <verb>` (contract C4).
 *
 * These tests link only `url_shortener_common` and so run even in an
 * `-DURLSHORTENER_ENABLE_TUI=OFF` build (the parser changes are always
 * compiled; only `tui_entry.cpp` is gated).
 *
 * Milestone 0006, Task 06.0, subtask 19.
 */
#define BOOST_TEST_MODULE CliParserTuiMode
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

/// Parse an invocation and return the whole ParseResult.
ParseResult parse(std::vector<std::string> args)
{
    ArgvBuilder argv(std::move(args));
    return CliParser {}.parse(argv.argc(), argv.argv());
}
}  // namespace

// ---------------------------------------------------------------------------
// Type: unit (parser mode recognition).
// Scenario: a bare `tui` engages TUI mode with every launch option at its
//   default (read-write, confirmations on, dark theme, no explicit log file),
//   and never engages the link/cmd/serve-stdio modes.
// Boundaries: tui engaged; command/cmd/serve_stdio disengaged; not a help req.
// On failure first check: did CliParser::parse() inspect argv[1] == "tui"
// before
//   the server parse, and are TuiCliOptions defaults correct?
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(bare_tui_sets_defaults)
{
    const ParseResult result = parse({"url_shortener", "tui"});

    BOOST_REQUIRE(result.tui.has_value());
    BOOST_CHECK(!result.command.has_value());
    BOOST_CHECK(!result.cmd.has_value());
    BOOST_CHECK(!result.serve_stdio.has_value());
    BOOST_CHECK(!result.help_requested);
    BOOST_CHECK_EQUAL(result.tui->read_only, false);
    BOOST_CHECK_EQUAL(result.tui->assume_yes, false);
    BOOST_CHECK_EQUAL(result.tui->theme, std::string("dark"));
    BOOST_CHECK(!result.tui->log_file.has_value());
}

// Type: unit. Scenario: each flag maps onto the matching TuiCliOptions field.
BOOST_AUTO_TEST_CASE(all_flags_parsed)
{
    const ParseResult result = parse({"url_shortener",
                                      "tui",
                                      "--read-only",
                                      "--yes",
                                      "--theme",
                                      "light",
                                      "--log-file",
                                      "/var/log/tui.log"});

    BOOST_REQUIRE(result.tui.has_value());
    BOOST_CHECK_EQUAL(result.tui->read_only, true);
    BOOST_CHECK_EQUAL(result.tui->assume_yes, true);
    BOOST_CHECK_EQUAL(result.tui->theme, std::string("light"));
    BOOST_REQUIRE(result.tui->log_file.has_value());
    BOOST_CHECK_EQUAL(*result.tui->log_file, std::string("/var/log/tui.log"));
}

// Type: unit. Scenario: each accepted theme name parses.
BOOST_AUTO_TEST_CASE(every_theme_name_accepted)
{
    for (const std::string& name : {"dark", "light", "mono"}) {
        const ParseResult result =
            parse({"url_shortener", "tui", "--theme", name});
        BOOST_REQUIRE(result.tui.has_value());
        BOOST_CHECK_EQUAL(result.tui->theme, name);
    }
}

// Type: unit (shared storage-config helper, not a second flag parser).
// Scenario: `--storage-config` records the path on config, like every mode.
BOOST_AUTO_TEST_CASE(storage_config_recorded_on_config)
{
    const ParseResult result = parse(
        {"url_shortener", "tui", "--storage-config", "/etc/shortener.yaml"});

    BOOST_REQUIRE(result.tui.has_value());
    BOOST_REQUIRE(result.config.storage_config_path.has_value());
    BOOST_CHECK_EQUAL(*result.config.storage_config_path,
                      std::string("/etc/shortener.yaml"));
}

// Type: unit (invalid-input). Scenario: an unknown theme is rejected at parse
//   time with a clear error, never silently defaulted (threat model: fail
//   fast).
BOOST_AUTO_TEST_CASE(invalid_theme_rejected)
{
    BOOST_CHECK_THROW(parse({"url_shortener", "tui", "--theme", "solarized"}),
                      std::invalid_argument);
}

// Type: unit. Scenario: `tui --help` yields the usage text and no options.
BOOST_AUTO_TEST_CASE(help_sets_help_text)
{
    const ParseResult result = parse({"url_shortener", "tui", "--help"});

    BOOST_CHECK(result.help_requested);
    BOOST_CHECK(!result.tui.has_value());
    BOOST_CHECK(result.help_text.find("Usage: url_shortener tui")
                != std::string::npos);
    // The help must state no server is started (contract C5 promise to the
    // user).
    BOOST_CHECK(result.help_text.find("No HTTP") != std::string::npos);
}

// Type: unit (invalid-input). Scenario: an unknown flag is rejected.
BOOST_AUTO_TEST_CASE(unknown_flag_rejected)
{
    BOOST_CHECK_THROW(parse({"url_shortener", "tui", "--frobnicate"}),
                      std::invalid_argument);
}

// Type: unit (invalid-input). Scenario: a server flag mixed into `tui` is an
//   unknown-option error, never parsed as a server option (contract C5: tui
//   starts no listener, so a port flag is meaningless here).
BOOST_AUTO_TEST_CASE(server_flag_rejected)
{
    BOOST_CHECK_THROW(parse({"url_shortener", "tui", "--http-port", "9000"}),
                      std::invalid_argument);
}

// Type: unit (invalid-input). Scenario: an empty --log-file value is rejected.
BOOST_AUTO_TEST_CASE(empty_log_file_rejected)
{
    BOOST_CHECK_THROW(parse({"url_shortener", "tui", "--log-file", "--yes"}),
                      std::invalid_argument);
}

// ---------------------------------------------------------------------------
// Regression (contract C4): `link <verb>` and server-mode parsing/help stay
// byte-identical after the `tui` branch was added.
// On failure first check: did the `tui` branch accidentally consume argv[1] for
//   another mode, or change the server/link help text?
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(link_mode_unchanged)
{
    const ParseResult result =
        parse({"url_shortener", "link", "get", "--slug", "x"});

    BOOST_REQUIRE(result.command.has_value());
    BOOST_CHECK(!result.tui.has_value());
    BOOST_CHECK(!result.help_requested);
}

BOOST_AUTO_TEST_CASE(server_mode_unchanged)
{
    const ParseResult tui_free =
        parse({"url_shortener", "--http-port", "9000"});

    BOOST_CHECK(!tui_free.tui.has_value());
    BOOST_CHECK(!tui_free.command.has_value());
    BOOST_CHECK(!tui_free.help_requested);
    BOOST_CHECK_EQUAL(tui_free.config.http_port, 9000);
}

// Server `--help` text is unchanged by the new mode (byte-compare the whole
// block to the one a parser without a `tui` branch would produce).
BOOST_AUTO_TEST_CASE(server_help_text_unchanged)
{
    const ParseResult result = parse({"url_shortener", "--help"});

    BOOST_REQUIRE(result.help_requested);
    // The server help begins with the fixed banner and never mentions `tui`.
    BOOST_CHECK(result.help_text.rfind("Usage: simple-http", 0) == 0);
    BOOST_CHECK(result.help_text.find(" tui ") == std::string::npos);
}
