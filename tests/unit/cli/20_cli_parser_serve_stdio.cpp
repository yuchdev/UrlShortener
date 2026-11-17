/**
 * @file 20_cli_parser_serve_stdio.cpp
 * @brief Unit tests: CliParser recognizes a leading `serve-stdio` positional as
 * the one-shot NDJSON stdio transport mode, parses its hardening flags
 * (`--read-only`, `--no-file-paths`, `--path-root`), normalizes a trailing
 * separator off `--path-root`, and short-circuits server-flag parsing without
 * disturbing server mode, `link <verb>` or the command-API gate.
 *
 * Milestone 0006, Task 05.0, subtask 16 (threat model T-102/T-107).
 */
#define BOOST_TEST_MODULE CliParserServeStdio
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

/// Parse an invocation and return the whole ParseResult.
ParseResult parse(std::vector<std::string> args)
{
    ArgvBuilder argv(std::move(args));
    return CliParser{}.parse(argv.argc(), argv.argv());
}
}  // namespace

/**
 * [Unit][CLI] A bare `serve-stdio` engages stdio mode with all three hardening
 * options at their secure-neutral defaults (admin, file paths allowed, no
 * root), and never engages the `link`/`cmd` modes.
 *
 * If this breaks, first check:
 *   - CliParser::parse() inspects argv[1] == "serve-stdio" before server parse.
 *   - ServeStdioCliOptions defaults are false/false/nullopt.
 */
BOOST_AUTO_TEST_CASE(bare_serve_stdio_sets_defaults)
{
    const ParseResult result = parse({"url_shortener", "serve-stdio"});

    BOOST_REQUIRE(result.serve_stdio.has_value());
    BOOST_CHECK(!result.command.has_value());
    BOOST_CHECK(!result.cmd.has_value());
    BOOST_CHECK(!result.help_requested);
    BOOST_CHECK_EQUAL(result.serve_stdio->read_only, false);
    BOOST_CHECK_EQUAL(result.serve_stdio->no_file_paths, false);
    BOOST_CHECK(!result.serve_stdio->path_root.has_value());
}

/**
 * [Unit][CLI] `--read-only` downgrades the local session (threat model T-107).
 */
BOOST_AUTO_TEST_CASE(read_only_flag)
{
    const ParseResult result =
        parse({"url_shortener", "serve-stdio", "--read-only"});

    BOOST_REQUIRE(result.serve_stdio.has_value());
    BOOST_CHECK_EQUAL(result.serve_stdio->read_only, true);
    BOOST_CHECK_EQUAL(result.serve_stdio->no_file_paths, false);
}

/**
 * [Unit][CLI] `--no-file-paths` disables `path` arguments (threat model T-107).
 */
BOOST_AUTO_TEST_CASE(no_file_paths_flag)
{
    const ParseResult result =
        parse({"url_shortener", "serve-stdio", "--no-file-paths"});

    BOOST_REQUIRE(result.serve_stdio.has_value());
    BOOST_CHECK_EQUAL(result.serve_stdio->no_file_paths, true);
    BOOST_CHECK(!result.serve_stdio->path_root.has_value());
}

/**
 * [Unit][CLI] `--path-root <dir>` confines `path` to the given directory; the
 * value is stored verbatim when it carries no trailing separator.
 */
BOOST_AUTO_TEST_CASE(path_root_flag_stores_value)
{
    const ParseResult result =
        parse({"url_shortener", "serve-stdio", "--path-root", "/tmp/x"});

    BOOST_REQUIRE(result.serve_stdio.has_value());
    BOOST_REQUIRE(result.serve_stdio->path_root.has_value());
    BOOST_CHECK_EQUAL(*result.serve_stdio->path_root, std::string("/tmp/x"));
}

/**
 * [Unit][CLI] A trailing separator is stripped from `--path-root` so the
 * lexical root check in `pair_file_access` matches in-root paths (carry-forward
 * owned by subtask 16): `"/srv/r/"` normalizes to `"/srv/r"`.
 *
 * If this breaks, first check:
 *   - parse_serve_stdio calls normalize_path_root on the operand.
 *   - normalize_path_root pops trailing '/' while size() > 1.
 */
BOOST_AUTO_TEST_CASE(path_root_strips_trailing_separator)
{
    const ParseResult result =
        parse({"url_shortener", "serve-stdio", "--path-root", "/srv/r/"});

    BOOST_REQUIRE(result.serve_stdio.has_value());
    BOOST_REQUIRE(result.serve_stdio->path_root.has_value());
    BOOST_CHECK_EQUAL(*result.serve_stdio->path_root, std::string("/srv/r"));
}

/**
 * [Unit][CLI] A lone `"/"` root is preserved (it is its own trailing
 * separator and must not collapse to an empty, meaningless root).
 */
BOOST_AUTO_TEST_CASE(path_root_root_slash_is_preserved)
{
    const ParseResult result =
        parse({"url_shortener", "serve-stdio", "--path-root", "/"});

    BOOST_REQUIRE(result.serve_stdio.has_value());
    BOOST_REQUIRE(result.serve_stdio->path_root.has_value());
    BOOST_CHECK_EQUAL(*result.serve_stdio->path_root, std::string("/"));
}

/**
 * [Unit][CLI] `--path-root` with no following value is a usage error.
 */
BOOST_AUTO_TEST_CASE(path_root_without_value_throws)
{
    BOOST_CHECK_THROW(
        parse({"url_shortener", "serve-stdio", "--path-root"}),
        std::invalid_argument);
}

/**
 * [Unit][CLI] `--path-root` and `--no-file-paths` are mutually exclusive:
 * confining and disabling `path` at once is contradictory (threat model T-107).
 */
BOOST_AUTO_TEST_CASE(path_root_with_no_file_paths_throws)
{
    BOOST_CHECK_THROW(
        parse({"url_shortener", "serve-stdio",
            "--path-root", "/tmp/x", "--no-file-paths"}),
        std::invalid_argument);
    BOOST_CHECK_THROW(
        parse({"url_shortener", "serve-stdio",
            "--no-file-paths", "--path-root", "/tmp/x"}),
        std::invalid_argument);
}

/**
 * [Unit][CLI] `serve-stdio --help` and `serve-stdio -h` request help, emit the
 * stdio usage text (NDJSON framing, EOF, stderr-only logs, the three hardening
 * flags and the transport guide), and parse no options.
 */
BOOST_AUTO_TEST_CASE(help_prints_usage)
{
    for (const std::string flag : {"--help", "-h"}) {
        const ParseResult result =
            parse({"url_shortener", "serve-stdio", flag});

        BOOST_REQUIRE(result.help_requested);
        BOOST_CHECK(!result.serve_stdio.has_value());
        const std::string& help = result.help_text;
        BOOST_CHECK(help.find("serve-stdio") != std::string::npos);
        BOOST_CHECK(help.find("NDJSON") != std::string::npos);
        BOOST_CHECK(help.find("EOF") != std::string::npos);
        BOOST_CHECK(help.find("stderr") != std::string::npos);
        BOOST_CHECK(help.find("--read-only") != std::string::npos);
        BOOST_CHECK(help.find("--no-file-paths") != std::string::npos);
        BOOST_CHECK(help.find("--path-root") != std::string::npos);
        BOOST_CHECK(
            help.find("adding-a-transport.md") != std::string::npos);
    }
}

/**
 * [Unit][CLI] An unknown stdio flag is rejected; the error echoes only the flag
 * stem before any '=' and never an operand value (threat model T-77).
 */
BOOST_AUTO_TEST_CASE(unknown_flag_throws)
{
    BOOST_CHECK_THROW(
        parse({"url_shortener", "serve-stdio", "--nope"}),
        std::invalid_argument);
    // Logging flags belong to `cmd`, not `serve-stdio`: they cannot route logs
    // to stdout here and are simply unknown (threat model T-102).
    BOOST_CHECK_THROW(
        parse({"url_shortener", "serve-stdio", "--verbose"}),
        std::invalid_argument);
    BOOST_CHECK_THROW(
        parse({"url_shortener", "serve-stdio", "--log-file", "/tmp/x.log"}),
        std::invalid_argument);
}

/**
 * [Unit][CLI] A non-flag positional after `serve-stdio` is a usage error.
 */
BOOST_AUTO_TEST_CASE(extra_positional_throws)
{
    BOOST_CHECK_THROW(
        parse({"url_shortener", "serve-stdio", "extra"}),
        std::invalid_argument);
}

/**
 * [Unit][CLI] `serve-stdio` is recognized only as argv[1]: `8080 serve-stdio`
 * stays server mode (a port positional followed by an unexpected token), never
 * the stdio mode.
 *
 * If this breaks, first check:
 *   - The `serve-stdio` branch guards on argv[1], not any position.
 */
BOOST_AUTO_TEST_CASE(serve_stdio_not_first_is_not_the_mode)
{
    // `8080 serve-stdio` parses as a server-mode invocation where the extra
    // `serve-stdio` positional is rejected by Boost.ProgramOptions; either way
    // it never engages stdio mode. Boost reports the extra positional as an
    // error, so assert it does not become a stdio invocation.
    BOOST_CHECK_THROW(
        parse({"url_shortener", "8080", "serve-stdio"}),
        std::invalid_argument);
}

/**
 * [Unit][CLI] Regression: plain server mode (`url_shortener 8080`) and
 * `link get` parse exactly as before and never engage `serve_stdio`.
 */
BOOST_AUTO_TEST_CASE(server_and_link_modes_unchanged)
{
    const ParseResult server = parse({"url_shortener", "8080"});
    BOOST_CHECK(!server.serve_stdio.has_value());
    BOOST_CHECK(!server.command.has_value());
    BOOST_CHECK(!server.cmd.has_value());
    BOOST_CHECK_EQUAL(server.config.http_port, 8080);

    const ParseResult link =
        parse({"url_shortener", "link", "get", "--slug", "abc"});
    BOOST_REQUIRE(link.command.has_value());
    BOOST_CHECK(link.command->verb == LinkCliVerb::get);
    BOOST_CHECK(!link.serve_stdio.has_value());
}

/**
 * [Unit][CLI] `serve-stdio` applies the shared global config flags the same way
 * the `link`/`cmd` paths do: `--base-domain` and `--storage-config` land on
 * `ParseResult::config` without being treated as unknown stdio flags.
 */
BOOST_AUTO_TEST_CASE(global_config_flags_apply_to_config)
{
    const ParseResult result = parse({"url_shortener", "serve-stdio",
        "--read-only",
        "--base-domain", "https://sho.rt",
        "--storage-config", "/etc/url_shortener/storage.yaml"});

    BOOST_REQUIRE(result.serve_stdio.has_value());
    BOOST_CHECK_EQUAL(result.serve_stdio->read_only, true);
    BOOST_CHECK_EQUAL(
        result.config.shortener_base_domain, std::string("https://sho.rt"));
    BOOST_REQUIRE(result.config.storage_config_path.has_value());
    BOOST_CHECK_EQUAL(*result.config.storage_config_path,
        std::string("/etc/url_shortener/storage.yaml"));
}

/**
 * [Unit][CLI] The `serve-stdio` short-circuit, like `cmd`, never touches the
 * command-API gate: it engages `ParseResult::serve_stdio` (not server mode) and
 * leaves the gate at its secure defaults (off / user). Deferred check from Task
 * 04.0 subtask 08, mirroring `cmd_short_circuit_is_undisturbed`.
 *
 * If this breaks, first check:
 *   - The `argv[1] == "serve-stdio"` branch returns before server-flag parsing.
 *   - ServerConfig initialises command_api_enabled=false, role="user".
 */
BOOST_AUTO_TEST_CASE(serve_stdio_short_circuit_is_undisturbed)
{
    const ParseResult result = parse({"url_shortener", "serve-stdio"});

    BOOST_REQUIRE(result.serve_stdio.has_value());
    BOOST_CHECK(!result.command.has_value());
    BOOST_CHECK(!result.cmd.has_value());
    BOOST_CHECK_EQUAL(result.config.command_api_enabled, false);
    BOOST_CHECK_EQUAL(
        result.config.command_api_role, std::string("user"));
}

/**
 * [Unit][CLI] `serve_stdio` stays empty for a `link <verb>` invocation (the two
 * modes are mutually exclusive).
 */
BOOST_AUTO_TEST_CASE(serve_stdio_empty_for_link)
{
    const ParseResult result =
        parse({"url_shortener", "link", "create", "--url",
            "https://example.com/path"});

    BOOST_REQUIRE(result.command.has_value());
    BOOST_CHECK(!result.serve_stdio.has_value());
}
