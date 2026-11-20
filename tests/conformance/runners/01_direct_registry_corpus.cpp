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
#include <string>
#include <vector>

#include <boost/test/unit_test.hpp>

#include "case_runner.hpp"
#include "corpus.hpp"
#include "direct_adapter.hpp"

using conformance::Case;
using conformance::CaseRunner;
using conformance::CorpusLoader;
using conformance::DirectRegistryAdapter;
using conformance::RunReport;
using conformance::Transport;

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
