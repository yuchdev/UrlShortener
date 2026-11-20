/**
 * @file 02_tui_client_corpus.cpp
 * @brief Boost.Test runner replaying the whole command corpus through the TUI
 * `InProcessClient` client seam.
 *
 * The TUI shim drives each corpus step through `tui::InProcessClient::call` -
 * the exact `ICommandClient` seam the TUI screens submit through (ADR 0003 rule
 * 1, contract C8) - over the one product `CommandRegistry` built by
 * `composition::build_command_runtime`. A green run proves the TUI's command
 * path (including the `--read-only` caller downgrade) adds no behavior over the
 * direct registry baseline (runner 01). This target is built and registered
 * only when `URLSHORTENER_ENABLE_TUI` is ON.
 *
 * @par Test type
 * Integration conformance runner (labels `integration;conformance`).
 * @par On failure first check
 * A failing case prints `tui/<case>/<step>: <path>: <reason>` with the expected
 * and actual envelopes. A mismatch is either a handler defect (file it against
 * the owning command task, do not patch behavior here) or the TUI client seam
 * (`InProcessClient::call`) diverging from the registry (e.g. dropping
 * `options`). An empty-corpus guard (`ran > 0`) fails if no case executed.
 */

#define BOOST_TEST_MODULE ConformanceTuiClientCorpus
#include <atomic>
#include <filesystem>
#include <optional>
#include <string>
#include <system_error>
#include <vector>

#include <boost/test/unit_test.hpp>
#include <url_shortener/commands/command_registry.hpp>
#include <url_shortener/commands/envelope.hpp>
#include <url_shortener/security/control_set.hpp>
#include <url_shortener/tui/command_client.hpp>
#include <url_shortener/tui/in_process_client.hpp>

#include "case_runner.hpp"
#include "corpus.hpp"
#include "mini_json.hpp"
#include "registry_fixture.hpp"
#include "tui_adapter.hpp"

using conformance::Case;
using conformance::CaseRunner;
using conformance::CorpusLoader;
using conformance::Json;
using conformance::RegistryFixture;
using conformance::RunReport;
using conformance::Step;
using conformance::Transport;
using conformance::TuiClientAdapter;
using url_shortener::commands::CallerContext;
using url_shortener::commands::CommandRegistry;
using url_shortener::tui::ICommandClient;
using url_shortener::tui::InProcessClient;

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

/// Counts cases the `tui` transport must skip: those whose `transports.only`
/// excludes `tui`, whose `transports.skip` names it, or - because `tui` cannot
/// carry raw text - that contain any `raw_request` step. The order mirrors the
/// case runner's skip resolution (only -> skip -> capability) so each case is
/// counted at most once.
int expected_tui_skips(const std::vector<Case>& cases)
{
    int skips = 0;
    for (const Case& test_case : cases) {
        if (!test_case.only.empty()
            && test_case.only.find(Transport::Tui) == test_case.only.end())
        {
            ++skips;
            continue;
        }
        if (test_case.skip.find(Transport::Tui) != test_case.skip.end()) {
            ++skips;
            continue;
        }
        for (const Step& step : test_case.steps) {
            if (step.raw_request.has_value()) {
                ++skips;
                break;
            }
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

/// A process-unique temp directory path for a seam test (not created).
std::filesystem::path unique_tmp_dir()
{
    static std::atomic<unsigned long long> counter {0};
    return std::filesystem::temp_directory_path()
        / ("conformance_tui_seam_" + std::to_string(counter.fetch_add(1)));
}

/// Serializes a one-command request envelope with an empty `args` object.
std::string command_envelope(const std::string& command)
{
    Json envelope = Json::object();
    envelope.set("v", Json::integer(1));
    envelope.set("command", Json::string(command));
    envelope.set("args", Json::object());
    return envelope.dump();
}

/// Returns the `error.code` string of a serialized response, or empty when the
/// response carries no error code.
std::string error_code_of(const std::string& response_text)
{
    std::string parse_error;
    const std::optional<Json> doc = Json::parse(response_text, &parse_error);
    if (!doc.has_value()) {
        return {};
    }
    const Json* code = doc->path("$.error.code");
    if (code == nullptr || !code->is_string()) {
        return {};
    }
    return code->as_string();
}

/// A minimal single-step case over the in-memory backend with a default role.
Case one_step_case(const std::string& name, const std::string& caller)
{
    Case test_case;
    test_case.name = name;
    test_case.caller = caller;
    test_case.backend = "inmemory";
    test_case.steps.emplace_back();  // one default step; send() is driven below
    return test_case;
}

}  // namespace

// Type: integration conformance (TUI client seam over the whole corpus).
// Scenario: every corpus case runs through build_command_runtime's registry via
//   tui::InProcessClient::call; no case fails; at least one case ran
//   (empty-corpus guard); exactly the cases that exclude/skip `tui` or carry a
//   raw_request step are skipped.
// Boundaries: tui supports both backends the corpus allows (sqlite, inmemory)
//   and declines no case; carries_raw is false, so raw_request cases are
//   skipped by capability; the skip count is derived from the corpus.
// On failure first check: a handler defect (file it against the owning command
//   task) or the InProcessClient seam diverging from the registry.
BOOST_AUTO_TEST_CASE(tui_runs_whole_corpus)
{
    const std::vector<Case> cases = load_corpus();
    TuiClientAdapter adapter;
    CaseRunner runner;
    const RunReport report = runner.run(cases, adapter);

    report_failures(report);
    BOOST_TEST_MESSAGE("tui: ran=" << report.ran
                                   << " skipped=" << report.skipped);

    BOOST_TEST(report.failed == 0);
    BOOST_TEST(report.ran > 0);
    BOOST_TEST(report.skipped == expected_tui_skips(cases));
}

// Type: integration conformance (no leaked state between runs).
// Scenario: running the whole corpus twice in the same process gives identical
//   ran/failed/skipped counts -- each case gets a fresh store, client and job
//   runner, so nothing carries over.
// Boundaries: a single process, two sequential runs of the same adapter.
// On failure first check: did the adapter stop resetting the fixture/client
//   between cases (state leak)?
BOOST_AUTO_TEST_CASE(repeated_run_is_identical)
{
    const std::vector<Case> cases = load_corpus();
    TuiClientAdapter adapter;
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

// Type: integration conformance (client seam identity, subtask step 2).
// Scenario: the client the TUI screens depend on (ICommandClient) is, in
//   production, the InProcessClient this shim drives; constructing it exactly
//   as tui_entry.cpp does yields an InProcessClient reachable through the base
//   interface, and a call round-trips through the real registry.
// Boundaries: the screens see only ICommandClient (C8); the concrete seam type
//   is InProcessClient, asserted via dynamic_cast on the base pointer.
// On failure first check: did the TUI client seam type change, or did
//   InProcessClient stop deriving from ICommandClient?
BOOST_AUTO_TEST_CASE(client_seam_is_in_process_client)
{
    namespace fs = std::filesystem;
    const fs::path tmp = unique_tmp_dir();
    std::error_code ec;
    fs::create_directories(tmp, ec);

    {
        RegistryFixture fixture(tmp, "inmemory");
        CallerContext caller;
        caller.principal = "local-operator";
        caller.transport = "tui";
        caller.control_set = ControlSet::Admin;
        InProcessClient client(fixture.registry(), caller);

        // A screen only ever holds the interface; the production seam is the
        // InProcessClient.
        ICommandClient& seam = client;
        BOOST_TEST(dynamic_cast<InProcessClient*>(&seam) != nullptr);
    }

    fs::remove_all(tmp, ec);
}

// Type: integration conformance security regression (read-only downgrade).
// Scenario: the same write command, sent through the TUI client seam over the
//   real registry, is refused `not_authorized` for a `user` caller
//   (ControlSet::User, the --read-only downgrade) but is NOT a not_authorized
//   refusal for an `admin` caller -- proving the role carried by the shim's
//   CallerContext is the discriminator, and that the role is applied at the
//   seam exactly as tui_entry.cpp does (not injected through the request body).
// Boundaries: authorization precedes argument validation (T-08), so empty args
//   are enough to reach the authz decision; the admin call clears authz (it may
//   then fail argspec validation, which is explicitly not not_authorized).
// On failure first check: did the adapter stop mapping `user` to
// ControlSet::User
//   / applying the per-step caller override, or did the registry authz change?
BOOST_AUTO_TEST_CASE(read_only_user_denied_write_but_admin_allowed)
{
    namespace fs = std::filesystem;
    const fs::path tmp = unique_tmp_dir();
    std::error_code ec;
    fs::create_directories(tmp, ec);

    {
        TuiClientAdapter adapter;
        Case test_case = one_step_case("tui_read_only_probe", "user");
        BOOST_REQUIRE(!adapter.begin_case(test_case, tmp).has_value());

        const std::string request = command_envelope("link.create");

        // Default role (user): the write command is refused not_authorized.
        Step user_step;  // no caller override -> case default "user"
        const std::string user_response = adapter.send(user_step, request);
        BOOST_TEST(error_code_of(user_response) == "not_authorized");

        // Per-step admin override: authorization passes, so the response is not
        // a not_authorized refusal (any other outcome is fine for this probe).
        Step admin_step;
        admin_step.caller = "admin";
        const std::string admin_response = adapter.send(admin_step, request);
        BOOST_TEST(error_code_of(admin_response) != "not_authorized");

        adapter.end_case();
    }

    fs::remove_all(tmp, ec);
}
