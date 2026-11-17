#define BOOST_TEST_MODULE CommandsLinkExportCommandTest
#include <atomic>
#include <cctype>
#include <chrono>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <functional>
#include <future>
#include <memory>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include <boost/test/unit_test.hpp>

#include "commands_test_fakes.hpp"
#include "url_shortener/commands/arg_spec.hpp"
#include "url_shortener/commands/audit.hpp"
#include "url_shortener/commands/command_registry.hpp"
#include "url_shortener/commands/envelope.hpp"
#include "url_shortener/commands/error_codes.hpp"
#include "url_shortener/commands/job_commands.hpp"
#include "url_shortener/commands/job_runner.hpp"
#include "url_shortener/commands/job_types.hpp"
#include "url_shortener/commands/link_command_codec.hpp"
#include "url_shortener/commands/link_commands.hpp"
#include "url_shortener/commands/pair_commands.hpp"
#include "url_shortener/commands/pair_file_access.hpp"
#include "url_shortener/commands/pair_formatter.hpp"
#include "url_shortener/commands/pair_line_parser.hpp"
#include "url_shortener/core/clock.hpp"
#include "url_shortener/core/types.hpp"
#include "url_shortener/core/utils.hpp"
#include "url_shortener/security/control_set.hpp"

using url_shortener::isValidSlug;
using url_shortener::jsonString;
using url_shortener::RedirectType;
using url_shortener::commands::ArgsView;
using url_shortener::commands::AuditRecord;
using url_shortener::commands::CallerContext;
using url_shortener::commands::CommandErrorCode;
using url_shortener::commands::CommandError;
using url_shortener::commands::CommandRegistry;
using url_shortener::commands::CommandRequest;
using url_shortener::commands::CommandResponse;
using url_shortener::commands::format_csv_row;
using url_shortener::commands::format_ndjson_row;
using url_shortener::commands::IAuditSink;
using url_shortener::commands::JobRecord;
using url_shortener::commands::JobRunner;
using url_shortener::commands::JobRunnerOptions;
using url_shortener::commands::JobState;
using url_shortener::commands::kExportPageSize;
using url_shortener::commands::kMaxInlineExportBytes;
using url_shortener::commands::LinkListFilter;
using url_shortener::commands::LinkPage;
using url_shortener::commands::LinkPageSource;
using url_shortener::commands::link_create_arg_specs;
using url_shortener::commands::PairCommandDeps;
using url_shortener::commands::PairFormat;
using url_shortener::commands::PairLineParser;
using url_shortener::commands::ParsedRow;
using url_shortener::commands::register_job_commands;
using url_shortener::commands::register_link_commands;
using url_shortener::commands::register_pair_commands;
using url_shortener::commands::RowKind;
using url_shortener::commands::validate_args;

namespace app = url_shortener::app;

namespace
{

// A caller with the given role and transport. Inline export needs `read`; a
// `path` export needs `write` (Admin) through the permission hook.
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

// A page source double: returns a window of pre-built views, records every
// (offset, limit) request and the filter it was called with, can fail, and can
// block on a chosen page to gate a cancellation mid-export.
struct FakePageSource
{
    std::vector<app::LinkView> links;
    bool fail = false;
    app::AppErrorCode fail_code = app::AppErrorCode::storage_failure;
    std::vector<std::pair<std::size_t, std::size_t>> calls;
    std::vector<LinkListFilter> filters;
    std::function<void(std::size_t)> on_page;  // keyed by offset
};

// Wraps a shared FakePageSource as a LinkPageSource; the closure copy the body
// captures still drives the same shared fake, so the test can read it after the
// job terminates.
LinkPageSource make_source(std::shared_ptr<FakePageSource> fake)
{
    return [fake](const LinkListFilter& filter, std::size_t offset,
                  std::size_t limit) -> app::Result<LinkPage>
    {
        fake->calls.emplace_back(offset, limit);
        fake->filters.push_back(filter);
        if (fake->on_page) {
            fake->on_page(offset);
        }
        app::Result<LinkPage> result;
        if (fake->fail) {
            result.error.code = fake->fail_code;
            result.error.detail = "list failed";
            return result;
        }
        LinkPage page;
        page.total = fake->links.size();
        for (std::size_t i = offset; i < fake->links.size() && i < offset + limit;
             ++i)
        {
            page.items.push_back(fake->links[i]);
        }
        result.value = std::move(page);
        return result;
    };
}

// Builds a minimal LinkView with a stable slug and target.
app::LinkView make_view(const std::string& slug, const std::string& target)
{
    app::LinkView view;
    view.id = "id-" + slug;
    view.slug = slug;
    view.target_url = target;
    view.short_url = "http://sho.rt/" + slug;
    view.created_at = "2025-01-01T00:00:00Z";
    view.updated_at = "2025-01-01T00:00:00Z";
    view.enabled = true;
    view.redirect_type = RedirectType::temporary;
    return view;
}

// Records every AuditRecord the registry emits, in emission order, so a test
// can assert a permission-denied export produced no `job_accepted` line
// (threat model T-108). Installed on the registry via a non-owning aliasing
// shared_ptr held by the Harness.
struct CapturingAuditSink : public IAuditSink
{
    std::vector<AuditRecord> records;

    void record(const AuditRecord& record) noexcept override
    {
        records.push_back(record);
    }
};

// Owns the fakes, service, runner and a registry wired with link, job and pair
// commands. `source` is bound as the export page source only when non-null, so a
// null source reproduces the "export unavailable" composition.
struct Harness
{
    commands_test_fakes::FakeLinkStore store;
    commands_test_fakes::FakeStatsReader reader;
    ServerConfig config;
    ManualClock clock;
    std::shared_ptr<app::LinkCommandService> service;
    std::shared_ptr<JobRunner> runner;
    std::shared_ptr<FakePageSource> source;
    std::shared_ptr<CapturingAuditSink> audit;
    CommandRegistry registry;

    explicit Harness(std::shared_ptr<FakePageSource> src,
                     bool allow_paths = false,
                     unsigned workers = 2)
        : config(commands_test_fakes::make_test_config())
        , clock(std::chrono::system_clock::time_point {})
        , runner(std::make_shared<JobRunner>(clock,
                                             JobRunnerOptions {workers, 64, 256}))
        , source(std::move(src))
        , audit(std::make_shared<CapturingAuditSink>())
    {
        service =
            std::make_shared<app::LinkCommandService>(store, reader, config);
        registry.set_audit_sink(audit);
        registry.set_job_submitter(runner);
        register_link_commands(registry, *service);
        register_job_commands(registry, *runner);

        PairCommandDeps deps;
        deps.service = service;
        deps.max_inline_bytes = 65536;
        deps.file_paths.allow_paths = allow_paths;
        if (source) {
            deps.list_page = make_source(source);
        }
        register_pair_commands(registry, std::move(deps));
    }
};

// Builds a `link.export` request from an argument object body.
CommandRequest export_request(const std::string& args_json)
{
    CommandRequest request;
    request.id = "export-1";
    request.command = "link.export";
    request.args_json = args_json;
    return request;
}

// Terminal outcome of a job.
struct JobResult
{
    JobState state = JobState::failed;
    std::string result_json;
    std::string fail_code;
};

// Submits a request, waits for the job to finish and returns its terminal
// record fields.
JobResult run_job(Harness& harness, const CommandRequest& request,
                  const CallerContext& caller)
{
    const CommandResponse response = harness.registry.execute(caller, request);
    BOOST_REQUIRE_MESSAGE(response.job.has_value(),
                          "expected a job, got error: " + response.error.message);
    const std::optional<JobRecord> record = harness.runner->wait_for_terminal(
        response.job->id, std::chrono::seconds(20));
    BOOST_REQUIRE(record.has_value());

    JobResult out;
    out.state = record->state;
    out.result_json = record->result_json.value_or("");
    if (record->failure.has_value()) {
        out.fail_code = record->failure->code;
    }
    return out;
}

// Counts the `job_accepted` audit records captured so far. A permission-denied
// export must leave this at zero: `execute` denies before the job factory runs,
// so no acceptance is audited (threat model T-108). `link.export` is a
// non-mutating command, so it emits no audit line at all; this directly proves a
// denied export produced none, rather than inferring it from the job-store count.
std::size_t count_job_accepted(const CapturingAuditSink& sink)
{
    std::size_t count = 0;
    for (const AuditRecord& record : sink.records) {
        if (record.outcome == "job_accepted") {
            ++count;
        }
    }
    return count;
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
           && std::isdigit(static_cast<unsigned char>(json[i])) != 0)
    {
        value = value * 10 + (json[i] - '0');
        ++i;
        any = true;
    }
    BOOST_REQUIRE(any);
    return value;
}

// Extracts and un-escapes the JSON string value of `data` in a result object,
// handling the escapes `jsonString` can emit for CSV/NDJSON text.
std::string extract_data(const std::string& json)
{
    const std::string needle = "\"data\":";
    const std::size_t at = json.find(needle);
    BOOST_REQUIRE(at != std::string::npos);
    std::size_t i = at + needle.size();
    if (json.compare(i, 4, "null") == 0) {
        return {};  // sentinel: no inline data
    }
    BOOST_REQUIRE(json[i] == '"');
    ++i;
    std::string out;
    while (i < json.size() && json[i] != '"') {
        if (json[i] == '\\' && i + 1 < json.size()) {
            const char esc = json[i + 1];
            switch (esc) {
                case 'n': out.push_back('\n'); break;
                case 't': out.push_back('\t'); break;
                case 'r': out.push_back('\r'); break;
                case '"': out.push_back('"'); break;
                case '\\': out.push_back('\\'); break;
                case '/': out.push_back('/'); break;
                default: out.push_back(esc); break;
            }
            i += 2;
            continue;
        }
        out.push_back(json[i]);
        ++i;
    }
    return out;
}

// A fresh, empty directory under the system temp dir, for file-mode tests.
std::filesystem::path make_unique_dir()
{
    static std::atomic<std::uint64_t> counter {0};
    const std::filesystem::path dir = std::filesystem::temp_directory_path()
        / ("export_test_"
           + std::to_string(
               std::chrono::steady_clock::now().time_since_epoch().count())
           + "_" + std::to_string(counter.fetch_add(1)));
    std::filesystem::create_directories(dir);
    return dir;
}

std::size_t count_dir_entries(const std::filesystem::path& dir)
{
    std::size_t n = 0;
    for (const auto& entry : std::filesystem::directory_iterator(dir)) {
        (void)entry;
        ++n;
    }
    return n;
}

std::string read_file(const std::filesystem::path& path)
{
    std::ifstream in(path, std::ios::binary);
    std::ostringstream os;
    os << in.rdbuf();
    return os.str();
}

// Splits a stream on '\n' and drops a trailing empty element.
std::vector<std::string> split_lines(const std::string& text)
{
    std::vector<std::string> lines;
    std::string current;
    for (const char c : text) {
        if (c == '\n') {
            lines.push_back(current);
            current.clear();
        }
        else {
            current.push_back(c);
        }
    }
    if (!current.empty()) {
        lines.push_back(current);
    }
    return lines;
}

}  // namespace

// Type: unit (CSV inline header + rows and the rows count).
// Scenario: three links exported inline as CSV yield the header line followed
//   by one row per link, with result `rows` equal to 3.
// Boundaries: csv_header() emitted once; one format_csv_row per item.
// On failure first check: did the body stop writing the header, or miscount
//   rows?
BOOST_AUTO_TEST_CASE(csv_inline_header_and_rows)
{
    auto source = std::make_shared<FakePageSource>();
    source->links = {make_view("aaa", "http://example.com/0"),
                     make_view("bbb", "http://example.com/1"),
                     make_view("ccc", "http://example.com/2")};
    Harness harness(source);

    const JobResult result =
        run_job(harness, export_request("{\"format\":\"csv\"}"), admin_caller());

    BOOST_TEST((result.state == JobState::succeeded));
    BOOST_TEST(json_int(result.result_json, "rows") == 3);
    BOOST_TEST(result.result_json.find("\"path\":null") != std::string::npos);

    const std::vector<std::string> lines = split_lines(extract_data(
        result.result_json));
    BOOST_REQUIRE(lines.size() == 4u);  // header + 3 rows
    BOOST_TEST(lines[0] == "full_url,slug");
    BOOST_TEST(lines[1] == "http://example.com/0,aaa");
    BOOST_TEST(lines[3] == "http://example.com/2,ccc");
}

// Type: unit (NDJSON row keys are exactly the link.create arg set).
// Scenario: an NDJSON row for a link with tags+metadata but no expiry/campaign
//   validates against link_create_arg_specs, omits server-assigned fields, and
//   grows expires_at/campaign only when the link carries them.
// Boundaries: key allowlist = link.create schema; optional keys gated on
//   presence.
// On failure first check: did format_ndjson_row start emitting id/short_url/
//   timestamps/status, or drop a required create key?
BOOST_AUTO_TEST_CASE(ndjson_keys_are_the_link_create_set)
{
    app::LinkView view = make_view("ndj", "http://nd.example/");
    view.tags = {"x", "y"};
    view.metadata = {{"k", "v"}};

    const std::string row = format_ndjson_row(view);
    BOOST_REQUIRE(!row.empty() && row.back() == '\n');
    const std::string object = row.substr(0, row.size() - 1);

    // The row validates as a link.create args object (re-importable, C1).
    ArgsView parsed;
    CommandError error;
    BOOST_TEST(validate_args(link_create_arg_specs(), object, &parsed, &error));

    // Required keys present; optional keys absent; server fields never emitted.
    for (const char* key :
         {"target_url", "slug", "redirect_type", "enabled", "tags", "metadata"})
    {
        BOOST_TEST(object.find(std::string("\"") + key + "\"")
                   != std::string::npos);
    }
    BOOST_TEST(object.find("\"expires_at\"") == std::string::npos);
    BOOST_TEST(object.find("\"campaign\"") == std::string::npos);
    for (const char* banned : {"\"id\"", "\"short_url\"", "\"created_at\"",
                               "\"updated_at\"", "\"status\""})
    {
        BOOST_TEST(object.find(banned) == std::string::npos);
    }

    // With expiry and campaign the two optional keys appear.
    view.expires_at = "2030-01-01T00:00:00Z";
    url_shortener::Link::Campaign campaign;
    campaign.name = "spring";
    view.campaign = campaign;
    const std::string row2 = format_ndjson_row(view);
    BOOST_TEST(row2.find("\"expires_at\"") != std::string::npos);
    BOOST_TEST(row2.find("\"campaign\"") != std::string::npos);
}

// Type: unit (zero matching links).
// Scenario: with no links the CSV export is a header-only payload and the
//   NDJSON export is empty; both report rows:0 and succeed.
// Boundaries: empty first page -> set_total(0), loop breaks.
// On failure first check: did the body emit a phantom row, or skip the header
//   for an empty CSV export?
BOOST_AUTO_TEST_CASE(zero_matching_links_header_only_or_empty)
{
    {
        auto source = std::make_shared<FakePageSource>();
        Harness harness(source);
        const JobResult result = run_job(
            harness, export_request("{\"format\":\"csv\"}"), admin_caller());
        BOOST_TEST((result.state == JobState::succeeded));
        BOOST_TEST(json_int(result.result_json, "rows") == 0);
        BOOST_TEST(extract_data(result.result_json) == "full_url,slug\n");
    }
    {
        auto source = std::make_shared<FakePageSource>();
        Harness harness(source);
        const JobResult result = run_job(
            harness, export_request("{\"format\":\"ndjson\"}"), admin_caller());
        BOOST_TEST((result.state == JobState::succeeded));
        BOOST_TEST(json_int(result.result_json, "rows") == 0);
        BOOST_TEST(extract_data(result.result_json).empty());
    }
}

// Type: unit (filters forwarded verbatim to the page source).
// Scenario: status/q/tag/campaign arguments reach the first list_page call
//   unchanged; status=all (sent by the TUI) is accepted.
// Boundaries: factory builds LinkListFilter straight from the args.
// On failure first check: did the factory rename a filter, or reject status=all?
BOOST_AUTO_TEST_CASE(filters_are_forwarded_verbatim)
{
    auto source = std::make_shared<FakePageSource>();
    Harness harness(source);

    const JobResult result = run_job(
        harness,
        export_request("{\"format\":\"csv\",\"status\":\"disabled\",\"q\":"
                       "\"foo\",\"tag\":\"bar\",\"campaign\":\"baz\"}"),
        admin_caller());
    BOOST_TEST((result.state == JobState::succeeded));

    BOOST_REQUIRE(!source->filters.empty());
    BOOST_TEST(source->filters.front().status == "disabled");
    BOOST_TEST(source->filters.front().q == "foo");
    BOOST_TEST(source->filters.front().tag == "bar");
    BOOST_TEST(source->filters.front().campaign == "baz");

    // status=all is accepted (TUI sends it).
    auto all_source = std::make_shared<FakePageSource>();
    Harness all(all_source);
    const JobResult all_result = run_job(
        all, export_request("{\"format\":\"csv\",\"status\":\"all\"}"),
        admin_caller());
    BOOST_TEST((all_result.state == JobState::succeeded));
    BOOST_TEST(all_source->filters.front().status == "all");
}

// Type: unit (pagination boundaries: every row exactly once).
// Scenario: for 0, 199, 200, 201 and 401 links the export's `rows` equals the
//   count exactly, so no boundary at the 200-row page size drops or repeats a
//   row.
// Boundaries: kExportPageSize edges at 200.
// On failure first check: did the offset advance by the wrong step, or the
//   empty/`offset>=total` stop condition misfire?
BOOST_AUTO_TEST_CASE(pagination_boundaries_emit_each_row_once)
{
    for (const std::size_t n : {std::size_t {0}, std::size_t {199},
                                std::size_t {200}, std::size_t {201},
                                std::size_t {401}})
    {
        auto source = std::make_shared<FakePageSource>();
        for (std::size_t i = 0; i < n; ++i) {
            source->links.push_back(make_view(
                "slug" + std::to_string(1000 + i),
                "http://example.com/" + std::to_string(i)));
        }
        Harness harness(source);
        const JobResult result = run_job(
            harness, export_request("{\"format\":\"csv\"}"), admin_caller());
        BOOST_TEST((result.state == JobState::succeeded));
        BOOST_TEST_MESSAGE("n=" << n);
        BOOST_TEST(json_int(result.result_json, "rows")
                   == static_cast<long>(n));

        // Every requested page used the fixed page size.
        for (const auto& call : source->calls) {
            BOOST_TEST(call.second == kExportPageSize);
        }
    }
}

// Type: unit (inline cap: oversize export fails with no partial data / T-109).
// Scenario: an inline export whose serialized bytes exceed kMaxInlineExportBytes
//   ends failed/invalid_field and retains no `data`.
// Boundaries: inline_buf.size()+row.size() > kMaxInlineExportBytes.
// On failure first check: did the body stop enforcing the cap, or publish a
//   partial inline payload?
BOOST_AUTO_TEST_CASE(inline_over_cap_fails_invalid_field_no_partial_data)
{
    auto source = std::make_shared<FakePageSource>();
    const std::string long_target = "http://example.com/" + std::string(1200, 'a');
    const std::size_t needed = (kMaxInlineExportBytes / long_target.size()) + 5;
    for (std::size_t i = 0; i < needed; ++i) {
        source->links.push_back(
            make_view("slug" + std::to_string(1000 + i), long_target));
    }
    Harness harness(source);

    const JobResult result =
        run_job(harness, export_request("{\"format\":\"csv\"}"), admin_caller());
    BOOST_TEST((result.state == JobState::failed));
    BOOST_TEST(result.fail_code == "invalid_field");
    // A failed outcome carries no result payload at all.
    BOOST_TEST(result.result_json.empty());
}

// Type: unit (path export writes the file and leaves no temp residue / T-99).
// Scenario: a `path` CSV export writes a create-new file with the expected
//   content, echoes the path in the result and leaves exactly that one file.
// Boundaries: AtomicFileSink create/commit; file-mode result shape.
// On failure first check: did the sink fail to commit, or leave a `.tmp-*`
//   sibling?
BOOST_AUTO_TEST_CASE(path_export_writes_file_no_temp_residue)
{
    auto source = std::make_shared<FakePageSource>();
    source->links = {make_view("aaa", "http://example.com/0"),
                     make_view("bbb", "http://example.com/1")};
    Harness harness(source, /*allow_paths=*/true);

    const std::filesystem::path dir = make_unique_dir();
    const std::filesystem::path target = dir / "out.csv";
    const JobResult result = run_job(
        harness,
        export_request("{\"format\":\"csv\",\"path\":" + jsonString(target.string())
                       + "}"),
        admin_caller());

    BOOST_TEST((result.state == JobState::succeeded));
    BOOST_TEST(json_int(result.result_json, "rows") == 2);
    BOOST_TEST(result.result_json.find("\"data\":null") != std::string::npos);
    BOOST_TEST(result.result_json.find(jsonString(target.string()))
               != std::string::npos);

    BOOST_REQUIRE(std::filesystem::exists(target));
    BOOST_TEST(count_dir_entries(dir) == 1u);  // target only; no temp
    BOOST_TEST(read_file(target)
               == "full_url,slug\nhttp://example.com/0,aaa\n"
                  "http://example.com/1,bbb\n");

    std::filesystem::remove_all(dir);
}

// Type: unit (create-new never clobbers an existing file / T-99).
// Scenario: exporting to a path that already exists is rejected invalid_field
//   by the factory and queues no job.
// Boundaries: AtomicFileSink::create_new on an existing target.
// On failure first check: did create_new start overwriting, or the factory stop
//   mapping the PathError?
BOOST_AUTO_TEST_CASE(existing_path_is_invalid_field_create_new)
{
    auto source = std::make_shared<FakePageSource>();
    source->links = {make_view("aaa", "http://example.com/0")};
    Harness harness(source, /*allow_paths=*/true);

    const std::filesystem::path dir = make_unique_dir();
    const std::filesystem::path target = dir / "exists.csv";
    std::ofstream(target, std::ios::binary) << "pre-existing";

    const CommandResponse response = harness.registry.execute(
        admin_caller(),
        export_request("{\"format\":\"csv\",\"path\":"
                       + jsonString(target.string()) + "}"));
    BOOST_TEST(response.ok == false);
    BOOST_TEST((response.error.code == CommandErrorCode::invalid_field));
    BOOST_TEST(harness.runner->list(std::nullopt, 0, 100).total == 0u);
    BOOST_TEST(read_file(target) == "pre-existing");  // untouched

    std::filesystem::remove_all(dir);
}

// Type: unit (path refused by capability, not transport name / T-95).
// Scenario: with allow_paths=false a `path` export is refused unsupported for
//   a "rest" transport and for a non-"rest" transport alike, queuing nothing.
// Boundaries: capability-only refusal in the factory.
// On failure first check: did the factory start branching on transport name
//   instead of deps.file_paths.allow_paths?
BOOST_AUTO_TEST_CASE(path_without_capability_is_unsupported_any_transport)
{
    auto source = std::make_shared<FakePageSource>();
    source->links = {make_view("aaa", "http://example.com/0")};
    Harness harness(source, /*allow_paths=*/false);

    for (const char* transport : {"rest", "stdio"}) {
        const CommandResponse response = harness.registry.execute(
            make_caller(ControlSet::Admin, transport),
            export_request("{\"format\":\"csv\",\"path\":\"/tmp/x.csv\"}"));
        BOOST_TEST(response.ok == false);
        BOOST_TEST((response.error.code == CommandErrorCode::unsupported));
    }
    BOOST_TEST(harness.runner->list(std::nullopt, 0, 100).total == 0u);
}

// Type: unit (cooperative cancel leaves no file / carry-forward 10.3, T-99).
// Scenario: a path export of 250 links gated at the second page is cancelled; it
//   ends cancelled with a null-payload result and leaves no file and no temp.
// Boundaries: cancel polled once per row; sink aborted on cancel.
// On failure first check: did the body stop polling cancel, or commit the sink
//   despite cancellation?
BOOST_AUTO_TEST_CASE(cancel_mid_export_leaves_no_file)
{
    auto source = std::make_shared<FakePageSource>();
    for (std::size_t i = 0; i < 250; ++i) {
        source->links.push_back(make_view(
            "slug" + std::to_string(1000 + i),
            "http://example.com/" + std::to_string(i)));
    }

    auto reached = std::make_shared<std::promise<void>>();
    auto release = std::make_shared<std::promise<void>>();
    std::shared_future<void> release_future = release->get_future().share();
    std::atomic<bool> reached_fired {false};
    source->on_page = [reached, release_future, &reached_fired](std::size_t off)
    {
        if (off == kExportPageSize) {  // the second page
            if (!reached_fired.exchange(true)) {
                reached->set_value();
            }
            release_future.wait();
        }
    };

    Harness harness(source, /*allow_paths=*/true);
    const std::filesystem::path dir = make_unique_dir();
    const std::filesystem::path target = dir / "cancel.csv";

    const CommandResponse response = harness.registry.execute(
        admin_caller(),
        export_request("{\"format\":\"csv\",\"path\":"
                       + jsonString(target.string()) + "}"));
    BOOST_REQUIRE(response.job.has_value());
    const std::string job_id = response.job->id;

    reached->get_future().wait();  // worker is fetching the second page
    harness.runner->cancel(job_id);
    release->set_value();

    const std::optional<JobRecord> record =
        harness.runner->wait_for_terminal(job_id, std::chrono::seconds(20));
    BOOST_REQUIRE(record.has_value());
    BOOST_TEST((record->state == JobState::cancelled));
    BOOST_REQUIRE(record->result_json.has_value());
    BOOST_TEST(record->result_json->find("\"path\":null") != std::string::npos);
    BOOST_TEST(record->result_json->find("\"data\":null") != std::string::npos);

    BOOST_TEST(!std::filesystem::exists(target));
    BOOST_TEST(count_dir_entries(dir) == 0u);  // no file, no .tmp-* residue

    std::filesystem::remove_all(dir);
}

// Type: unit (list failure maps to a failed job).
// Scenario: a page source that fails storage_failure ends the job failed with
//   the mapped code and no result payload.
// Boundaries: list error -> from_app_error mapping in the body.
// On failure first check: did the body swallow the list error, or mis-map it?
BOOST_AUTO_TEST_CASE(list_failure_fails_job_with_mapped_code)
{
    auto source = std::make_shared<FakePageSource>();
    source->fail = true;
    source->fail_code = app::AppErrorCode::storage_failure;
    Harness harness(source);

    const JobResult result =
        run_job(harness, export_request("{\"format\":\"csv\"}"), admin_caller());
    BOOST_TEST((result.state == JobState::failed));
    BOOST_TEST(result.fail_code == "storage_failure");
}

// Type: unit (read-only caller: inline allowed, path denied / D16, C7).
// Scenario: a ControlSet::User may run an inline export (read) but is denied a
//   `path` export with not_authorized before the factory runs, so no job is
//   created and no file is written.
// Boundaries: permission_for raises to write only when `path` is present.
// On failure first check: did the hook stop raising for `path`, or start
//   denying inline export?
BOOST_AUTO_TEST_CASE(read_only_caller_inline_ok_path_denied)
{
    auto source = std::make_shared<FakePageSource>();
    source->links = {make_view("aaa", "http://example.com/0")};
    Harness harness(source, /*allow_paths=*/true);

    // Inline export is a read; the User role is allowed and the job runs.
    const JobResult inline_result = run_job(
        harness, export_request("{\"format\":\"csv\"}"),
        make_caller(ControlSet::User, "test"));
    BOOST_TEST((inline_result.state == JobState::succeeded));

    // A `path` export raises to write; the User role is denied before any job.
    const std::filesystem::path dir = make_unique_dir();
    const std::filesystem::path target = dir / "denied.csv";
    const CommandResponse response = harness.registry.execute(
        make_caller(ControlSet::User, "test"),
        export_request("{\"format\":\"csv\",\"path\":"
                       + jsonString(target.string()) + "}"));
    BOOST_TEST(response.ok == false);
    BOOST_TEST((response.error.code == CommandErrorCode::not_authorized));
    BOOST_TEST(!response.job.has_value());
    BOOST_TEST(!std::filesystem::exists(target));
    // Only the earlier inline job exists; the denied path export created none.
    BOOST_TEST(harness.runner->list(std::nullopt, 0, 100).total == 1u);
    // The denial is audited as nothing: no `job_accepted` line was emitted for
    // the refused export (T-108), proven directly through the sink.
    BOOST_TEST(count_job_accepted(*harness.audit) == 0u);

    std::filesystem::remove_all(dir);
}

// Type: unit (status=all|deleted bulk enumeration needs write, T-71 / C7).
// Scenario: a ControlSet::User is denied link.export with status in {all,
//   deleted} with not_authorized before the factory runs (no job, no audit),
//   but is allowed status in {active, disabled, expired}; an Admin may export
//   status=deleted.
// Boundaries: link.export's permission_for mirrors link.list via the shared
//   link_status_permission helper, so the two can never drift.
// On failure first check: did export's permission_for stop raising for
//   status=all|deleted, or start denying the User-readable statuses?
BOOST_AUTO_TEST_CASE(status_all_or_deleted_export_requires_write)
{
    auto source = std::make_shared<FakePageSource>();
    source->links = {make_view("aaa", "http://example.com/0")};
    Harness harness(source);

    // A User is denied the bulk enumeration of all/deleted links before any job
    // is created, and the denial emits no job_accepted audit line (T-108).
    for (const char* status : {"all", "deleted"}) {
        const CommandResponse denied = harness.registry.execute(
            make_caller(ControlSet::User, "test"),
            export_request(std::string("{\"format\":\"csv\",\"status\":\"")
                           + status + "\"}"));
        BOOST_TEST(denied.ok == false);
        BOOST_TEST((denied.error.code == CommandErrorCode::not_authorized));
        BOOST_TEST(!denied.job.has_value());
    }
    // No job was accepted for either denied status, and nothing was audited.
    BOOST_TEST(harness.runner->list(std::nullopt, 0, 100).total == 0u);
    BOOST_TEST(count_job_accepted(*harness.audit) == 0u);

    // The User-readable statuses stay `read`, so the User may export them.
    for (const char* status : {"active", "disabled", "expired"}) {
        const JobResult result = run_job(
            harness,
            export_request(std::string("{\"format\":\"csv\",\"status\":\"")
                           + status + "\"}"),
            make_caller(ControlSet::User, "test"));
        BOOST_TEST((result.state == JobState::succeeded));
    }

    // An Admin is above the raised write bar, so status=deleted is allowed.
    const JobResult admin_deleted = run_job(
        harness, export_request("{\"format\":\"csv\",\"status\":\"deleted\"}"),
        admin_caller());
    BOOST_TEST((admin_deleted.state == JobState::succeeded));
}

// Type: unit (export unavailable when no page source is bound).
// Scenario: a registry built from deps with no list_page has no link.export
//   command.
// Boundaries: register_pair_commands gates link.export on list_page.
// On failure first check: did registration stop checking list_page?
BOOST_AUTO_TEST_CASE(unset_page_source_omits_command)
{
    Harness harness(/*source=*/nullptr);
    BOOST_TEST(harness.registry.find("link.export") == nullptr);
    BOOST_TEST(harness.registry.find("link.import") != nullptr);  // still there
}

// Type: unit (CSV formatter round-trips through the parser for tricky fields).
// Scenario: a field containing a comma, a double quote and a CRLF is quoted and
//   escaped by format_csv_row so PairLineParser recovers it verbatim.
// Boundaries: RFC 4180 quoting <-> the parser's quoted-field tokenizer.
// On failure first check: did the formatter stop doubling quotes, or the parser
//   stop accepting an embedded newline inside quotes?
BOOST_AUTO_TEST_CASE(csv_formatter_round_trips_through_parser)
{
    const std::string tricky = "http://x/a,\"b\"\r\nc";
    app::LinkView view = make_view("plainslug", tricky);
    const std::string row = format_csv_row(view);
    BOOST_REQUIRE(!row.empty() && row.back() == '\n');
    const std::string line = row.substr(0, row.size() - 1);

    PairLineParser parser(PairFormat::csv);
    const ParsedRow parsed = parser.parse(line, 1);
    BOOST_TEST((parsed.kind == RowKind::pair));
    BOOST_TEST(parsed.full_url == tricky);
    BOOST_REQUIRE(parsed.slug.has_value());
    BOOST_TEST(*parsed.slug == "plainslug");
}

// Type: unit (export -> import round trip is lossless for both formats).
// Scenario: a set of links exported to a file and re-imported into an empty
//   store yields the same slug and target sets, for CSV and NDJSON.
// Boundaries: formatter output accepted unchanged by link.import.
// On failure first check: did a formatter emit a shape the importer rejects?
BOOST_AUTO_TEST_CASE(round_trip_export_import_preserves_slug_and_target)
{
    std::vector<app::LinkView> links;
    for (int i = 0; i < 5; ++i) {
        links.push_back(make_view("round" + std::to_string(i),
                                  "http://example.com/" + std::to_string(i)));
    }
    links[1].tags = {"a", "b"};  // exercise richer NDJSON fields
    links[2].redirect_type = RedirectType::permanent;

    const std::set<std::string> want_slugs = [&]
    {
        std::set<std::string> s;
        for (const auto& v : links) {
            s.insert(v.slug);
        }
        return s;
    }();
    const std::set<std::string> want_targets = [&]
    {
        std::set<std::string> s;
        for (const auto& v : links) {
            s.insert(v.target_url);
        }
        return s;
    }();

    for (const char* format : {"csv", "ndjson"}) {
        auto source = std::make_shared<FakePageSource>();
        source->links = links;
        Harness exporter(source, /*allow_paths=*/true);

        const std::filesystem::path dir = make_unique_dir();
        const std::filesystem::path target = dir / "rt.dat";
        const JobResult exported = run_job(
            exporter,
            export_request(std::string("{\"format\":\"") + format
                           + "\",\"path\":" + jsonString(target.string()) + "}"),
            admin_caller());
        BOOST_REQUIRE((exported.state == JobState::succeeded));

        // Re-import the file into a fresh store via link.import.
        Harness importer(/*source=*/nullptr, /*allow_paths=*/true);
        CommandRequest import;
        import.id = "imp";
        import.command = "link.import";
        import.args_json = std::string("{\"format\":\"") + format
            + "\",\"path\":" + jsonString(target.string()) + "}";
        const CommandResponse import_resp =
            importer.registry.execute(admin_caller(), import);
        BOOST_REQUIRE(import_resp.job.has_value());
        importer.runner->wait_for_terminal(import_resp.job->id,
                                           std::chrono::seconds(20));

        std::set<std::string> got_slugs;
        std::set<std::string> got_targets;
        for (const url_shortener::Link& link : importer.store.all()) {
            got_slugs.insert(link.slug);
            got_targets.insert(link.target_url);
        }
        BOOST_TEST_MESSAGE(std::string("format=") + format);
        BOOST_TEST((got_slugs == want_slugs));
        BOOST_TEST((got_targets == want_targets));

        std::filesystem::remove_all(dir);
    }
}

// Type: unit (no exported cell can start with a formula metacharacter / T-98).
// Scenario: over the whole slug-generator alphabet and the isValidSlug class, no
//   admissible first byte of a slug is a CSV formula metacharacter; targets are
//   always http(s)://. So no CSV cell a valid link produces can begin with
//   `=`, `+`, `-`, `@`, a tab or a CR, without any neutralization.
// Boundaries: the slug grammar's leading-char class vs the OWASP metachar set.
// On failure first check: did the slug grammar widen to admit a metachar lead,
//   or did a formatter prepend something?
BOOST_AUTO_TEST_CASE(no_exported_cell_starts_with_formula_metachar)
{
    const std::string metachars = "=+-@\t\r";

    // The slug generator's alphabet never leads with a metachar.
    const std::string alphabet =
        "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";
    for (const char c : alphabet) {
        BOOST_TEST(metachars.find(c) == std::string::npos);
    }

    // isValidSlug rejects any slug that leads with a metachar, so no stored slug
    // can; and every valid slug's first CSV cell byte is therefore safe.
    for (const char c : metachars) {
        const std::string slug = std::string(1, c) + "abc";
        BOOST_TEST(!isValidSlug(slug));
    }

    // Targets always begin with the scheme, never a metachar.
    app::LinkView view = make_view("abcslug", "http://example.com/x");
    const std::string row = format_csv_row(view);
    BOOST_TEST(metachars.find(row.front()) == std::string::npos);
}
