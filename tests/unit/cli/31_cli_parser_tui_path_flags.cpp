/**
 * @file 31_cli_parser_tui_path_flags.cpp
 * @brief Unit tests: `CliParser` parses the `tui` subcommand's new
 * `--path-root` / `--no-file-paths` flags (Milestone 0006, Task 07.0 subtask
 * 20, user ruling (a)).
 *
 * The flags reach parity with `serve-stdio`: `--path-root` is routed through
 * the **shared** `normalize_path_root`, so `--path-root /foo/` normalizes
 * identically to `--path-root /foo` (trailing-separator strip, separator-run
 * collapse) and `.`/`..`/`~`/non-absolute values are rejected; the two flags
 * are mutually exclusive. The trailing-slash normalization is what defeats the
 * `/foo` vs `/foobar` string-prefix trap at the component-wise root check in
 * `pair_file_access` (threat model T-96); the confinement itself is proven at
 * that layer (`tests/unit/commands/78_path_argument_policy_test.cpp`).
 *
 * These tests link only `url_shortener_common` and so run even in an
 * `-DURLSHORTENER_ENABLE_TUI=OFF` build (the parser changes are always
 * compiled; only `tui_entry.cpp` is gated). They pair with
 * `30_cli_parser_tui_mode.cpp`.
 */
#define BOOST_TEST_MODULE CliParserTuiPathFlags
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
/// CliParser takes `char* argv[]`, so the backing strings must be non-const and
/// outlive the parse call.
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
// Type: unit (normal). Scenario: with neither flag the local-operator default
//   is preserved - `no_file_paths` is false and `path_root` is unset, so
//   run_tui keeps full-filesystem access.
// Boundaries: defaults of the two new fields.
// On failure first check: did a default flip when the fields were added?
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(defaults_preserve_full_filesystem)
{
    const ParseResult result = parse({"url_shortener", "tui"});

    BOOST_REQUIRE(result.tui.has_value());
    BOOST_CHECK_EQUAL(result.tui->no_file_paths, false);
    BOOST_CHECK(!result.tui->path_root.has_value());
}

// ---------------------------------------------------------------------------
// Type: unit (normal). Scenario: `--no-file-paths` sets the field; no root.
// On failure first check: did parseTuiCommand lose the `--no-file-paths` arm?
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(no_file_paths_flag_parsed)
{
    const ParseResult result =
        parse({"url_shortener", "tui", "--no-file-paths"});

    BOOST_REQUIRE(result.tui.has_value());
    BOOST_CHECK_EQUAL(result.tui->no_file_paths, true);
    BOOST_CHECK(!result.tui->path_root.has_value());
}

// ---------------------------------------------------------------------------
// Type: unit (normal, prefix-trap defence). Scenario: `--path-root /foo/`
//   normalizes to exactly `/foo` - byte-identical to `--path-root /foo` - so
//   the trailing separator cannot defeat the component-wise root check, and a
//   sibling like `/foobar` stays a distinct root string (the string-prefix
//   trap is not reachable). Separator runs collapse too (`//srv//r/`).
// Boundaries: trailing-slash strip; `/foo` != `/foobar`; separator-run
//   collapse; the normalizer is the SAME one serve-stdio uses.
// On failure first check: did `tui` grow a second copy of normalize_path_root,
//   or stop stripping the trailing separator?
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(path_root_trailing_slash_normalizes_like_serve_stdio)
{
    const ParseResult slash =
        parse({"url_shortener", "tui", "--path-root", "/foo/"});
    const ParseResult plain =
        parse({"url_shortener", "tui", "--path-root", "/foo"});

    BOOST_REQUIRE(slash.tui.has_value());
    BOOST_REQUIRE(plain.tui.has_value());
    BOOST_REQUIRE(slash.tui->path_root.has_value());
    BOOST_REQUIRE(plain.tui->path_root.has_value());

    // Trailing slash stripped and identical to the no-slash form.
    BOOST_CHECK_EQUAL(*slash.tui->path_root, std::string("/foo"));
    BOOST_CHECK_EQUAL(*slash.tui->path_root, *plain.tui->path_root);
    BOOST_CHECK_EQUAL(slash.tui->no_file_paths, false);

    // The string-prefix trap: `/foobar` is a *different* normalized root, so a
    // component-wise check can never treat it as inside `/foo`.
    const ParseResult sibling =
        parse({"url_shortener", "tui", "--path-root", "/foobar/"});
    BOOST_REQUIRE(sibling.tui.has_value());
    BOOST_REQUIRE(sibling.tui->path_root.has_value());
    BOOST_CHECK_EQUAL(*sibling.tui->path_root, std::string("/foobar"));
    BOOST_CHECK(*sibling.tui->path_root != *plain.tui->path_root);

    // Separator runs collapse, exactly as serve-stdio normalizes them.
    const ParseResult runs =
        parse({"url_shortener", "tui", "--path-root", "//srv//r/"});
    BOOST_REQUIRE(runs.tui.has_value());
    BOOST_REQUIRE(runs.tui->path_root.has_value());
    BOOST_CHECK_EQUAL(*runs.tui->path_root, std::string("/srv/r"));
}

// ---------------------------------------------------------------------------
// Type: unit (invalid-input). Scenario: a `.`/`..`/`~`/relative/empty
//   `--path-root` is rejected at parse time, never silently widening the jail.
// Boundaries: the shared normalizer's rejection set.
// On failure first check: did `--path-root` stop calling normalize_path_root?
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(path_root_rejects_dot_dotdot_tilde_relative_empty)
{
    for (const std::string& bad : {std::string("relative/dir"),
                                   std::string("/a/../b"),
                                   std::string("/a/./b"),
                                   std::string("~/dir"),
                                   std::string("~")})
    {
        BOOST_CHECK_THROW(parse({"url_shortener", "tui", "--path-root", bad}),
                          std::invalid_argument);
    }

    // Empty value (the next token consumed as the operand is itself empty).
    BOOST_CHECK_THROW(parse({"url_shortener", "tui", "--path-root", ""}),
                      std::invalid_argument);

    // Missing operand (flag is the last token).
    BOOST_CHECK_THROW(parse({"url_shortener", "tui", "--path-root"}),
                      std::invalid_argument);
}

// ---------------------------------------------------------------------------
// Type: unit (invalid-input). Scenario: `--no-file-paths` and `--path-root`
//   together are rejected (parity with parse_serve_stdio), in either order.
// Boundaries: confining and disabling `path` at once is contradictory.
// On failure first check: did the mutual-exclusion check get dropped or run
//   before the second flag was parsed?
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(path_root_and_no_file_paths_mutually_exclusive)
{
    BOOST_CHECK_THROW(parse({"url_shortener",
                             "tui",
                             "--no-file-paths",
                             "--path-root",
                             "/srv/root"}),
                      std::invalid_argument);
    BOOST_CHECK_THROW(parse({"url_shortener",
                             "tui",
                             "--path-root",
                             "/srv/root",
                             "--no-file-paths"}),
                      std::invalid_argument);
}

// ---------------------------------------------------------------------------
// Regression: the new flags coexist with the existing `tui` options and with
// `--storage-config`, and do not disturb server/link modes.
// On failure first check: did the new arms consume a sibling flag's token?
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(path_root_coexists_with_other_tui_flags)
{
    const ParseResult result = parse({"url_shortener",
                                      "tui",
                                      "--read-only",
                                      "--path-root",
                                      "/srv/root",
                                      "--theme",
                                      "mono",
                                      "--storage-config",
                                      "/etc/shortener.yaml"});

    BOOST_REQUIRE(result.tui.has_value());
    BOOST_CHECK_EQUAL(result.tui->read_only, true);
    BOOST_REQUIRE(result.tui->path_root.has_value());
    BOOST_CHECK_EQUAL(*result.tui->path_root, std::string("/srv/root"));
    BOOST_CHECK_EQUAL(result.tui->theme, std::string("mono"));
    BOOST_REQUIRE(result.config.storage_config_path.has_value());
    BOOST_CHECK_EQUAL(*result.config.storage_config_path,
                      std::string("/etc/shortener.yaml"));
}

// ---------------------------------------------------------------------------
// Type: unit. Scenario: `tui --help` enumerates both new flags so the operator
//   can discover them (contract: help documents every flag).
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(help_text_documents_the_new_flags)
{
    const ParseResult result = parse({"url_shortener", "tui", "--help"});

    BOOST_REQUIRE(result.help_requested);
    BOOST_CHECK(result.help_text.find("--no-file-paths") != std::string::npos);
    BOOST_CHECK(result.help_text.find("--path-root") != std::string::npos);
}
