#define BOOST_TEST_MODULE CommandsLinkImportCommandTest
#include <atomic>
#include <cctype>
#include <chrono>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <future>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include <boost/test/unit_test.hpp>

#include "commands_test_fakes.hpp"

#include "url_shortener/commands/command_registry.hpp"
#include "url_shortener/commands/envelope.hpp"
#include "url_shortener/commands/error_codes.hpp"
#include "url_shortener/commands/job_commands.hpp"
#include "url_shortener/commands/job_runner.hpp"
#include "url_shortener/commands/job_types.hpp"
#include "url_shortener/commands/link_commands.hpp"
#include "url_shortener/commands/pair_commands.hpp"
#include "url_shortener/commands/pair_line_parser.hpp"
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

// A caller with the given role and transport. Imports need `write` (Admin).
CallerContext make_caller(ControlSet role, std::string transport)
{
    CallerContext caller;
    caller.principal = "local-operator";
    caller.transport = std::move(transport);
    caller.control_set = role;
    return caller;
}

CallerContext admin_caller(std::string transport = "test")
{
    return make_caller(ControlSet::Admin, std::move(transport));
}

// Owns the fakes, service, runner and a registry wired with the link, job and
// pair commands, so a test can drive `link.import` alongside `link.get` and
// `job.get` through the one registry and the real `JobRunner`.
struct Harness
{
    commands_test_fakes::FakeLinkStore store;
    commands_test_fakes::FakeStatsReader reader;
    ServerConfig config;
    ManualClock clock;
    std::shared_ptr<app::LinkCommandService> service;
    std::shared_ptr<JobRunner> runner;
    CommandRegistry registry;

    explicit Harness(bool allow_private = false,
                     bool allow_paths = false,
                     unsigned workers = 2)
        : config(commands_test_fakes::make_test_config(allow_private)),
          clock(std::chrono::system_clock::time_point {}),
          runner(std::make_shared<JobRunner>(
              clock, JobRunnerOptions {workers, 64, 256}))
    {
        service =
            std::make_shared<app::LinkCommandService>(store, reader, config);
        registry.set_job_submitter(runner);
        register_link_commands(registry, *service);
        register_job_commands(registry, *runner);

        PairCommandDeps deps;
        deps.service = service;
        deps.max_inline_bytes = 65536;
        deps.file_paths.allow_paths = allow_paths;
        register_pair_commands(registry, std::move(deps));
    }
};

// Builds a `link.import` request from a single source (`data` or `path`).
CommandRequest import_request(const std::string& format,
                              const std::string& source_key,
                              const std::string& source_value,
                              const std::string& on_conflict = "",
                              bool dry_run = false)
{
    std::string args = "{\"format\":\"" + format + "\",\"" + source_key
        + "\":" + jsonString(source_value);
    if (!on_conflict.empty()) {
        args += ",\"on_conflict\":\"" + on_conflict + "\"";
    }
    args += "}";

    CommandRequest request;
    request.id = "import-1";
    request.command = "link.import";
    request.args_json = std::move(args);
    request.options.dry_run = dry_run;
    return request;
}

// Extracts a non-negative integer field value from well-formed result JSON.
long json_int(const std::string& json, const std::string& key)
{
    const std::string needle = "\"" + key + "\":";
    const std::size_t at = json.find(needle);
    BOOST_REQUIRE_MESSAGE(at != std::string::npos,
                          "missing field " + key + " in " + json);
    std::size_t i = at + needle.size();
    long value = 0;
    bool any = false;
    while (i < json.size()
           && std::isdigit(static_cast<unsigned char>(json[i])) != 0) {
        value = value * 10 + (json[i] - '0');
        ++i;
        any = true;
    }
    BOOST_REQUIRE(any);
    return value;
}

// Terminal outcome of a real (non-dry-run) import job.
struct ImportResult
{
    JobState state = JobState::failed;
    std::string result_json;
    std::string fail_code;
};

// Submits a real import, waits for it to finish and returns its terminal
// record fields.
ImportResult run_real_import(Harness& harness, const CommandRequest& request)
{
    const CommandResponse response =
        harness.registry.execute(admin_caller(), request);
    BOOST_REQUIRE_MESSAGE(response.job.has_value(),
                          "expected a job, got error: "
                              + response.error.message);
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

// Writes @p content to a fresh temp file and returns its path; the caller
// removes it.
std::string write_temp_file(const std::string& content)
{
    const std::filesystem::path path =
        std::filesystem::temp_directory_path()
        / ("import_test_"
           + std::to_string(
               std::chrono::steady_clock::now().time_since_epoch().count())
           + ".csv");
    std::ofstream out(path, std::ios::binary);
    out << content;
    out.close();
    return path.string();
}

}  // namespace

// Type: unit (CSV happy/malformed/conflict mix / counts and line numbers).
// Scenario: a CSV with a header, two creatable rows, a malformed row, a blank
//   line and a duplicate-slug row yields total=4, created=2, failed=1,
//   skipped=1 and reports the malformed row at its 1-based physical line 4.
// Boundaries: header/blank rows excluded from total; per-line error numbering.
// On failure first check: did the body start counting header/blank rows, or did
//   the parser line number drift from the physical line?
BOOST_AUTO_TEST_CASE(csv_mixed_counts_and_physical_line_numbers)
{
    Harness harness;
    const std::string data =
        "full_url,slug\n"                 // line 1: header -> skipped
        "http://a.com/,aaa\n"             // line 2: created
        "http://b.com/\n"                 // line 3: created (generated slug)
        "x,y,z\n"                         // line 4: malformed -> failed
        "\n"                              // line 5: blank -> skipped
        "http://a.com/,aaa\n";            // line 6: dup slug -> skipped

    const ImportResult result =
        run_real_import(harness, import_request("csv", "data", data));

    BOOST_TEST((result.state == JobState::succeeded));
    BOOST_TEST(json_int(result.result_json, "total") == 4);
    BOOST_TEST(json_int(result.result_json, "created") == 2);
    BOOST_TEST(json_int(result.result_json, "failed") == 1);
    BOOST_TEST(json_int(result.result_json, "skipped") == 1);
    BOOST_TEST(json_int(result.result_json, "errors_omitted") == 0);
    BOOST_TEST(result.result_json.find("\"line\":4") != std::string::npos);
    BOOST_TEST(result.result_json.find("\"dry_run\":false")
               != std::string::npos);
    BOOST_TEST(harness.store.size() == 2u);
}

// Type: unit (blank slug generates one).
// Scenario: a CSV row with no slug column creates a link with a non-empty,
//   service-generated slug.
// Boundaries: slug generation path through CreateLink.
// On failure first check: did the CSV decoder stop leaving slug nullopt when
//   the column is absent?
BOOST_AUTO_TEST_CASE(csv_blank_slug_generates_slug)
{
    Harness harness;
    const ImportResult result = run_real_import(
        harness, import_request("csv", "data", "http://generated.example/\n"));

    BOOST_TEST((result.state == JobState::succeeded));
    BOOST_TEST(json_int(result.result_json, "created") == 1);
    const std::vector<url_shortener::Link> links = harness.store.all();
    BOOST_REQUIRE(links.size() == 1u);
    BOOST_TEST(!links.front().slug.empty());
    BOOST_TEST(links.front().slug.size() == 7u);
}

// Type: unit (NDJSON happy path with tags and campaign).
// Scenario: one NDJSON link.create object with tags and a campaign is created
//   through the shared args decoder.
// Boundaries: NDJSON row -> validate_args -> decode_create_link_command.
// On failure first check: did link.import stop reusing the link.create schema
//   and decoder?
BOOST_AUTO_TEST_CASE(ndjson_happy_path_with_tags_and_campaign)
{
    Harness harness;
    const std::string data =
        "{\"target_url\":\"http://nd.example/\",\"slug\":\"nd1\","
        "\"tags\":[\"x\",\"y\"],\"campaign\":{\"name\":\"spring\"}}\n";

    const ImportResult result =
        run_real_import(harness, import_request("ndjson", "data", data));

    BOOST_TEST((result.state == JobState::succeeded));
    BOOST_TEST(json_int(result.result_json, "created") == 1);
    const std::optional<url_shortener::Link> link =
        harness.store.findBySlug("nd1");
    BOOST_REQUIRE(link.has_value());
    BOOST_TEST(link->tags.size() == 2u);
}

// Type: unit (NDJSON unknown key -> failed/invalid_field).
// Scenario: an NDJSON object with an undeclared key fails the row as
//   invalid_field and persists nothing.
// Boundaries: validate_args rejection inside the body.
// On failure first check: did the body stop validating NDJSON rows against the
//   link.create schema?
BOOST_AUTO_TEST_CASE(ndjson_unknown_key_row_fails_invalid_field)
{
    Harness harness;
    const std::string data =
        "{\"target_url\":\"http://nd.example/\",\"bogus\":1}\n";

    const ImportResult result =
        run_real_import(harness, import_request("ndjson", "data", data));

    BOOST_TEST(json_int(result.result_json, "total") == 1);
    BOOST_TEST(json_int(result.result_json, "failed") == 1);
    BOOST_TEST(result.result_json.find("\"code\":\"invalid_field\"")
               != std::string::npos);
    BOOST_TEST(harness.store.size() == 0u);
}

// Type: unit (single SSRF path: private target rejected by CreateLink).
// Scenario: with allow_private_targets=false a 127.0.0.1 row is failed as
//   invalid_url; with true the identical row is created, proving the import
//   honours the same switch through the one validation path (contract C1).
// Boundaries: normalizeTargetUrl/isPrivateHost reached only via CreateLink.
// On failure first check: did pair_commands add its own URL check instead of
//   deferring to the service?
BOOST_AUTO_TEST_CASE(private_target_row_honours_allow_private_switch)
{
    {
        Harness denied(/*allow_private=*/false);
        const ImportResult result = run_real_import(
            denied, import_request("csv", "data", "http://127.0.0.1/\n"));
        BOOST_TEST(json_int(result.result_json, "failed") == 1);
        BOOST_TEST(result.result_json.find("\"code\":\"invalid_url\"")
                   != std::string::npos);
        BOOST_TEST(denied.store.size() == 0u);
    }
    {
        Harness allowed(/*allow_private=*/true);
        const ImportResult result = run_real_import(
            allowed, import_request("csv", "data", "http://127.0.0.1/\n"));
        BOOST_TEST(json_int(result.result_json, "created") == 1);
        BOOST_TEST(allowed.store.size() == 1u);
    }
}

// Type: unit (URL control-char regression / requirement 6).
// Scenario: a CSV row whose full_url carries a CR in the path is rejected by
//   the single CreateLink URL path as invalid_url (normalizeTargetUrl), exactly
//   as isValidSlug rejects control chars in a slug.
// Boundaries: control char reaches CreateLink (the CSV parser passes it
//   through) and is rejected there, not in pair_commands.
// On failure first check: did normalizeTargetUrl stop rejecting a line
//   terminator in the URL, or did the CSV parser start stripping mid-field
//   control bytes?
// NOTE (reported to the brief owner): only CR/LF *in the path* are rejected by
//   normalizeTargetUrl today; a NUL / U+2028 / other control byte embedded in
//   the host or path is NOT rejected by the core validator. Per requirement 6
//   this is reported rather than patched into core/utils.cpp here.
BOOST_AUTO_TEST_CASE(url_control_char_in_path_rejected_by_create)
{
    Harness harness;
    std::string data = "http://example.com/a";
    data.push_back('\r');
    data += "b\n";

    const ImportResult result =
        run_real_import(harness, import_request("csv", "data", data));

    BOOST_TEST(json_int(result.result_json, "failed") == 1);
    BOOST_TEST(result.result_json.find("\"code\":\"invalid_url\"")
               != std::string::npos);
    BOOST_TEST(harness.store.size() == 0u);
}

// Type: unit (conflict mode skip).
// Scenario: a duplicate slug under on_conflict=skip is counted skipped and the
//   first row stays persisted; the job succeeds.
// Boundaries: slug_conflict -> skip branch.
// On failure first check: did the body stop treating AppErrorCode::slug_conflict
//   as the skip trigger?
BOOST_AUTO_TEST_CASE(conflict_skip_counts_skipped)
{
    Harness harness;
    const std::string data = "http://a.com/,dup\nhttp://b.com/,dup\n";
    const ImportResult result =
        run_real_import(harness, import_request("csv", "data", data, "skip"));

    BOOST_TEST((result.state == JobState::succeeded));
    BOOST_TEST(json_int(result.result_json, "created") == 1);
    BOOST_TEST(json_int(result.result_json, "skipped") == 1);
    BOOST_TEST(harness.store.size() == 1u);
}

// Type: unit (conflict mode fail aborts the job, prior rows persisted).
// Scenario: a duplicate slug under on_conflict=fail ends the job
//   failed/slug_conflict while the first row stays persisted.
// Boundaries: slug_conflict -> fail branch; partial persistence kept.
// On failure first check: did the fail branch stop aborting, or did it roll
//   back the already-created row?
BOOST_AUTO_TEST_CASE(conflict_fail_aborts_job_keeps_prior_rows)
{
    Harness harness;
    const std::string data = "http://a.com/,dup\nhttp://b.com/,dup\n";
    const ImportResult result =
        run_real_import(harness, import_request("csv", "data", data, "fail"));

    BOOST_TEST((result.state == JobState::failed));
    BOOST_TEST(result.fail_code == "slug_conflict");
    BOOST_TEST(harness.store.size() == 1u);  // the first row persisted
}

// Type: unit (error list cap and omitted count / T-109).
// Scenario: 150 malformed rows cap the reported errors at 100 and count 50 as
//   omitted.
// Boundaries: kMaxReportedImportErrors boundary at 100.
// On failure first check: did ImportErrorList stop capping, or did the body
//   stop forwarding its omitted count into the result?
BOOST_AUTO_TEST_CASE(many_bad_rows_cap_errors_at_100_with_omitted_50)
{
    Harness harness;
    std::string data;
    for (int i = 0; i < 150; ++i) {
        data += "x,y,z\n";  // three columns -> malformed
    }
    const ImportResult result =
        run_real_import(harness, import_request("csv", "data", data));

    BOOST_TEST(json_int(result.result_json, "total") == 150);
    BOOST_TEST(json_int(result.result_json, "failed") == 150);
    BOOST_TEST(json_int(result.result_json, "errors_omitted") == 50);

    std::size_t entries = 0;
    for (std::size_t p = result.result_json.find("\"line\":");
         p != std::string::npos;
         p = result.result_json.find("\"line\":", p + 1)) {
        ++entries;
    }
    BOOST_TEST(entries == 100u);
}

// Type: unit (over-long line counted failed, next line processed / T-97).
// Scenario: a line longer than kMaxImportLineBytes is counted failed and the
//   following valid row is still created.
// Boundaries: line_status::too_long handling.
// On failure first check: did the body stop continuing after a too_long line?
BOOST_AUTO_TEST_CASE(over_long_line_failed_then_next_row_processed)
{
    Harness harness;
    std::string data(
        url_shortener::commands::kMaxImportLineBytes + 100, 'a');
    data += "\nhttp://ok.example/,okslug\n";

    const ImportResult result =
        run_real_import(harness, import_request("csv", "data", data));

    BOOST_TEST(json_int(result.result_json, "failed") == 1);
    BOOST_TEST(json_int(result.result_json, "created") == 1);
    BOOST_TEST(harness.store.findBySlug("okslug").has_value());
}

// Type: unit (dry-run is synchronous, non-mutating, bounded / T-91, T-97).
// Scenario: a dry-run returns a synchronous result (no job), creates no job
//   record, performs zero CreateLink calls, and detects an in-file duplicate
//   slug exactly as a real run would.
// Boundaries: dispatch::dry_run_job path; in-file slug set.
// On failure first check: did dry-run start submitting a job, or did
//   ValidateCreateLink start writing to the store?
BOOST_AUTO_TEST_CASE(dry_run_is_synchronous_non_mutating_detects_dup)
{
    Harness harness;
    const std::string data = "http://a.com/,dup\nhttp://b.com/,dup\n";
    const CommandResponse response = harness.registry.execute(
        admin_caller(),
        import_request("csv", "data", data, "skip", /*dry_run=*/true));

    BOOST_TEST(response.ok == true);
    BOOST_TEST(response.job.has_value() == false);  // a result, not a job
    BOOST_TEST(json_int(response.result_json, "created") == 1);
    BOOST_TEST(json_int(response.result_json, "skipped") == 1);
    BOOST_TEST(response.result_json.find("\"dry_run\":true")
               != std::string::npos);
    // No job record and no mutation (T-91).
    BOOST_TEST(harness.runner->list(std::nullopt, 0, 100).total == 0u);
    BOOST_TEST(harness.store.create_calls() == 0u);
    BOOST_TEST(harness.store.size() == 0u);
}

// Type: unit (both/neither of data/path -> invalid_field, nothing queued).
// Scenario: supplying neither or both of data/path is rejected invalid_field by
//   the factory and queues no job.
// Boundaries: factory step 1 (exactly one source).
// On failure first check: did the factory stop requiring exactly one source?
BOOST_AUTO_TEST_CASE(both_or_neither_source_is_invalid_field)
{
    Harness harness;

    CommandRequest neither;
    neither.id = "n";
    neither.command = "link.import";
    neither.args_json = "{\"format\":\"csv\"}";
    const CommandResponse r1 =
        harness.registry.execute(admin_caller(), neither);
    BOOST_TEST(r1.ok == false);
    BOOST_TEST((r1.error.code == CommandErrorCode::invalid_field));

    CommandRequest both;
    both.id = "b";
    both.command = "link.import";
    both.args_json =
        "{\"format\":\"csv\",\"data\":\"x\",\"path\":\"/tmp/x\"}";
    const CommandResponse r2 = harness.registry.execute(admin_caller(), both);
    BOOST_TEST(r2.ok == false);
    BOOST_TEST((r2.error.code == CommandErrorCode::invalid_field));

    BOOST_TEST(harness.runner->list(std::nullopt, 0, 100).total == 0u);
}

// Type: unit (inline data above the cap -> invalid_field).
// Scenario: an inline payload larger than deps.max_inline_bytes is refused
//   invalid_field by from_inline, with nothing queued.
// Boundaries: LineSource::from_inline size cap.
// On failure first check: did the factory stop passing max_inline_bytes to
//   from_inline?
BOOST_AUTO_TEST_CASE(inline_data_over_cap_is_invalid_field)
{
    // A harness whose inline cap is tiny; the ArgSpec string cap is far larger,
    // so the refusal comes from from_inline, not the registry.
    commands_test_fakes::FakeLinkStore store;
    commands_test_fakes::FakeStatsReader reader;
    ServerConfig config = commands_test_fakes::make_test_config();
    ManualClock clock(std::chrono::system_clock::time_point {});
    auto runner = std::make_shared<JobRunner>(
        clock, JobRunnerOptions {1u, 4u, 16u});
    auto service =
        std::make_shared<app::LinkCommandService>(store, reader, config);

    CommandRegistry registry;
    registry.set_job_submitter(runner);
    PairCommandDeps deps;
    deps.service = service;
    deps.max_inline_bytes = 8;  // tiny cap
    register_pair_commands(registry, std::move(deps));

    const CommandResponse response = registry.execute(
        admin_caller(),
        import_request("csv", "data", "http://a.com/,aaa\n"));
    BOOST_TEST(response.ok == false);
    BOOST_TEST((response.error.code == CommandErrorCode::invalid_field));
    BOOST_TEST(runner->list(std::nullopt, 0, 100).total == 0u);
}

// Type: unit (path refused by capability, not transport / T-95).
// Scenario: with allow_paths=false a `path` import is refused unsupported for
//   both a REST transport and a non-"rest" transport, and nothing is queued.
// Boundaries: capability-only refusal in the factory.
// On failure first check: did the factory start branching on transport name
//   instead of deps.file_paths.allow_paths?
BOOST_AUTO_TEST_CASE(path_without_capability_is_unsupported_any_transport)
{
    Harness harness(/*allow_private=*/false, /*allow_paths=*/false);

    for (const char* transport : {"rest", "stdio"}) {
        const CommandResponse response = harness.registry.execute(
            make_caller(ControlSet::Admin, transport),
            import_request("csv", "path", "/etc/passwd"));
        BOOST_TEST(response.ok == false);
        BOOST_TEST((response.error.code == CommandErrorCode::unsupported));
    }
    BOOST_TEST(harness.runner->list(std::nullopt, 0, 100).total == 0u);
}

// Type: unit (overwrite not offered before subtask 09).
// Scenario: on_conflict=overwrite is refused unsupported for a REST caller
//   (and, being capability/mode-based, for any caller).
// Boundaries: overwrite rejection in the factory.
// On failure first check: did overwrite start reaching the body?
BOOST_AUTO_TEST_CASE(overwrite_is_unsupported)
{
    Harness harness;
    const CommandResponse response = harness.registry.execute(
        make_caller(ControlSet::Admin, "rest"),
        import_request("csv", "data", "http://a.com/,aaa\n", "overwrite"));
    BOOST_TEST(response.ok == false);
    BOOST_TEST((response.error.code == CommandErrorCode::unsupported));
    BOOST_TEST(harness.runner->list(std::nullopt, 0, 100).total == 0u);
}

// Type: unit (write permission required / C7, T-101).
// Scenario: a ControlSet::User is denied not_authorized before the factory, so
//   nothing is queued.
// Boundaries: registry permission check at step 3, before the job path.
// On failure first check: did link.import lose its write permission?
BOOST_AUTO_TEST_CASE(user_caller_is_not_authorized)
{
    Harness harness;
    const CommandResponse response = harness.registry.execute(
        make_caller(ControlSet::User, "test"),
        import_request("csv", "data", "http://a.com/,aaa\n"));
    BOOST_TEST(response.ok == false);
    BOOST_TEST((response.error.code == CommandErrorCode::not_authorized));
    BOOST_TEST(harness.runner->list(std::nullopt, 0, 100).total == 0u);
}

// Type: unit (too many rows -> job failed, bounded / T-97).
// Scenario: a file of kMaxImportRows+1 malformed rows ends the job
//   failed/invalid_field without unbounded work; a dry-run over the same file
//   also fails bounded and mutates nothing.
// Boundaries: kMaxImportRows row cap in the body and in dry-run.
// On failure first check: did the row cap stop firing, or did dry-run skip it?
BOOST_AUTO_TEST_CASE(too_many_rows_fails_bounded_real_and_dry_run)
{
    Harness harness(/*allow_private=*/false, /*allow_paths=*/true);

    std::string content;
    content.reserve(
        (url_shortener::commands::kMaxImportRows + 1) * 6);
    for (std::size_t i = 0;
         i < url_shortener::commands::kMaxImportRows + 1;
         ++i) {
        content += "x,y,z\n";  // malformed: no store writes, bounded work
    }
    const std::string path = write_temp_file(content);

    const ImportResult result =
        run_real_import(harness, import_request("csv", "path", path));
    BOOST_TEST((result.state == JobState::failed));
    BOOST_TEST(result.fail_code == "invalid_field");
    BOOST_TEST(harness.store.size() == 0u);

    const CommandResponse dry = harness.registry.execute(
        admin_caller(),
        import_request("csv", "path", path, "", /*dry_run=*/true));
    BOOST_TEST(dry.ok == false);
    BOOST_TEST((dry.error.code == CommandErrorCode::invalid_field));
    BOOST_TEST(harness.store.create_calls() == 0u);

    std::filesystem::remove(path);
}

// Type: unit (cooperative cancel keeps a partial result / T-88, T-90).
// Scenario: a 100-row import gated at row 50 is cancelled; it ends cancelled
//   with created==50 and the store holds exactly 50 links; job.get reports the
//   partial counts.
// Boundaries: cancel polled once per row; partial result_json retained.
// On failure first check: did the body stop polling cancel per row, or did the
//   runner stop keeping the cancelled result?
BOOST_AUTO_TEST_CASE(cancel_mid_import_keeps_partial_counts)
{
    Harness harness;

    auto reached = std::make_shared<std::promise<void>>();
    auto release = std::make_shared<std::promise<void>>();
    std::shared_future<void> release_future = release->get_future().share();
    std::atomic<bool> reached_fired {false};

    harness.store.on_create =
        [reached, release_future, &reached_fired](std::size_t attempt)
    {
        if (attempt == 50) {
            if (!reached_fired.exchange(true)) {
                reached->set_value();
            }
            release_future.wait();
        }
    };

    std::string data;
    for (int i = 0; i < 100; ++i) {
        data += "http://host" + std::to_string(i) + ".example/,slug"
            + std::to_string(i) + "\n";
    }

    const CommandResponse response = harness.registry.execute(
        admin_caller(), import_request("csv", "data", data));
    BOOST_REQUIRE(response.job.has_value());
    const std::string job_id = response.job->id;

    reached->get_future().wait();   // worker is inside the 50th create
    harness.runner->cancel(job_id);  // set the cancel flag
    release->set_value();            // let the 50th create finish

    const std::optional<JobRecord> record =
        harness.runner->wait_for_terminal(job_id, std::chrono::seconds(20));
    BOOST_REQUIRE(record.has_value());
    BOOST_TEST((record->state == JobState::cancelled));
    BOOST_REQUIRE(record->result_json.has_value());
    BOOST_TEST(json_int(*record->result_json, "created") == 50);
    BOOST_TEST(harness.store.size() == 50u);
}

// Type: unit (no head-of-line blocking; progress advances).
// Scenario: a 10k-row file import blocked mid-stream still lets link.get answer
//   through the same registry and job.get report a running job with progress.
// Boundaries: a long job holds one worker; sync commands keep answering.
// On failure first check: did the fake store start blocking under its lock, or
//   did the body stop reporting progress?
BOOST_AUTO_TEST_CASE(long_import_does_not_block_other_commands)
{
    Harness harness(/*allow_private=*/false, /*allow_paths=*/true);

    auto reached = std::make_shared<std::promise<void>>();
    auto release = std::make_shared<std::promise<void>>();
    std::shared_future<void> release_future = release->get_future().share();
    std::atomic<bool> reached_fired {false};

    harness.store.on_create =
        [reached, release_future, &reached_fired](std::size_t attempt)
    {
        if (attempt == 5000) {
            if (!reached_fired.exchange(true)) {
                reached->set_value();
            }
            release_future.wait();
        }
    };

    std::string content;
    content.reserve(10000 * 32);
    for (int i = 0; i < 10000; ++i) {
        content += "http://example.com/" + std::to_string(i) + ",slug"
            + std::to_string(i) + "\n";
    }
    const std::string path = write_temp_file(content);

    const CommandResponse response = harness.registry.execute(
        admin_caller(), import_request("csv", "path", path));
    BOOST_REQUIRE(response.job.has_value());
    const std::string job_id = response.job->id;

    reached->get_future().wait();  // blocked at create 5000, lock not held

    // link.get answers while the import is blocked.
    CommandRequest get;
    get.id = "g";
    get.command = "link.get";
    get.args_json = "{\"slug\":\"slug0\"}";
    const CommandResponse got = harness.registry.execute(admin_caller(), get);
    BOOST_TEST(got.ok == true);

    // job.get shows the job running with progress advanced.
    CommandRequest job_get;
    job_get.id = "j";
    job_get.command = "job.get";
    job_get.args_json = "{\"id\":" + jsonString(job_id) + "}";
    const CommandResponse job_resp =
        harness.registry.execute(admin_caller(), job_get);
    BOOST_TEST(job_resp.ok == true);
    BOOST_TEST(job_resp.result_json.find("\"state\":\"running\"")
               != std::string::npos);
    BOOST_TEST(json_int(job_resp.result_json, "done") >= 4000);

    release->set_value();
    harness.runner->wait_for_terminal(job_id, std::chrono::seconds(30));
    std::filesystem::remove(path);
}
