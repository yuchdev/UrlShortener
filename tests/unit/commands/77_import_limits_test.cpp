/**
 * @file 77_import_limits_test.cpp
 * @brief Size and resource limits on `link.import` / `link.export` (Milestone
 * 0006, Task 07.0 subtask 20, threats T-97/T-109).
 *
 * Proves the import/export surface is size-bounded at every seam a crafted
 * payload could exploit: an inline `data` over `max_request_body_bytes`, a
 * `path` file over @ref kMaxImportFileBytes (rejected by the size seam before
 * reading), a single over-long line, the 100-entry error-report cap, CSV edge
 * cases that must never crash, and an inline export over @ref
 * kMaxInlineExportBytes. The filesystem size seam is exercised with a *sparse*
 * file so the 64 MiB bound is asserted without writing 64 MiB.
 */
#define BOOST_TEST_MODULE CommandsImportLimitsTest
#include <chrono>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <string>
#include <system_error>
#include <variant>
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
#include "url_shortener/commands/pair_file_access.hpp"
#include "url_shortener/commands/pair_line_parser.hpp"
#include "url_shortener/core/clock.hpp"
#include "url_shortener/core/types.hpp"
#include "url_shortener/security/control_set.hpp"

namespace fs = std::filesystem;

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
using url_shortener::commands::kMaxImportFileBytes;
using url_shortener::commands::kMaxImportLineBytes;
using url_shortener::commands::kMaxInlineExportBytes;
using url_shortener::commands::LineSource;
using url_shortener::commands::LinkListFilter;
using url_shortener::commands::LinkPage;
using url_shortener::commands::LinkPageSource;
using url_shortener::commands::PairCommandDeps;
using url_shortener::commands::PathError;
using url_shortener::commands::PathPolicy;
using url_shortener::commands::register_job_commands;
using url_shortener::commands::register_link_commands;
using url_shortener::commands::register_pair_commands;

namespace app = url_shortener::app;

namespace
{

/// A per-test temp directory removed in the destructor.
struct TempDir
{
    fs::path path;

    TempDir()
        : path(fs::temp_directory_path()
               / ("import_limits_"
                  + std::to_string(std::chrono::steady_clock::now()
                                       .time_since_epoch()
                                       .count())))
    {
        fs::create_directories(path);
    }

    ~TempDir()
    {
        std::error_code ec;
        fs::remove_all(path, ec);
    }
};

/// Registry wired with link + job + pair commands over a thread-safe fake
/// store.
struct Harness
{
    commands_test_fakes::FakeLinkStore store;
    commands_test_fakes::FakeStatsReader reader;
    ServerConfig config;
    ManualClock clock;
    std::shared_ptr<app::LinkCommandService> service;
    std::shared_ptr<JobRunner> runner;
    CommandRegistry registry;

    explicit Harness(std::size_t max_inline = 65536,
                     bool allow_paths = true,
                     LinkPageSource list_page = nullptr)
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
        deps.max_inline_bytes = max_inline;
        deps.file_paths.allow_paths = allow_paths;
        if (list_page) {
            deps.list_page = std::move(list_page);
        }
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

CommandRequest import_data(const std::string& format,
                           const std::string& data,
                           bool dry_run = false)
{
    CommandRequest request;
    request.id = "import-1";
    request.command = "link.import";
    request.args_json =
        "{\"format\":\"" + format + "\",\"data\":" + jsonString(data) + "}";
    request.options.dry_run = dry_run;
    return request;
}

/// Runs a real (non-dry) import job and returns its terminal state + result.
JobRecord run_import_job(Harness& harness, const CommandRequest& request)
{
    const CommandResponse response =
        harness.registry.execute(admin_caller(), request);
    BOOST_REQUIRE_MESSAGE(response.job.has_value(),
                          "expected a job, got: " + response.error.message);
    const std::optional<JobRecord> record = harness.runner->wait_for_terminal(
        response.job->id, std::chrono::seconds(20));
    BOOST_REQUIRE(record.has_value());
    return *record;
}

}  // namespace

// Type: unit (inline data over the body cap -> invalid_field, no job).
// Scenario: an inline `data` larger than deps.max_inline_bytes is refused by
//   from_inline before a job is queued.
// On failure first check: did the factory stop passing max_inline_bytes into
//   from_inline?
BOOST_AUTO_TEST_CASE(inline_data_over_max_body_rejected)
{
    Harness harness(/*max_inline=*/16);  // tiny cap; ArgSpec cap is far larger
    const CommandResponse response = harness.registry.execute(
        admin_caller(), import_data("csv", "http://a.example/,aaa\n"));

    BOOST_TEST(response.ok == false);
    BOOST_TEST((response.error.code == CommandErrorCode::invalid_field));
    BOOST_TEST(harness.runner->list(std::nullopt, 0, 100).total == 0u);
}

// Type: unit (file over the 64 MiB cap -> invalid_field at the size seam).
// Scenario: a sparse file larger than kMaxImportFileBytes is refused by
//   LineSource::from_path before any byte is read (the size check precedes the
//   read); a normal in-cap file opens.
// Boundaries: file_size > kMaxImportFileBytes; sparse so the test is cheap.
// On failure first check: did from_path stop stat-ing the size before reading?
BOOST_AUTO_TEST_CASE(file_size_cap)
{
    TempDir dir;
    PathPolicy policy;
    policy.allow_paths = true;

    // An oversized sparse file: resize_file sets the logical size without
    // writing 64 MiB of data, so the size seam sees it as over-cap.
    const fs::path big = dir.path / "big.csv";
    {
        std::error_code ec;
        std::ofstream(big).close();
        fs::resize_file(big, kMaxImportFileBytes + 1, ec);
        BOOST_REQUIRE_MESSAGE(!ec,
                              "could not create sparse file: " + ec.message());
    }

    const std::variant<LineSource, PathError> rejected =
        LineSource::from_path(big.string(), policy);
    BOOST_REQUIRE(std::holds_alternative<PathError>(rejected));
    BOOST_TEST(std::get<PathError>(rejected).code
               == std::string("invalid_field"));

    // A small file at the same policy opens fine (the cap is the only reason).
    const fs::path small = dir.path / "small.csv";
    {
        std::ofstream out(small, std::ios::binary);
        out << "http://a.example/,aaa\n";
    }
    const std::variant<LineSource, PathError> ok =
        LineSource::from_path(small.string(), policy);
    BOOST_TEST(std::holds_alternative<LineSource>(ok));
}

// Type: unit (single over-long line -> row failure, memory bounded).
// Scenario: a 1 MiB line (far over the kMaxImportLineBytes 16 KiB cap) in a
//   `path`-sourced file is counted failed - the over-cap bytes are consumed
//   without being stored (bounded memory) - and the following valid row still
//   imports. The file route is used because an inline `data` arg is byte-capped
//   by the registry long before 1 MiB, so only a file can carry a line that
//   large to exercise the per-line cap.
// On failure first check: did the body stop continuing past a too_long line, or
//   did read_bounded_line stop bounding its allocation?
BOOST_AUTO_TEST_CASE(single_line_length_cap)
{
    BOOST_REQUIRE(kMaxImportLineBytes < 1024u * 1024u);
    TempDir dir;
    const fs::path file = dir.path / "bigline.csv";
    {
        std::ofstream out(file, std::ios::binary);
        out << std::string(1024u * 1024u, 'a');  // 1 MiB single line
        out << "\nhttp://ok.example/,okslug\n";
    }

    Harness harness(/*max_inline=*/65536, /*allow_paths=*/true);
    CommandRequest request;
    request.id = "import-bigline";
    request.command = "link.import";
    request.args_json =
        "{\"format\":\"csv\",\"path\":" + jsonString(file.string()) + "}";

    const JobRecord record = run_import_job(harness, request);
    const std::string result = record.result_json.value_or("");

    BOOST_TEST((record.state == JobState::succeeded));
    BOOST_TEST(json_int(result, "failed") == 1);
    BOOST_TEST(json_int(result, "created") == 1);
    BOOST_TEST(harness.store.findBySlug("okslug").has_value());
}

// Type: unit (error report capped at 100 / T-109).
// Scenario: 1000 malformed rows cap the reported errors at 100 with failed=1000
//   and errors_omitted=900, bounding the response size.
// On failure first check: did the error list stop capping or forwarding the
//   omitted count?
BOOST_AUTO_TEST_CASE(errors_array_capped_at_100)
{
    Harness harness;
    std::string data;
    data.reserve(1000 * 6);
    for (int i = 0; i < 1000; ++i) {
        data += "x,y,z\n";  // three columns -> malformed
    }

    const JobRecord record = run_import_job(harness, import_data("csv", data));
    const std::string result = record.result_json.value_or("");

    BOOST_TEST(json_int(result, "total") == 1000);
    BOOST_TEST(json_int(result, "failed") == 1000);
    BOOST_TEST(json_int(result, "errors_omitted") == 900);

    std::size_t entries = 0;
    for (std::size_t p = result.find("\"line\":"); p != std::string::npos;
         p = result.find("\"line\":", p + 1))
    {
        ++entries;
    }
    BOOST_TEST(entries == 100u);
}

// Type: unit (CSV edge cases never crash, defined per-row outcomes).
// Scenario: quoted fields with embedded commas/newlines, a UTF-8 BOM, CRLF line
//   endings, blank lines, a trailing comma and 3+ columns all produce a clean
//   per-row outcome - the job completes and persists only the well-formed rows.
// Boundaries: no crash, no hang; malformed rows are counted, not fatal.
// On failure first check: did the CSV parser throw or loop on a quoted field or
//   a stray BOM?
BOOST_AUTO_TEST_CASE(csv_edge_cases)
{
    Harness harness;
    // BOM + header, a quoted target with an embedded comma, a CRLF row, a blank
    // line, a trailing-comma row, and a 3-column malformed row.
    std::string data = "\xEF\xBB\xBF";  // UTF-8 BOM
    data += "full_url,slug\r\n";  // header (CRLF)
    data += "\"http://a.example/x,y\",qalpha\r\n";  // quoted embedded comma
    data += "http://b.example/,bbb\n";  // plain row
    data += "\n";  // blank line
    data += "http://c.example/,ccc,extra\n";  // 3 columns -> malformed
    data += "http://d.example/,\n";  // trailing comma -> empty slug, generated

    const JobRecord record = run_import_job(harness, import_data("csv", data));
    const std::string result = record.result_json.value_or("");

    // The job finishes (no crash/hang) and only valid rows persist.
    BOOST_TEST((record.state == JobState::succeeded));
    BOOST_TEST(json_int(result, "failed") >= 1);  // the 3-column row at least
    BOOST_TEST(harness.store.findBySlug("bbb").has_value());
    // The quoted embedded comma is one field, so its slug is the second column.
    BOOST_TEST(harness.store.findBySlug("qalpha").has_value());
}

// Type: unit (inline export over the cap -> job failed/invalid_field, no data).
// Scenario: an inline (no path) export whose serialized bytes exceed
//   kMaxInlineExportBytes ends the job failed/invalid_field and emits no
//   partial `data`.
// On failure first check: did the inline export stop checking the running size
//   against kMaxInlineExportBytes?
BOOST_AUTO_TEST_CASE(export_data_size_cap)
{
    const std::string long_target =
        "http://example.com/" + std::string(900, 'p');
    const std::size_t rows = (kMaxInlineExportBytes / long_target.size()) + 8;

    auto page = std::make_shared<std::vector<app::LinkView>>();
    page->reserve(rows);
    for (std::size_t i = 0; i < rows; ++i) {
        app::LinkView view;
        view.id = "id" + std::to_string(i);
        view.slug = "slug" + std::to_string(i);
        view.target_url = long_target + std::to_string(i);
        view.short_url = "http://sho.rt/slug" + std::to_string(i);
        view.created_at = "2025-01-01T00:00:00Z";
        view.updated_at = "2025-01-01T00:00:00Z";
        view.enabled = true;
        view.redirect_type = url_shortener::RedirectType::temporary;
        page->push_back(view);
    }

    LinkPageSource source = [page](const LinkListFilter&,
                                   std::size_t offset,
                                   std::size_t limit) -> app::Result<LinkPage>
    {
        app::Result<LinkPage> result;
        LinkPage out;
        out.total = page->size();
        for (std::size_t i = offset; i < page->size() && i < offset + limit;
             ++i)
        {
            out.items.push_back((*page)[i]);
        }
        result.value = std::move(out);
        return result;
    };

    Harness harness(65536, /*allow_paths=*/false, std::move(source));

    CommandRequest request;
    request.id = "export-big";
    request.command = "link.export";
    request.args_json = "{\"format\":\"csv\"}";  // inline (no path)

    const JobRecord record = run_import_job(harness, request);
    const std::string result = record.result_json.value_or("");

    BOOST_TEST((record.state == JobState::failed));
    BOOST_REQUIRE(record.failure.has_value());
    BOOST_TEST(record.failure->code == std::string("invalid_field"));
    // No partial inline payload is leaked in the failure path.
    BOOST_TEST(result.find("\"data\":\"") == std::string::npos);
}
