/**
 * @file 58_link_import_overwrite.cpp
 * @brief Unit tests: `link.import` with `on_conflict=overwrite` (subtask 09).
 *
 * Covers the additive `overwritten` counter, the routing of a slug conflict
 * through `LinkCommandService::ReplaceLinkTarget` (new target applied), the
 * refusal of overwrite over REST (a transport rule), the non-mutating
 * synchronous dry-run (threat model T-91/T-100), the soft-deleted-slug and
 * no-slug edge cases, and that the `skip`/`fail` policies from subtask 08 are
 * unchanged after the conflict branch was refactored.
 */
#define BOOST_TEST_MODULE CommandsLinkImportOverwriteTest
#include <cctype>
#include <chrono>
#include <cstddef>
#include <memory>
#include <optional>
#include <string>

#include <boost/test/unit_test.hpp>

#include "commands_test_fakes.hpp"
#include "url_shortener/app/link_command_service.hpp"
#include "url_shortener/commands/command_registry.hpp"
#include "url_shortener/commands/envelope.hpp"
#include "url_shortener/commands/error_codes.hpp"
#include "url_shortener/commands/job_commands.hpp"
#include "url_shortener/commands/job_runner.hpp"
#include "url_shortener/commands/job_types.hpp"
#include "url_shortener/commands/link_commands.hpp"
#include "url_shortener/commands/pair_commands.hpp"
#include "url_shortener/core/clock.hpp"
#include "url_shortener/core/utils.hpp"
#include "url_shortener/security/control_set.hpp"

using url_shortener::jsonString;
using url_shortener::commands::CallerContext;
using url_shortener::commands::CommandErrorCode;
using url_shortener::commands::CommandRegistry;
using url_shortener::commands::CommandRequest;
using url_shortener::commands::CommandResponse;
using url_shortener::commands::JobRecord;
using url_shortener::commands::JobRunner;
using url_shortener::commands::JobRunnerOptions;
using url_shortener::commands::JobState;
using url_shortener::commands::PairCommandDeps;
using url_shortener::commands::register_job_commands;
using url_shortener::commands::register_link_commands;
using url_shortener::commands::register_pair_commands;

namespace app = url_shortener::app;

namespace
{

CallerContext make_caller(ControlSet role, std::string transport)
{
    CallerContext caller;
    caller.principal = "local-operator";
    caller.transport = std::move(transport);
    caller.control_set = role;
    return caller;
}

// Imports need `write`; a non-"rest" transport so overwrite is permitted.
CallerContext admin_caller(std::string transport = "test")
{
    return make_caller(ControlSet::Admin, std::move(transport));
}

// Owns the fakes, service, runner and a registry wired with link, job and pair
// commands so a test can drive `link.import` and seed/inspect links through the
// one service.
struct Harness
{
    commands_test_fakes::FakeLinkStore store;
    commands_test_fakes::FakeStatsReader reader;
    ServerConfig config;
    ManualClock clock;
    std::shared_ptr<app::LinkCommandService> service;
    std::shared_ptr<JobRunner> runner;
    CommandRegistry registry;

    Harness()
        : config(commands_test_fakes::make_test_config())
        , clock(std::chrono::system_clock::time_point {})
        , runner(std::make_shared<JobRunner>(clock,
                                             JobRunnerOptions {2u, 64u, 256u}))
    {
        service =
            std::make_shared<app::LinkCommandService>(store, reader, config);
        registry.set_job_submitter(runner);
        register_link_commands(registry, *service);
        register_job_commands(registry, *runner);

        PairCommandDeps deps;
        deps.service = service;
        deps.max_inline_bytes = 65536;
        register_pair_commands(registry, std::move(deps));
    }

    // Seeds an existing link with the given slug and target.
    void seed_link(const std::string& slug, const std::string& target)
    {
        app::CreateLinkCommand cmd;
        cmd.target_url = target;
        cmd.slug = slug;
        BOOST_REQUIRE(service->CreateLink(cmd).ok());
    }
};

CommandRequest import_request(const std::string& data,
                              const std::string& on_conflict,
                              bool dry_run = false,
                              const std::string& format = "csv")
{
    std::string args = "{\"format\":\"" + format
        + "\",\"data\":" + jsonString(data) + ",\"on_conflict\":\""
        + on_conflict + "\"}";

    CommandRequest request;
    request.id = "import-ov";
    request.command = "link.import";
    request.args_json = std::move(args);
    request.options.dry_run = dry_run;
    return request;
}

long json_int(const std::string& json, const std::string& key)
{
    const std::string needle = "\"" + key + "\":";
    const std::size_t at = json.find(needle);
    BOOST_REQUIRE_MESSAGE(at != std::string::npos,
                          "missing field " + key + " in " + json);
    std::size_t i = at + needle.size();
    long value = 0;
    while (i < json.size()
           && std::isdigit(static_cast<unsigned char>(json[i])) != 0)
    {
        value = value * 10 + (json[i] - '0');
        ++i;
    }
    return value;
}

struct ImportResult
{
    JobState state = JobState::failed;
    std::string result_json;
    std::string fail_code;
};

ImportResult run_real_import(Harness& harness, const CommandRequest& request)
{
    const CommandResponse response =
        harness.registry.execute(admin_caller(), request);
    BOOST_REQUIRE_MESSAGE(
        response.job.has_value(),
        "expected a job, got error: " + response.error.message);
    const std::optional<JobRecord> record = harness.runner->wait_for_terminal(
        response.job->id, std::chrono::seconds(20));
    BOOST_REQUIRE(record.has_value());

    ImportResult out;
    out.state = record->state;
    out.result_json = record->result_json.value_or("");
    if (record->failure.has_value()) {
        out.fail_code = record->failure->code;
    }
    return out;
}

}  // namespace

// Type: unit (overwrite replaces an existing target and counts it separately).
// Scenario: a two-row import under overwrite retargets an existing slug and
//   creates a brand-new slug; counts are created=1, overwritten=1, total=2 and
//   the existing link now points at the new target.
// Boundaries: slug_conflict -> ReplaceLinkTarget; overwritten counter additive.
// On failure first check: did the conflict branch stop calling ReplaceLinkTarget
//   or fold overwritten into created?
BOOST_AUTO_TEST_CASE(overwrite_retargets_existing_and_creates_new)
{
    Harness harness;
    harness.seed_link("keep", "http://old.example/");

    const std::string data =
        "http://new.example/,keep\n"  // existing slug -> overwritten
        "http://fresh.example/,brand\n";  // new slug -> created
    const ImportResult result =
        run_real_import(harness, import_request(data, "overwrite"));

    BOOST_TEST((result.state == JobState::succeeded));
    BOOST_TEST(json_int(result.result_json, "total") == 2);
    BOOST_TEST(json_int(result.result_json, "created") == 1);
    BOOST_TEST(json_int(result.result_json, "overwritten") == 1);
    BOOST_TEST(json_int(result.result_json, "failed") == 0);
    BOOST_TEST(json_int(result.result_json, "skipped") == 0);

    const std::optional<url_shortener::Link> kept =
        harness.store.findBySlug("keep");
    BOOST_REQUIRE(kept.has_value());
    BOOST_TEST(kept->target_url == "http://new.example/");
    BOOST_TEST(harness.store.findBySlug("brand").has_value());
}

// Type: unit (overwrite of a soft-deleted slug fails, never resurrects it).
// Scenario: a slug that was soft-deleted is overwritten -> the row fails with
//   slug_conflict and the deleted record is untouched.
// Boundaries: ReplaceLinkTarget deleted-link guard -> slug_conflict -> failed.
// On failure first check: did ReplaceLinkTarget start editing deleted records,
//   or did the import stop mapping its slug_conflict to a failed row?
BOOST_AUTO_TEST_CASE(overwrite_deleted_slug_fails_slug_conflict)
{
    Harness harness;
    harness.seed_link("gone", "http://old.example/");
    app::DeleteLinkCommand del;
    del.slug = "gone";
    BOOST_REQUIRE(harness.service->DeleteLink(del).ok());

    const ImportResult result = run_real_import(
        harness, import_request("http://new.example/,gone\n", "overwrite"));

    BOOST_TEST((result.state == JobState::succeeded));
    BOOST_TEST(json_int(result.result_json, "overwritten") == 0);
    BOOST_TEST(json_int(result.result_json, "failed") == 1);
    BOOST_TEST(result.result_json.find("\"code\":\"slug_conflict\"")
               != std::string::npos);
    // The deleted record keeps its old target.
    BOOST_TEST(harness.store.findBySlug("gone")->target_url
               == "http://old.example/");
}

// Type: unit (a row without a slug can never overwrite).
// Scenario: under overwrite, a slug-less row gets a fresh generated slug
//   (created), the pre-existing link is untouched and overwritten stays 0.
// Boundaries: generated slug never collides -> never reaches the overwrite path.
// On failure first check: did the CSV decoder start reusing an existing slug,
//   or did apply_overwrite fire for a generated slug?
BOOST_AUTO_TEST_CASE(overwrite_row_without_slug_creates_new)
{
    Harness harness;
    harness.seed_link("keep", "http://old.example/");

    const ImportResult result = run_real_import(
        harness, import_request("http://new.example/\n", "overwrite"));

    BOOST_TEST((result.state == JobState::succeeded));
    BOOST_TEST(json_int(result.result_json, "created") == 1);
    BOOST_TEST(json_int(result.result_json, "overwritten") == 0);
    BOOST_TEST(harness.store.findBySlug("keep")->target_url
               == "http://old.example/");
    BOOST_TEST(harness.store.size() == 2u);
}

// Type: unit (dry-run overwrite is synchronous, non-mutating / T-91, T-100).
// Scenario: a dry-run overwrite over an existing slug returns a synchronous
//   result (not a job), reports overwritten=1, creates no job record, calls no
//   CreateLink, and leaves the existing target unchanged.
// Boundaries: dry-run dispatch; apply_overwrite dry-run branch counts only.
// On failure first check: did dry-run start submitting a job or did it call
//   ReplaceLinkTarget/CreateLink and mutate the store?
BOOST_AUTO_TEST_CASE(dry_run_overwrite_is_synchronous_non_mutating)
{
    Harness harness;
    harness.seed_link("keep", "http://old.example/");
    const std::size_t creates_before = harness.store.create_calls();

    const CommandResponse response = harness.registry.execute(
        admin_caller(),
        import_request("http://new.example/,keep\n", "overwrite",
                       /*dry_run=*/true));

    BOOST_TEST(response.ok == true);
    BOOST_TEST(response.job.has_value() == false);  // result, not a job
    BOOST_TEST(json_int(response.result_json, "overwritten") == 1);
    BOOST_TEST(json_int(response.result_json, "created") == 0);
    BOOST_TEST(response.result_json.find("\"dry_run\":true")
               != std::string::npos);

    // No job record, no mutation (T-91).
    BOOST_TEST(harness.runner->list(std::nullopt, 0, 100).total == 0u);
    BOOST_TEST(harness.store.create_calls() == creates_before);
    BOOST_TEST(harness.store.findBySlug("keep")->target_url
               == "http://old.example/");
}

// Type: unit (overwrite not offered over REST / transport rule).
// Scenario: a REST caller requesting overwrite is refused unsupported and
//   nothing is queued; a non-"rest" caller is allowed (proving the gate is the
//   transport rule, not a blanket block).
// Boundaries: factory transport == "rest" check for overwrite.
// On failure first check: did the factory start blocking overwrite for all
//   transports again, or stop blocking REST?
BOOST_AUTO_TEST_CASE(overwrite_refused_over_rest_allowed_elsewhere)
{
    Harness harness;
    harness.seed_link("keep", "http://old.example/");

    const CommandResponse rest = harness.registry.execute(
        make_caller(ControlSet::Admin, "rest"),
        import_request("http://new.example/,keep\n", "overwrite"));
    BOOST_TEST(rest.ok == false);
    BOOST_TEST((rest.error.code == CommandErrorCode::unsupported));
    BOOST_TEST(harness.runner->list(std::nullopt, 0, 100).total == 0u);

    // A non-"rest" caller is accepted (a job is queued).
    const CommandResponse other = harness.registry.execute(
        make_caller(ControlSet::Admin, "stdio"),
        import_request("http://new.example/,keep\n", "overwrite"));
    BOOST_TEST(other.ok == true);
    BOOST_REQUIRE(other.job.has_value());
    harness.runner->wait_for_terminal(other.job->id, std::chrono::seconds(20));
}

// Type: unit (skip/fail policies unchanged after the conflict-branch refactor).
// Scenario: the same conflicting input yields skipped=1 under skip and a
//   failed/slug_conflict job under fail, exactly as subtask 08 defined.
// Boundaries: the switch over ImportConflict in run_import.
// On failure first check: did the switch drop or reorder the skip/fail arms?
BOOST_AUTO_TEST_CASE(skip_and_fail_policies_unchanged)
{
    {
        Harness harness;
        const ImportResult result = run_real_import(
            harness,
            import_request("http://a.com/,dup\nhttp://b.com/,dup\n", "skip"));
        BOOST_TEST((result.state == JobState::succeeded));
        BOOST_TEST(json_int(result.result_json, "created") == 1);
        BOOST_TEST(json_int(result.result_json, "skipped") == 1);
        BOOST_TEST(json_int(result.result_json, "overwritten") == 0);
    }
    {
        Harness harness;
        const ImportResult result = run_real_import(
            harness,
            import_request("http://a.com/,dup\nhttp://b.com/,dup\n", "fail"));
        BOOST_TEST((result.state == JobState::failed));
        BOOST_TEST(result.fail_code == "slug_conflict");
        BOOST_TEST(harness.store.size() == 1u);
    }
}
