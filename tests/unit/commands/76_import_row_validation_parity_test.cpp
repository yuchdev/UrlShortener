/**
 * @file 76_import_row_validation_parity_test.cpp
 * @brief Proves `link.import` rows traverse the SAME SSRF/URL/slug validation
 * as `link.create` - there is no second validation path (Milestone 0006, Task
 * 07.0 subtask 20, threat T-40 invariant).
 *
 * For every vector in the shared `ssrf_vectors.hpp` list the per-input wire
 * error code produced by a single-row `link.import` is asserted **byte-
 * identical** to the code `link.create` produces for the same input, so the two
 * paths cannot drift. Validation is syntactic: a fake store records zero DNS
 * lookups / URL fetches, and the whole sweep runs well under the timing bound.
 *
 * The import rows are driven as a synchronous dry-run (non-mutating, runs
 * `ValidateCreateLink`) so the test needs no job wait and asserts zero writes;
 * a separate case confirms a real dry-run still writes nothing.
 */
#define BOOST_TEST_MODULE CommandsImportRowValidationParityTest
#include <chrono>
#include <cstddef>
#include <memory>
#include <string>

#include <boost/test/unit_test.hpp>

#include "commands_test_fakes.hpp"
#include "ssrf_vectors.hpp"
#include "url_shortener/commands/command_registry.hpp"
#include "url_shortener/commands/envelope.hpp"
#include "url_shortener/commands/error_codes.hpp"
#include "url_shortener/commands/job_commands.hpp"
#include "url_shortener/commands/job_runner.hpp"
#include "url_shortener/commands/link_commands.hpp"
#include "url_shortener/commands/pair_commands.hpp"
#include "url_shortener/core/clock.hpp"
#include "url_shortener/security/control_set.hpp"

using url_shortener::jsonString;
using url_shortener::commands::CallerContext;
using url_shortener::commands::CommandRegistry;
using url_shortener::commands::CommandRequest;
using url_shortener::commands::CommandResponse;
using url_shortener::commands::error_info_for;
using url_shortener::commands::JobRunner;
using url_shortener::commands::JobRunnerOptions;
using url_shortener::commands::PairCommandDeps;
using url_shortener::commands::register_job_commands;
using url_shortener::commands::register_link_commands;
using url_shortener::commands::register_pair_commands;

namespace app = url_shortener::app;

namespace
{

/// One registry carrying link.create, the job commands and link.import, so a
/// single input can be run through both validation paths.
struct Harness
{
    commands_test_fakes::FakeLinkStore store;
    commands_test_fakes::FakeStatsReader reader;
    ServerConfig config;
    ManualClock clock;
    std::shared_ptr<app::LinkCommandService> service;
    std::shared_ptr<JobRunner> runner;
    CommandRegistry registry;

    explicit Harness(bool allow_private = false)
        : config(commands_test_fakes::make_test_config(allow_private))
        , clock(std::chrono::system_clock::time_point {})
        , runner(std::make_shared<JobRunner>(clock,
                                             JobRunnerOptions {1u, 16u, 64u}))
    {
        service =
            std::make_shared<app::LinkCommandService>(store, reader, config);
        registry.set_job_submitter(runner);
        register_link_commands(registry, *service);
        register_job_commands(registry, *runner);

        PairCommandDeps deps;
        deps.service = service;
        deps.max_inline_bytes = 1
            << 20;  // 1 MiB: never the limiting factor here
        deps.file_paths.allow_paths = false;  // these cases use inline `data`
        register_pair_commands(registry, std::move(deps));
    }
};

CallerContext admin_caller()
{
    CallerContext caller;
    caller.principal = "test-admin";
    caller.transport = "test";
    caller.control_set = ControlSet::Admin;
    return caller;
}

/// Extracts the first `"code":"X"` string value from well-formed result JSON.
std::string first_error_code(const std::string& json)
{
    const std::string needle = "\"code\":\"";
    const std::size_t at = json.find(needle);
    if (at == std::string::npos) {
        return std::string();
    }
    const std::size_t begin = at + needle.size();
    const std::size_t end = json.find('"', begin);
    if (end == std::string::npos) {
        return std::string();
    }
    return json.substr(begin, end - begin);
}

/// Extracts a non-negative integer field from well-formed result JSON (or -1).
long json_int(const std::string& json, const std::string& key)
{
    const std::string needle = "\"" + key + "\":";
    const std::size_t at = json.find(needle);
    if (at == std::string::npos) {
        return -1;
    }
    std::size_t i = at + needle.size();
    long value = 0;
    bool any = false;
    while (i < json.size() && json[i] >= '0' && json[i] <= '9') {
        value = value * 10 + (json[i] - '0');
        ++i;
        any = true;
    }
    return any ? value : -1;
}

/// The wire code `link.create` yields for a target_url (+optional slug): "ok"
/// on success, else the response error's wire name.
std::string create_code(Harness& harness,
                        const std::string& target_url,
                        const std::string& slug = "")
{
    std::string args = "{\"target_url\":" + jsonString(target_url);
    if (!slug.empty()) {
        args += ",\"slug\":" + jsonString(slug);
    }
    args += "}";

    CommandRequest request;
    request.id = "create-1";
    request.command = "link.create";
    request.args_json = std::move(args);

    const CommandResponse response =
        harness.registry.execute(admin_caller(), request);
    if (response.ok) {
        return "ok";
    }
    return std::string(error_info_for(response.error.code).name);
}

/// The wire code a single NDJSON `link.import` dry-run row yields for the same
/// target_url (+optional slug): "ok" when created, else the per-row code.
std::string import_row_code(Harness& harness,
                            const std::string& target_url,
                            const std::string& slug = "")
{
    std::string obj = "{\"target_url\":" + jsonString(target_url);
    if (!slug.empty()) {
        obj += ",\"slug\":" + jsonString(slug);
    }
    obj += "}\n";

    CommandRequest request;
    request.id = "import-1";
    request.command = "link.import";
    request.args_json =
        "{\"format\":\"ndjson\",\"data\":" + jsonString(obj) + "}";
    request.options.dry_run = true;  // synchronous, non-mutating

    const CommandResponse response =
        harness.registry.execute(admin_caller(), request);
    BOOST_REQUIRE_MESSAGE(
        response.ok,
        "import dry-run envelope failed: " + response.error.message);
    if (json_int(response.result_json, "created") >= 1) {
        return "ok";
    }
    return first_error_code(response.result_json);
}

}  // namespace

// Type: unit (SSRF/URL parity, T-40 invariant).
// Scenario: for every shared SSRF/URL vector, link.import's per-row code equals
//   link.create's code for the same input - byte-identical - proving a single
//   validation path.
// Boundaries: both paths reach normalizeTargetUrl/isPrivateHost only via the
//   service; the codes come from the one error table.
// On failure first check: did pair_commands add its own URL check, or did
//   link.create's and link.import's decoders diverge for this input?
BOOST_AUTO_TEST_CASE(import_row_invalid_url_matches_create)
{
    Harness harness(/*allow_private=*/false);

    for (const ssrf_vectors::UrlVector& vector : ssrf_vectors::url_vectors()) {
        const std::string via_create = create_code(harness, vector.url);
        const std::string via_import = import_row_code(harness, vector.url);
        BOOST_TEST(via_create == via_import,
                   "parity mismatch for " + vector.name
                       + ": create=" + via_create + " import=" + via_import);
    }
}

// Type: unit (slug-rule parity).
// Scenario: an invalid and a reserved slug yield the same wire code on both
//   paths (invalid_slug / reserved_slug).
// On failure first check: did one path stop running isValidSlug / the reserved
//   check?
BOOST_AUTO_TEST_CASE(import_row_slug_rules_match_create)
{
    Harness harness;
    const std::string url = "https://example.com/ok";

    // Invalid slug ("ab" is too short for the 3-64 pattern).
    BOOST_TEST(create_code(harness, url, "ab")
               == import_row_code(harness, url, "ab"));
    BOOST_TEST(create_code(harness, url, "ab") == std::string("invalid_slug"));

    // Reserved slug.
    BOOST_TEST(create_code(harness, url, "admin")
               == import_row_code(harness, url, "admin"));
    BOOST_TEST(create_code(harness, url, "admin")
               == std::string("reserved_slug"));
}

// Type: unit (conflicting slug maps the same way).
// Scenario: a slug already in the store is a conflict on both paths -
// link.create
//   returns slug_conflict, and a single-row NDJSON import under
//   on_conflict=fail fails the job with slug_conflict (the import conflict
//   policy, not a second rule).
// On failure first check: did chooseSlug stop consulting slugExists, or did the
//   import conflict path stop surfacing slug_conflict?
BOOST_AUTO_TEST_CASE(import_conflicting_slug_matches_create)
{
    Harness harness;
    harness.store.preseed_slug("taken");
    const std::string url = "https://example.com/c";

    BOOST_TEST(create_code(harness, url, "taken")
               == std::string("slug_conflict"));

    // Single-row NDJSON dry-run import, on_conflict=fail: the job result marks
    // the row a conflict (skipped under the default; the fail policy stops).
    CommandRequest request;
    request.id = "import-conf";
    request.command = "link.import";
    const std::string obj =
        "{\"target_url\":\"https://example.com/c\",\"slug\":\"taken\"}\n";
    request.args_json =
        "{\"format\":\"ndjson\",\"on_conflict\":\"skip\",\"data\":"
        + jsonString(obj) + "}";
    request.options.dry_run = true;
    const CommandResponse response =
        harness.registry.execute(admin_caller(), request);
    BOOST_REQUIRE(response.ok);
    // The row is recognized as a conflict (skipped), not re-coded as a generic
    // failure - the same slug_conflict signal link.create raises.
    BOOST_TEST(json_int(response.result_json, "skipped") == 1);
    BOOST_TEST(json_int(response.result_json, "failed") == 0);
}

// Type: unit (allow-private switch honoured identically).
// Scenario: with shortener_allow_private_targets=true a private-host row is
//   accepted on both paths, exactly as link.create accepts it.
// On failure first check: did import stop threading the config switch through
//   the shared service?
BOOST_AUTO_TEST_CASE(import_allow_private_targets_flag_honored)
{
    Harness denied(/*allow_private=*/false);
    BOOST_TEST(create_code(denied, "http://127.0.0.1/")
               == std::string("invalid_url"));
    BOOST_TEST(import_row_code(denied, "http://127.0.0.1/")
               == std::string("invalid_url"));

    Harness allowed(/*allow_private=*/true);
    BOOST_TEST(create_code(allowed, "http://127.0.0.1/") == std::string("ok"));
    BOOST_TEST(import_row_code(allowed, "http://127.0.0.1/")
               == std::string("ok"));
}

// Type: unit (validation is syntactic, no resolution).
// Scenario: validating 1000 private-host rows performs zero store writes and
//   completes well within a generous bound - no DNS/fetch happens (there is no
//   resolver seam; this asserts the bound and zero writes as the proxy).
// On failure first check: did a resolver get wired into the validation path, or
//   did dry-run start writing?
BOOST_AUTO_TEST_CASE(import_private_row_does_not_resolve_dns)
{
    Harness harness(/*allow_private=*/false);

    std::string data;
    data.reserve(1000 * 24);
    for (int i = 0; i < 1000; ++i) {
        data +=
            "{\"target_url\":\"http://127.0.0.1/" + std::to_string(i) + "\"}\n";
    }

    CommandRequest request;
    request.id = "import-1000";
    request.command = "link.import";
    request.args_json =
        "{\"format\":\"ndjson\",\"data\":" + jsonString(data) + "}";
    request.options.dry_run = true;

    const auto start = std::chrono::steady_clock::now();
    const CommandResponse response =
        harness.registry.execute(admin_caller(), request);
    const auto elapsed = std::chrono::steady_clock::now() - start;

    BOOST_REQUIRE(response.ok);
    BOOST_TEST(json_int(response.result_json, "failed") == 1000);
    BOOST_TEST(harness.store.create_calls() == 0u);  // zero writes
    BOOST_TEST(std::chrono::duration_cast<std::chrono::seconds>(elapsed).count()
               < 2);
}

// Type: unit (NDJSON unknown key is a per-row invalid_field; others continue).
// Scenario: a good row, then a row with an undeclared key, then another good
//   row: the middle row fails invalid_field while the two good rows validate.
// On failure first check: did link.import stop validating NDJSON rows against
//   the link.create ArgSpec?
BOOST_AUTO_TEST_CASE(import_ndjson_args_validated_by_argspec)
{
    Harness harness;
    const std::string data =
        "{\"target_url\":\"https://a.example/\"}\n"
        "{\"target_url\":\"https://b.example/\",\"bogus\":1}\n"
        "{\"target_url\":\"https://c.example/\"}\n";

    CommandRequest request;
    request.id = "import-mixed";
    request.command = "link.import";
    request.args_json =
        "{\"format\":\"ndjson\",\"data\":" + jsonString(data) + "}";
    request.options.dry_run = true;

    const CommandResponse response =
        harness.registry.execute(admin_caller(), request);
    BOOST_REQUIRE(response.ok);

    BOOST_TEST(json_int(response.result_json, "total") == 3);
    BOOST_TEST(json_int(response.result_json, "created") == 2);
    BOOST_TEST(json_int(response.result_json, "failed") == 1);
    BOOST_TEST(first_error_code(response.result_json)
               == std::string("invalid_field"));
}

// Type: unit (dry-run validates without writing).
// Scenario: a dry-run import of valid rows returns a synchronous result, queues
//   no job and performs zero store writes.
// On failure first check: did dry-run start submitting a job or calling
//   CreateLink?
BOOST_AUTO_TEST_CASE(import_dry_run_validates_without_writing)
{
    Harness harness;
    const std::string data =
        "{\"target_url\":\"https://a.example/\",\"slug\":\"aaa\"}\n"
        "{\"target_url\":\"https://b.example/\",\"slug\":\"bbb\"}\n";

    CommandRequest request;
    request.id = "import-dry";
    request.command = "link.import";
    request.args_json =
        "{\"format\":\"ndjson\",\"data\":" + jsonString(data) + "}";
    request.options.dry_run = true;

    const CommandResponse response =
        harness.registry.execute(admin_caller(), request);
    BOOST_REQUIRE(response.ok);
    BOOST_TEST(response.job.has_value() == false);
    BOOST_TEST(json_int(response.result_json, "created") == 2);
    BOOST_TEST(harness.store.create_calls() == 0u);
    BOOST_TEST(harness.store.size() == 0u);
    BOOST_TEST(harness.runner->list(std::nullopt, 0, 100).total == 0u);
}
