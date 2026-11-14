/**
 * @file 05_cli_storage_config_flag.cpp
 * @brief Unit tests: the global `--storage-config FILE` flag and its
 * `URL_SHORTENER_STORAGE_CONFIG` env fallback are recorded verbatim in
 * `ServerConfig::storage_config_path` for both server mode and `link <verb>`
 * mode, with no filesystem I/O at parse time and flag-over-env precedence.
 *
 * Milestone 0006, Task 02.0, subtask 10.
 */
#define BOOST_TEST_MODULE CliStorageConfigFlag
#include <boost/test/unit_test.hpp>

#include <cstdlib>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

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

/// RAII guard that sets an environment variable for the duration of a test and
/// restores the previous value (or unsets it) on destruction.
///
/// Uses `setenv`/`unsetenv` on POSIX and `_putenv_s` on Windows (which has no
/// `setenv`), so the env-fallback tests stay portable and leave no global
/// state behind that could leak into sibling tests.
class ScopedEnv
{
public:
    ScopedEnv(std::string name, const std::string& value)
        : m_name_(std::move(name))
    {
        if (const char* prev = std::getenv(m_name_.c_str()); prev != nullptr) {
            m_had_prev_ = true;
            m_prev_ = prev;
        }
        set(m_name_, value);
    }

    ~ScopedEnv()
    {
        if (m_had_prev_) {
            set(m_name_, m_prev_);
        }
        else {
            unset(m_name_);
        }
    }

    ScopedEnv(const ScopedEnv&) = delete;
    ScopedEnv& operator=(const ScopedEnv&) = delete;

private:
    static void set(const std::string& name, const std::string& value)
    {
#ifdef _WIN32
        _putenv_s(name.c_str(), value.c_str());
#else
        ::setenv(name.c_str(), value.c_str(), 1);
#endif
    }

    static void unset(const std::string& name)
    {
#ifdef _WIN32
        _putenv_s(name.c_str(), "");
#else
        ::unsetenv(name.c_str());
#endif
    }

    std::string m_name_;
    bool m_had_prev_ = false;
    std::string m_prev_;
};

/// Name of the storage-config environment variable, kept in one place so a
/// rename shows up as a single-line diff.
const char* const kStorageEnvVar = "URL_SHORTENER_STORAGE_CONFIG";

/// Parse @p tokens with the env var cleared and return the resulting
/// storage_config_path, so the result reflects only @p tokens.
std::optional<std::string> parsePath(std::vector<std::string> tokens)
{
    ::unsetenv(kStorageEnvVar);
    ArgvBuilder args(std::move(tokens));
    return CliParser{}.parse(args.argc(), args.argv()).config.storage_config_path;
}

}  // namespace

// ---------------------------------------------------------------------------
// Server mode: flag and absence
// ---------------------------------------------------------------------------

/**
 * [Unit][CLI] Server mode records `--storage-config FILE` verbatim and leaves
 * the path unset when the flag is absent.
 *
 * Scenario: default (no flag) must stay byte-for-byte the legacy in-memory
 * path (contract C4); the flag must select a config file.
 * Boundaries: present vs absent flag.
 * On failure first check: the "Storage options" po group is registered in both
 * `all` and `visible`, and the post-notify handler copies vm["storage-config"].
 */
BOOST_AUTO_TEST_CASE(server_mode_records_flag_and_defaults_to_nullopt)
{
    const auto absent = parsePath({"url_shortener"});
    BOOST_CHECK(!absent.has_value());

    const auto present =
        parsePath({"url_shortener", "--storage-config", "/x.yaml"});
    BOOST_REQUIRE(present.has_value());
    BOOST_CHECK_EQUAL(*present, "/x.yaml");
}

/**
 * [Unit][CLI] Server mode accepts the `--storage-config=FILE` joined spelling.
 *
 * On failure first check: po registers the option with a value, so both the
 * split and joined forms resolve identically.
 */
BOOST_AUTO_TEST_CASE(server_mode_accepts_joined_spelling)
{
    const auto path =
        parsePath({"url_shortener", "--storage-config=/etc/url/storage.yaml"});
    BOOST_REQUIRE(path.has_value());
    BOOST_CHECK_EQUAL(*path, "/etc/url/storage.yaml");
}

/**
 * [Unit][CLI] A Windows path (drive letter + backslashes) is stored verbatim;
 * parsing performs no canonicalization.
 *
 * Boundaries: non-POSIX path shape that must survive untouched.
 */
BOOST_AUTO_TEST_CASE(server_mode_preserves_windows_path_verbatim)
{
    const std::string win = "C:\\ProgramData\\url\\storage.yaml";
    const auto path = parsePath({"url_shortener", "--storage-config", win});
    BOOST_REQUIRE(path.has_value());
    BOOST_CHECK_EQUAL(*path, win);
}

// ---------------------------------------------------------------------------
// Environment variable and precedence
// ---------------------------------------------------------------------------

/**
 * [Unit][CLI] The `URL_SHORTENER_STORAGE_CONFIG` env var sets the path when no
 * flag is present, in both server and link mode.
 *
 * On failure first check: the env read sits beside the other SHORTENER_* env
 * reads, before the `link` dispatch, so both modes observe it.
 */
BOOST_AUTO_TEST_CASE(env_var_sets_path_when_flag_absent)
{
    ScopedEnv env(kStorageEnvVar, "/e.yaml");

    ArgvBuilder server({"url_shortener"});
    const auto sp =
        CliParser{}.parse(server.argc(), server.argv()).config.storage_config_path;
    BOOST_REQUIRE(sp.has_value());
    BOOST_CHECK_EQUAL(*sp, "/e.yaml");

    ArgvBuilder link({"url_shortener", "link", "get", "--slug", "a"});
    const auto lr = CliParser{}.parse(link.argc(), link.argv());
    BOOST_REQUIRE(lr.config.storage_config_path.has_value());
    BOOST_CHECK_EQUAL(*lr.config.storage_config_path, "/e.yaml");
}

/**
 * [Unit][CLI] When both env var and flag are set, the flag wins, in both
 * server and link mode (precedence: flag > env > unset).
 *
 * Boundaries: env + flag together is the collision case the threat model pins.
 */
BOOST_AUTO_TEST_CASE(flag_overrides_env_var)
{
    ScopedEnv env(kStorageEnvVar, "/e.yaml");

    ArgvBuilder server({"url_shortener", "--storage-config", "/x.yaml"});
    const auto sp =
        CliParser{}.parse(server.argc(), server.argv()).config.storage_config_path;
    BOOST_REQUIRE(sp.has_value());
    BOOST_CHECK_EQUAL(*sp, "/x.yaml");

    ArgvBuilder link(
        {"url_shortener", "link", "get", "--slug", "a", "--storage-config",
            "/x.yaml"});
    const auto lr = CliParser{}.parse(link.argc(), link.argv());
    BOOST_REQUIRE(lr.config.storage_config_path.has_value());
    BOOST_CHECK_EQUAL(*lr.config.storage_config_path, "/x.yaml");
}

// ---------------------------------------------------------------------------
// link mode: extraction before the strict per-verb parsers
// ---------------------------------------------------------------------------

/**
 * [Unit][CLI] `link get --slug a --storage-config /x.yaml` and the flag placed
 * before the verb flags both parse the payload as `GetLinkQuery{slug,"a"}` and
 * record the path; the strict per-verb parser never sees the global flag.
 *
 * On failure first check: extract_storage_config_flag runs on verb_args before
 * cli::parseGetArgs and erases exactly the one flag (both split positions).
 */
BOOST_AUTO_TEST_CASE(link_get_strips_flag_in_any_position)
{
    namespace app = url_shortener::app;

    const std::vector<std::vector<std::string>> variants = {
        {"url_shortener", "link", "get", "--slug", "a", "--storage-config",
            "/x.yaml"},
        {"url_shortener", "link", "get", "--storage-config=/x.yaml", "--slug",
            "a"},
    };

    for (const auto& tokens : variants) {
        ArgvBuilder args(tokens);
        const auto r = CliParser{}.parse(args.argc(), args.argv());

        BOOST_REQUIRE(r.command.has_value());
        const auto* q = std::get_if<app::GetLinkQuery>(&r.command->payload);
        BOOST_REQUIRE(q != nullptr);
        BOOST_CHECK(q->by == app::GetLinkBy::slug);
        BOOST_CHECK_EQUAL(q->value, "a");

        BOOST_REQUIRE(r.config.storage_config_path.has_value());
        BOOST_CHECK_EQUAL(*r.config.storage_config_path, "/x.yaml");
    }
}

/**
 * [Unit][CLI] Every link verb accepts the global flag: each minimal valid
 * invocation parses and records the path, proving the extraction is verb-
 * agnostic.
 *
 * Boundaries: all nine verbs, not just `get`.
 */
BOOST_AUTO_TEST_CASE(all_link_verbs_accept_the_flag)
{
    const std::vector<std::vector<std::string>> invocations = {
        {"link", "create", "--url", "https://example.com/"},
        {"link", "get", "--slug", "a"},
        {"link", "update", "--slug", "a"},
        {"link", "delete", "--slug", "a"},
        {"link", "enable", "--slug", "a"},
        {"link", "disable", "--slug", "a"},
        {"link", "restore", "--slug", "a"},
        {"link", "preview", "--slug", "a"},
        {"link", "stats", "--slug", "a", "--from", "0", "--to", "10",
            "--bucket", "hour"},
    };

    for (auto tokens : invocations) {
        std::vector<std::string> argv = {"url_shortener"};
        for (auto& t : tokens) {
            argv.push_back(std::move(t));
        }
        argv.push_back("--storage-config");
        argv.push_back("/s.yaml");

        ArgvBuilder args(std::move(argv));
        const auto r = CliParser{}.parse(args.argc(), args.argv());

        BOOST_REQUIRE_MESSAGE(r.command.has_value(),
            "verb invocation did not parse: " << tokens.at(1));
        BOOST_REQUIRE(r.config.storage_config_path.has_value());
        BOOST_CHECK_EQUAL(*r.config.storage_config_path, "/s.yaml");
    }
}

/**
 * [Unit][CLI] After the global flag is stripped, an unknown verb flag is still
 * rejected by the strict per-verb parser (extraction removes exactly one flag,
 * not every double-dash token).
 */
BOOST_AUTO_TEST_CASE(unknown_verb_flag_still_rejected_after_extraction)
{
    ArgvBuilder args({"url_shortener", "link", "get", "--slug", "a",
        "--storage-config", "/x.yaml", "--bogus", "y"});
    BOOST_CHECK_THROW(
        CliParser{}.parse(args.argc(), args.argv()), std::invalid_argument);
}

// ---------------------------------------------------------------------------
// Error cases (flag named, path never echoed)
// ---------------------------------------------------------------------------

/**
 * [Unit][CLI] A missing value (flag is the last token, or is immediately
 * followed by another `--`-prefixed flag), an empty value, and a repeated flag
 * each throw std::invalid_argument naming only the flag.
 *
 * Scenario: the threat model requires error messages to name the flag and
 * never echo the path value (which can reveal filesystem layout).
 * On failure first check: extract_storage_config_flag's three guards and that
 * each `throw` message is a fixed string without the value.
 */
BOOST_AUTO_TEST_CASE(malformed_flag_throws_naming_only_the_flag)
{
    const std::string hidden_path = "/private/layout/storage.yaml";
    const std::vector<std::vector<std::string>> bad = {
        {"url_shortener", "link", "get", "--slug", "a", "--storage-config"},
        {"url_shortener", "link", "get", "--storage-config", "--slug", "a"},
        {"url_shortener", "link", "get", "--slug", "a", "--storage-config="},
        {"url_shortener", "link", "get", "--slug", "a", "--storage-config",
            hidden_path, "--storage-config", "/other.yaml"},
    };

    for (const auto& tokens : bad) {
        ArgvBuilder args(tokens);
        bool threw = false;
        try {
            CliParser{}.parse(args.argc(), args.argv());
        }
        catch (const std::invalid_argument& e) {
            threw = true;
            const std::string msg = e.what();
            BOOST_CHECK_MESSAGE(msg.find("--storage-config") != std::string::npos,
                "error must name the flag: " << msg);
            BOOST_CHECK_MESSAGE(msg.find(hidden_path) == std::string::npos,
                "error must not echo the path value: " << msg);
        }
        BOOST_CHECK_MESSAGE(threw, "malformed flag did not throw");
    }
}

/**
 * [Unit][CLI] Server mode rejects an empty `--storage-config=` value with a
 * flag-naming std::invalid_argument, matching the link-mode guard.
 */
BOOST_AUTO_TEST_CASE(server_mode_rejects_empty_value)
{
    ArgvBuilder args({"url_shortener", "--storage-config="});
    bool threw = false;
    try {
        CliParser{}.parse(args.argc(), args.argv());
    }
    catch (const std::invalid_argument& e) {
        threw = true;
        BOOST_CHECK(std::string(e.what()).find("--storage-config")
            != std::string::npos);
    }
    BOOST_CHECK(threw);
}

// ---------------------------------------------------------------------------
// Help text
// ---------------------------------------------------------------------------

/**
 * [Unit][CLI] Both top-level `--help` and `link --help` mention the storage
 * flag, and the help paths perform no I/O (they return a help string without
 * touching the config file).
 */
BOOST_AUTO_TEST_CASE(help_text_documents_the_flag)
{
    {
        ArgvBuilder args({"url_shortener", "--help"});
        const auto r = CliParser{}.parse(args.argc(), args.argv());
        BOOST_CHECK(r.help_requested);
        BOOST_CHECK(r.help_text.find("--storage-config") != std::string::npos);
        BOOST_CHECK(r.help_text.find("Storage options") != std::string::npos);
    }
    {
        ArgvBuilder args({"url_shortener", "link", "--help"});
        const auto r = CliParser{}.parse(args.argc(), args.argv());
        BOOST_CHECK(r.help_requested);
        BOOST_CHECK(r.help_text.find("--storage-config") != std::string::npos);
    }
}
