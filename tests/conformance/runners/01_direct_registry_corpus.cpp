/**
 * @file 01_direct_registry_corpus.cpp
 * @brief Boost.Test runner replaying the whole command corpus through the
 * direct registry adapter.
 *
 * The direct adapter is the reference transport: it executes each corpus case
 * straight against the product `CommandRegistry` built by
 * `composition::build_command_runtime`, with no transport in between. Every
 * other transport is judged against the same expectations, so a green run here
 * is the baseline the REST/CLI/stdio/TUI shims are compared to.
 *
 * @par Test type
 * Integration conformance runner (labels `integration;conformance`).
 * @par On failure first check
 * A failing case prints `direct/<case>/<step>: <path>: <reason>` with the
 * expected and actual envelopes. A mismatch is either a handler defect (file it
 * against the owning command task, do not patch behavior here) or a corpus
 * expectation that drifted from the registry. An empty-corpus guard
 * (`ran > 0`) fails if no case executed at all.
 */

#define BOOST_TEST_MODULE ConformanceDirectRegistryCorpus
#include <atomic>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include <boost/test/unit_test.hpp>

#include "case_runner.hpp"
#include "corpus.hpp"
#include "direct_adapter.hpp"
#include "mini_json.hpp"
#include "registry_fixture.hpp"
#include "url_shortener/commands/command_registry.hpp"
#include "url_shortener/commands/envelope.hpp"
#include "url_shortener/commands/error_codes.hpp"

using conformance::Case;
using conformance::CaseRunner;
using conformance::CorpusLoader;
using conformance::DirectRegistryAdapter;
using conformance::Json;
using conformance::RegistryFixture;
using conformance::RunReport;
using conformance::Transport;
using url_shortener::commands::CommandErrorCode;
using url_shortener::commands::CommandResponse;

namespace
{

/// Absolute path to the corpus suite directory (baked in via the build).
std::string corpus_dir()
{
    return std::string(CONFORMANCE_CORPUS_DIR) + "/commands";
}

/// Loads the whole corpus, requiring a clean structural parse.
std::vector<Case> load_corpus()
{
    std::string error;
    std::vector<Case> cases =
        CorpusLoader::load_directory(corpus_dir(), &error);
    BOOST_REQUIRE_MESSAGE(error.empty(), "corpus failed to load: " << error);
    return cases;
}

/// Counts cases the `direct` transport must skip: those whose `transports.only`
/// excludes `direct` or whose `transports.skip` names it. `direct` carries raw
/// text, so a `raw_request` step never adds a skip.
int expected_direct_skips(const std::vector<Case>& cases)
{
    int skips = 0;
    for (const Case& test_case : cases) {
        if (!test_case.only.empty()
            && test_case.only.find(Transport::Direct) == test_case.only.end())
        {
            ++skips;
        }
        else if (test_case.skip.find(Transport::Direct) != test_case.skip.end())
        {
            ++skips;
        }
    }
    return skips;
}

/// Prints every failure line so a CI log shows the readable path on a red run.
void report_failures(const RunReport& report)
{
    for (const std::string& failure : report.failures) {
        BOOST_TEST_MESSAGE(failure);
    }
}

/// A process-unique temp directory path for the sandbox test (not created).
std::filesystem::path unique_tmp_dir()
{
    static std::atomic<unsigned long long> counter {0};
    return std::filesystem::temp_directory_path()
        / ("conformance_pathjail_tmp_" + std::to_string(counter.fetch_add(1)));
}

/// Serializes a one-command request envelope with the given file `path` arg.
std::string pair_envelope(const std::string& command, const std::string& path)
{
    Json args = Json::object();
    args.set("format", Json::string("csv"));
    args.set("path", Json::string(path));
    Json envelope = Json::object();
    envelope.set("v", Json::integer(1));
    envelope.set("command", Json::string(command));
    envelope.set("args", std::move(args));
    return envelope.dump();
}

/// Reads the whole file at @p path, or an empty string when it cannot be read.
std::string read_file(const std::filesystem::path& path)
{
    std::ifstream stream(path, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(stream)),
                       std::istreambuf_iterator<char>());
}

}  // namespace

// Type: integration conformance (reference transport over the whole corpus).
// Scenario: every corpus case runs through build_command_runtime's registry on
//   the direct transport; no case fails; at least one case ran (empty-corpus
//   guard); exactly the cases that exclude or skip `direct` are skipped.
// Boundaries: direct supports both backends the corpus allows (sqlite,
//   inmemory) and declines no case; the skip count is derived from the corpus.
// On failure first check: a handler defect (file it against the owning command
//   task) or a corpus expectation that drifted from the registry.
BOOST_AUTO_TEST_CASE(direct_runs_whole_corpus)
{
    const std::vector<Case> cases = load_corpus();
    DirectRegistryAdapter adapter;
    CaseRunner runner;
    const RunReport report = runner.run(cases, adapter);

    report_failures(report);
    BOOST_TEST_MESSAGE("direct: ran=" << report.ran
                                      << " skipped=" << report.skipped);

    BOOST_TEST(report.failed == 0);
    BOOST_TEST(report.ran > 0);
    BOOST_TEST(report.skipped == expected_direct_skips(cases));
}

// Type: integration conformance (no leaked state between runs).
// Scenario: running the whole corpus twice in the same process gives identical
//   ran/failed/skipped counts -- each case gets a fresh store and job runner,
//   so nothing carries over (subtask 04 success criterion 2).
// Boundaries: a single process, two sequential runs of the same adapter.
// On failure first check: did a fixture stop resetting the store, system.info
// or
//   the job runner between cases (state leak)?
BOOST_AUTO_TEST_CASE(repeated_run_is_identical)
{
    const std::vector<Case> cases = load_corpus();
    DirectRegistryAdapter adapter;
    CaseRunner runner;

    const RunReport first = runner.run(cases, adapter);
    const RunReport second = runner.run(cases, adapter);

    report_failures(second);
    BOOST_TEST(first.ran == second.ran);
    BOOST_TEST(first.failed == second.failed);
    BOOST_TEST(first.skipped == second.skipped);
    BOOST_TEST(second.failed == 0);
    BOOST_TEST(second.ran > 0);
}

// Type: integration conformance security regression (path confinement).
// Scenario: the per-case fixture pins the file-path root to its temp dir, so a
//   pair command whose `path` escapes the root -- an absolute path outside it
//   or a `${tmp}/..` traversal -- is refused `invalid_field` and a sentinel
//   file outside the root is never created, opened for write, or modified.
// Boundaries: a `path` arg is free text (unlike a `files` key the case runner
//   confines); `cmd` has no `--path-root`, so the fixture `root` is the only
//   confinement. The capability is granted (allow_paths=true), so the refusal
//   is the root jail, not `unsupported`.
// On failure first check: did RegistryFixture stop setting file_paths.root to
//   the case temp dir, or did the pair-command root jail stop applying?
BOOST_AUTO_TEST_CASE(direct_paths_are_confined_to_case_tmp)
{
    namespace fs = std::filesystem;

    // A sentinel OUTSIDE any case temp dir, with known content.
    const fs::path sentinel =
        fs::temp_directory_path() / "conformance_pathjail_sentinel.txt";
    {
        std::ofstream stream(sentinel, std::ios::binary);
        stream << "SENTINEL";
    }

    const fs::path tmp = unique_tmp_dir();
    std::error_code ec;
    fs::create_directories(tmp, ec);

    {
        RegistryFixture fixture(tmp, "inmemory");
        const auto admin = fixture.caller("admin");

        // 1) link.export writing to an absolute path outside the root.
        const CommandResponse export_abs = fixture.registry().execute_json(
            admin, pair_envelope("link.export", sentinel.string()));
        BOOST_TEST(!export_abs.ok);
        BOOST_TEST((export_abs.error.code == CommandErrorCode::invalid_field));

        // 2) link.export via a `${tmp}/..` traversal back onto the sentinel.
        const std::string escape = (tmp / ".." / sentinel.filename()).string();
        const CommandResponse export_escape = fixture.registry().execute_json(
            admin, pair_envelope("link.export", escape));
        BOOST_TEST(!export_escape.ok);

        // 3) link.import reading an absolute path outside the root is refused
        //    before the file is opened.
        const CommandResponse import_abs = fixture.registry().execute_json(
            admin, pair_envelope("link.import", sentinel.string()));
        BOOST_TEST(!import_abs.ok);
        BOOST_TEST((import_abs.error.code == CommandErrorCode::invalid_field));
    }

    // The sentinel outside the root was never created-anew or overwritten.
    BOOST_TEST(read_file(sentinel) == "SENTINEL");

    fs::remove(sentinel, ec);
    fs::remove_all(tmp, ec);
}
