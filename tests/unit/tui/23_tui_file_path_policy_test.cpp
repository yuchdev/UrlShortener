/**
 * @file 23_tui_file_path_policy_test.cpp
 * @brief Unit tests for the `tui` file-path wiring seam
 * `url_shortener::tui::tui_file_path_policy` (Milestone 0006, Task 07.0 subtask
 * 20, user ruling (a)).
 *
 * `run_tui` derives `runtime_options.file_paths` from the parsed
 * `--no-file-paths` / `--path-root` flags through this one seam, so the wiring
 * is provable without an interactive terminal. The seam is the only place the
 * two flags become a `composition::PairFileOptions`; these tests pin the three
 * reachable shapes and the end-to-end wire from `CliParser::parse` so the
 * local-operator default (full filesystem) is preserved and the confinement /
 * disable semantics match `serve-stdio` (threat model T-95/T-96/T-118).
 *
 * It links `url_shortener_tui` (which carries `tui_entry.cpp`), mirroring
 * `19_tui_options_test.cpp`.
 */
#define BOOST_TEST_MODULE TuiFilePathPolicyTest

#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <boost/test/unit_test.hpp>
#include <url_shortener/cli/cli_parser.hpp>
#include <url_shortener/composition/command_runtime.hpp>
#include <url_shortener/tui/tui_entry.hpp>

namespace tui = url_shortener::tui;
namespace composition = url_shortener::composition;

namespace
{
/// Build a mutable argv from strings for CliParser::parse().
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

/// Parse an invocation and return the TuiCliOptions (asserting tui mode).
TuiCliOptions parse_tui(std::vector<std::string> args)
{
    ArgvBuilder argv(std::move(args));
    const ParseResult result = CliParser {}.parse(argv.argc(), argv.argv());
    BOOST_REQUIRE(result.tui.has_value());
    return *result.tui;
}
}  // namespace

// ---------------------------------------------------------------------------
// Type: unit (normal, default preserved). Scenario: neither flag -> the local
//   operator keeps full-filesystem access (allow_paths == true, no root),
//   exactly as before the flags existed.
// Boundaries: the "neither flag" default must not regress to deny.
// On failure first check: did tui_file_path_policy stop defaulting allow_paths
//   to true?
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(neither_flag_keeps_full_filesystem)
{
    TuiCliOptions cli;  // defaults: no_file_paths=false, path_root=nullopt
    const composition::PairFileOptions policy = tui::tui_file_path_policy(cli);

    BOOST_CHECK_EQUAL(policy.allow_paths, true);
    BOOST_CHECK(!policy.root.has_value());
}

// ---------------------------------------------------------------------------
// Type: unit (normal, disable). Scenario: --no-file-paths -> allow_paths false
//   and no root, so every `path` returns `unsupported` (T-95).
// On failure first check: did the disable branch set a root, or fail to clear
//   allow_paths?
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(no_file_paths_disables_capability)
{
    TuiCliOptions cli;
    cli.no_file_paths = true;
    const composition::PairFileOptions policy = tui::tui_file_path_policy(cli);

    BOOST_CHECK_EQUAL(policy.allow_paths, false);
    BOOST_CHECK(!policy.root.has_value());
}

// ---------------------------------------------------------------------------
// Type: unit (normal, confine). Scenario: --path-root <DIR> -> allow_paths true
//   and root == the already-normalized DIR, so `path` is confined (T-96).
// On failure first check: did the confine branch drop the root or disable
//   paths?
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(path_root_confines_capability)
{
    TuiCliOptions cli;
    cli.path_root = std::string("/srv/root");
    const composition::PairFileOptions policy = tui::tui_file_path_policy(cli);

    BOOST_CHECK_EQUAL(policy.allow_paths, true);
    BOOST_REQUIRE(policy.root.has_value());
    BOOST_CHECK_EQUAL(*policy.root, std::string("/srv/root"));
}

// ---------------------------------------------------------------------------
// Type: unit (capability seam is role-agnostic). Scenario: --read-only does NOT
//   change the derived capability - the policy still reflects the flags, and
//   the registry (not this seam) denies `path` for a read-only caller because
//   both path forms require `write`.
// On failure first check: did the seam start folding read_only into the file-
//   path capability (it must not; authorization is the registry's job, C7)?
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(read_only_does_not_alter_the_capability)
{
    TuiCliOptions cli;
    cli.read_only = true;
    cli.path_root = std::string("/srv/root");
    const composition::PairFileOptions policy = tui::tui_file_path_policy(cli);

    // The capability still carries the root; the write-permission denial for a
    // read-only session is enforced downstream in the registry, not here.
    BOOST_CHECK_EQUAL(policy.allow_paths, true);
    BOOST_REQUIRE(policy.root.has_value());
    BOOST_CHECK_EQUAL(*policy.root, std::string("/srv/root"));
}

// ---------------------------------------------------------------------------
// Type: unit (end-to-end wire). Scenario: parsing `tui --path-root /foo/` and
//   feeding the resulting TuiCliOptions through tui_file_path_policy yields the
//   normalized root `/foo` as the runtime's confinement - the whole CLI ->
//   runtime_options.file_paths path, trailing slash included.
// On failure first check: did the parser stop normalizing, or the seam stop
//   forwarding path_root?
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(parsed_path_root_reaches_runtime_options_normalized)
{
    const TuiCliOptions cli =
        parse_tui({"url_shortener", "tui", "--path-root", "/foo/"});
    const composition::PairFileOptions policy = tui::tui_file_path_policy(cli);

    BOOST_CHECK_EQUAL(policy.allow_paths, true);
    BOOST_REQUIRE(policy.root.has_value());
    BOOST_CHECK_EQUAL(*policy.root, std::string("/foo"));

    // And `tui --no-file-paths` disables the capability end to end.
    const TuiCliOptions disabled =
        parse_tui({"url_shortener", "tui", "--no-file-paths"});
    const composition::PairFileOptions disabled_policy =
        tui::tui_file_path_policy(disabled);
    BOOST_CHECK_EQUAL(disabled_policy.allow_paths, false);
    BOOST_CHECK(!disabled_policy.root.has_value());
}
