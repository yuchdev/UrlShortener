/**
 * @file 65_jobs_table_source.cpp
 * @brief Unit tests for @ref url_shortener::commands::make_jobs_table_source:
 * metadata, exact per-row key set, metadata-only projection (threat model T-94:
 * no args/args_digest/result/failure-message leak, error_code only), `q` and
 * sort behavior, paging/total, and the `db.rows`/`db.tables` catalog path for a
 * read-only caller.
 */
#define BOOST_TEST_MODULE CommandsJobsTableSourceTest
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <future>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include <boost/test/unit_test.hpp>

#include "url_shortener/app/app_error.hpp"
#include "url_shortener/app/table_source.hpp"
#include "url_shortener/commands/browse_commands.hpp"
#include "url_shortener/commands/command_descriptor.hpp"
#include "url_shortener/commands/command_registry.hpp"
#include "url_shortener/commands/envelope.hpp"
#include "url_shortener/commands/error_codes.hpp"
#include "url_shortener/commands/job_runner.hpp"
#include "url_shortener/commands/job_types.hpp"
#include "url_shortener/commands/jobs_table_source.hpp"
#include "url_shortener/core/clock.hpp"

using url_shortener::app::ITableSource;
using url_shortener::app::Result;
using url_shortener::app::TableCatalog;
using url_shortener::app::TableColumn;
using url_shortener::app::TableQuery;
using url_shortener::app::TableRowsPage;
using url_shortener::commands::CallerContext;
using url_shortener::commands::CommandErrorCode;
using url_shortener::commands::CommandRegistry;
using url_shortener::commands::CommandRequest;
using url_shortener::commands::CommandResponse;
using url_shortener::commands::IJobContext;
using url_shortener::commands::is_terminal;
using url_shortener::commands::JobBody;
using url_shortener::commands::JobOutcome;
using url_shortener::commands::JobRecord;
using url_shortener::commands::JobRunner;
using url_shortener::commands::JobRunnerOptions;
using url_shortener::commands::JobState;
using url_shortener::commands::JobSubmitResult;
using url_shortener::commands::make_db_rows_descriptor;
using url_shortener::commands::make_db_tables_descriptor;
using url_shortener::commands::make_error;
using url_shortener::commands::make_jobs_table_source;

namespace
{

// A clock frozen at the epoch; the runner only reads it for record timestamps.
ManualClock make_clock()
{
    return ManualClock(std::chrono::system_clock::time_point {});
}

// A one-shot gate: a body blocks in wait() until the test calls open(). Pure
// condition-variable synchronization, never a timed sleep.
struct Gate
{
    std::mutex mutex;
    std::condition_variable cv;
    bool opened = false;

    void wait()
    {
        std::unique_lock<std::mutex> lock(mutex);
        cv.wait(lock, [this] { return opened; });
    }

    void open()
    {
        {
            std::lock_guard<std::mutex> lock(mutex);
            opened = true;
        }
        cv.notify_all();
    }
};

// Blocks until the job is terminal (woken by the worker's finish), then asserts
// it actually reached a terminal state.
JobRecord wait_terminal(const JobRunner& runner, const std::string& id)
{
    std::optional<JobRecord> record =
        runner.wait_for_terminal(id, std::chrono::seconds(10));
    BOOST_REQUIRE(record.has_value());
    BOOST_REQUIRE(is_terminal(record->state));
    return *record;
}

// A body that succeeds, optionally carrying a result payload.
JobBody ok_body(std::optional<std::string> result = std::nullopt)
{
    return [result](IJobContext&)
    {
        JobOutcome out;
        out.ok = true;
        out.result_json = result;
        return out;
    };
}

// A body that fails with the given code and message.
JobBody fail_body(CommandErrorCode code, std::string message)
{
    return [code, message](IJobContext&)
    {
        JobOutcome out;
        out.ok = false;
        out.failure = make_error(code, message);
        return out;
    };
}

// Submits a body, requiring acceptance, and returns the new job id.
std::string submit_ok(JobRunner& runner,
                      const std::string& command,
                      JobBody body,
                      const std::string& digest = "0123456789abcdef")
{
    JobSubmitResult submitted = runner.submit(command, digest, std::move(body));
    BOOST_REQUIRE(submitted.job.has_value());
    return submitted.job->id;
}

// Extracts the ordered list of top-level object keys from a flat JSON object.
std::vector<std::string> top_level_keys(const std::string& json)
{
    std::vector<std::string> keys;
    int depth = 0;
    std::size_t i = 0;
    const std::size_t n = json.size();
    while (i < n) {
        const char c = json[i];
        if (c == '{' || c == '[') {
            ++depth;
            ++i;
            continue;
        }
        if (c == '}' || c == ']') {
            --depth;
            ++i;
            continue;
        }
        if (c == '"') {
            std::string token;
            ++i;
            while (i < n) {
                const char d = json[i];
                if (d == '\\' && i + 1 < n) {
                    token.push_back(json[i + 1]);
                    i += 2;
                    continue;
                }
                if (d == '"') {
                    ++i;
                    break;
                }
                token.push_back(d);
                ++i;
            }
            if (depth == 1 && i < n && json[i] == ':') {
                keys.push_back(token);
            }
            continue;
        }
        ++i;
    }
    return keys;
}

// Reads the string value of `"key":"..."` from a flat row object, or "" if the
// key is absent or its value is not a string.
std::string field_value(const std::string& row, const std::string& key)
{
    const std::string needle = "\"" + key + "\":\"";
    const std::size_t pos = row.find(needle);
    if (pos == std::string::npos) {
        return {};
    }
    const std::size_t start = pos + needle.size();
    const std::size_t end = row.find('"', start);
    if (end == std::string::npos) {
        return {};
    }
    return row.substr(start, end - start);
}

// The ids of each row, in row order (the sort/paging result).
std::vector<std::string> row_ids(const TableRowsPage& page)
{
    std::vector<std::string> ids;
    ids.reserve(page.rows_json.size());
    for (const std::string& row : page.rows_json) {
        ids.push_back(field_value(row, "id"));
    }
    return ids;
}

const std::vector<std::string> k_expected_columns = {"id",
                                                     "command",
                                                     "state",
                                                     "done",
                                                     "total",
                                                     "error_code",
                                                     "created_at",
                                                     "finished_at"};

// A minimal ITableSource used only to co-register "links"/"click_events" in the
// catalog path test; it returns a fixed count and an empty page.
class StubTableSource final : public ITableSource
{
public:
    explicit StubTableSource(std::string name) : m_name(std::move(name)) {}

    const std::string& name() const override { return m_name; }
    bool writable() const override { return false; }
    const std::vector<TableColumn>& columns() const override
    {
        return m_columns;
    }
    Result<std::size_t> row_count() const override
    {
        Result<std::size_t> result;
        result.value = 0;
        return result;
    }
    Result<TableRowsPage> rows(const TableQuery&) const override
    {
        Result<TableRowsPage> result;
        result.value = TableRowsPage {};
        return result;
    }

private:
    std::string m_name;
    std::vector<TableColumn> m_columns;
};

// A read-only User caller: db.* requires only Permission::read.
CallerContext user_caller()
{
    CallerContext caller;
    caller.principal = "test-user";
    caller.transport = "test";
    caller.control_set = ControlSet::User;
    return caller;
}

CommandRequest request(std::string command, std::string args_json)
{
    CommandRequest req;
    req.id = "client-1";
    req.command = std::move(command);
    req.args_json = std::move(args_json);
    return req;
}

}  // namespace

// Type: unit (metadata: name, writable, exact column list and order, T-94).
// Scenario: a fresh source exposes name "jobs", writable false, and the fixed
//   eight-column list with the documented sortable/searchable flags.
// Boundaries: only id/command/state/created_at/finished_at are sortable; only
//   id/command are searchable; there is no args/args_digest/result column.
// On failure first check: did the literal column list drift (contract C6/T-94)?
BOOST_AUTO_TEST_CASE(metadata_name_writable_and_columns)
{
    ManualClock clock = make_clock();
    JobRunner runner(clock, JobRunnerOptions {1, 4, 256});
    std::shared_ptr<ITableSource> source = make_jobs_table_source(runner);

    BOOST_TEST(source->name() == "jobs");
    BOOST_TEST(!source->writable());

    const std::vector<TableColumn>& columns = source->columns();
    BOOST_REQUIRE(columns.size() == k_expected_columns.size());
    for (std::size_t i = 0; i < columns.size(); ++i) {
        BOOST_TEST(columns[i].name == k_expected_columns[i]);
    }

    BOOST_TEST(columns[0].sortable);  // id
    BOOST_TEST(columns[0].searchable);  // id
    BOOST_TEST(columns[1].sortable);  // command
    BOOST_TEST(columns[1].searchable);  // command
    BOOST_TEST(columns[2].sortable);  // state
    BOOST_TEST(!columns[2].searchable);  // state
    BOOST_TEST(columns[3].type == "int");  // done
    BOOST_TEST(!columns[3].sortable);  // done
    BOOST_TEST(columns[4].type == "int");  // total
    BOOST_TEST(columns[5].name == "error_code");
    BOOST_TEST(!columns[5].sortable);  // error_code
    BOOST_TEST(columns[6].sortable);  // created_at
    BOOST_TEST(columns[6].type == "timestamp");  // created_at
    BOOST_TEST(columns[7].sortable);  // finished_at
    BOOST_TEST(columns[7].type == "timestamp");  // finished_at
}

// Type: unit (per-row key set equals the column list).
// Scenario: one succeeded job serializes to a row whose top-level keys, in
//   order, equal the fixed column names - no extra keys.
// Boundaries: the key set is exactly the eight columns.
// On failure first check: did serialize_job_row add or drop a field?
BOOST_AUTO_TEST_CASE(row_key_set_equals_columns)
{
    ManualClock clock = make_clock();
    JobRunner runner(clock, JobRunnerOptions {1, 4, 256});
    const std::string id = submit_ok(runner, "link.import", ok_body());
    wait_terminal(runner, id);

    std::shared_ptr<ITableSource> source = make_jobs_table_source(runner);
    const Result<TableRowsPage> page = source->rows(TableQuery {});
    BOOST_REQUIRE(page.ok());
    BOOST_REQUIRE(page.value->rows_json.size() == 1u);

    const std::vector<std::string> keys =
        top_level_keys(page.value->rows_json[0]);
    BOOST_REQUIRE(keys.size() == k_expected_columns.size());
    for (std::size_t i = 0; i < keys.size(); ++i) {
        BOOST_TEST(keys[i] == k_expected_columns[i]);
    }
}

// Type: unit (metadata only; no arg/result/message leak, threat model T-94).
// Scenario: a succeeded job carries an args digest marker and a result-payload
//   marker; a failed job carries a failure-message marker and the code
//   "invalid_field". No row contains any of the three markers; the failed row
//   exposes error_code "invalid_field" but not its message.
// Boundaries: the source reads neither args_digest, result_json, nor the failure
//   message - only the failure code reaches error_code.
// On failure first check: did serialize_job_row start reading a forbidden field?
BOOST_AUTO_TEST_CASE(metadata_only_never_leaks_args_result_or_message)
{
    ManualClock clock = make_clock();
    JobRunner runner(clock, JobRunnerOptions {1, 4, 256});

    const std::string ok_id = submit_ok(
        runner, "link.export",
        ok_body(std::string {"{\"secret\":\"RESULTMARKER\"}"}),
        "ARGSMARKERdigest0");
    const std::string fail_id = submit_ok(
        runner, "link.import",
        fail_body(CommandErrorCode::invalid_field, "MSGMARKER echoes --arg"));
    wait_terminal(runner, ok_id);
    wait_terminal(runner, fail_id);

    std::shared_ptr<ITableSource> source = make_jobs_table_source(runner);
    TableQuery query;
    query.limit = 500;
    const Result<TableRowsPage> page = source->rows(query);
    BOOST_REQUIRE(page.ok());
    BOOST_REQUIRE(page.value->rows_json.size() == 2u);

    std::string failed_row;
    for (const std::string& row : page.value->rows_json) {
        // No forbidden content survives into any row.
        BOOST_TEST(row.find("RESULTMARKER") == std::string::npos);
        BOOST_TEST(row.find("ARGSMARKER") == std::string::npos);
        BOOST_TEST(row.find("MSGMARKER") == std::string::npos);
        if (field_value(row, "id") == fail_id) {
            failed_row = row;
        }
    }

    BOOST_REQUIRE(!failed_row.empty());
    BOOST_TEST(field_value(failed_row, "error_code") == "invalid_field");
    BOOST_TEST(field_value(failed_row, "state") == "failed");
}

// Type: unit (q matches id and command, case-insensitive).
// Scenario: with an "link.import" (j-1) and a "link.export" (j-2) job, q="EXPORT"
//   returns only the export job, q="j-1" returns only the import job, and an
//   unmatched q returns none.
// Boundaries: q is a case-insensitive substring over id and command only.
// On failure first check: did matches_query start probing another field, or
//   stop folding case?
BOOST_AUTO_TEST_CASE(q_matches_id_and_command_case_insensitive)
{
    ManualClock clock = make_clock();
    JobRunner runner(clock, JobRunnerOptions {1, 4, 256});
    const std::string import_id =
        submit_ok(runner, "link.import", ok_body());  // j-1
    const std::string export_id =
        submit_ok(runner, "link.export", ok_body());  // j-2
    wait_terminal(runner, import_id);
    wait_terminal(runner, export_id);

    std::shared_ptr<ITableSource> source = make_jobs_table_source(runner);

    TableQuery by_command;
    by_command.q = "EXPORT";
    const Result<TableRowsPage> cmd_page = source->rows(by_command);
    BOOST_REQUIRE(cmd_page.ok());
    BOOST_TEST(cmd_page.value->total == 1u);
    BOOST_REQUIRE(cmd_page.value->rows_json.size() == 1u);
    BOOST_TEST(field_value(cmd_page.value->rows_json[0], "id") == export_id);

    TableQuery by_id;
    by_id.q = import_id;  // "j-1"
    const Result<TableRowsPage> id_page = source->rows(by_id);
    BOOST_REQUIRE(id_page.ok());
    BOOST_TEST(id_page.value->total == 1u);
    BOOST_REQUIRE(id_page.value->rows_json.size() == 1u);
    BOOST_TEST(field_value(id_page.value->rows_json[0], "id") == import_id);

    TableQuery no_match;
    no_match.q = "nomatch-xyz";
    const Result<TableRowsPage> empty = source->rows(no_match);
    BOOST_REQUIRE(empty.ok());
    BOOST_TEST(empty.value->total == 0u);
    BOOST_TEST(empty.value->rows_json.empty());
}

// Type: unit (sort by created_at/state/command ascending and descending).
// Scenario: three jobs created at distinct times (j-1 import succeeded, j-2
//   export failed, j-3 aaa.cmd succeeded). The default order is created_at
//   descending (j-3,j-2,j-1); created_at ascending reverses it; command
//   ascending orders aaa.cmd,export,import (j-3,j-2,j-1); state ascending puts
//   "failed" (j-2) before the two "succeeded".
// Boundaries: an absent sort keeps the newest-first default; descending flips.
// On failure first check: did less_by_column mis-map a column, or did the
//   default stop being created_at descending?
BOOST_AUTO_TEST_CASE(sort_by_created_state_and_command)
{
    ManualClock clock = make_clock();
    JobRunner runner(clock, JobRunnerOptions {1, 4, 256});

    const std::string j1 = submit_ok(runner, "link.import", ok_body());
    wait_terminal(runner, j1);
    clock.advance(std::chrono::seconds(1));
    const std::string j2 = submit_ok(
        runner, "link.export",
        fail_body(CommandErrorCode::invalid_field, "bad"));
    wait_terminal(runner, j2);
    clock.advance(std::chrono::seconds(1));
    const std::string j3 = submit_ok(runner, "aaa.cmd", ok_body());
    wait_terminal(runner, j3);

    std::shared_ptr<ITableSource> source = make_jobs_table_source(runner);

    // Default order: created_at descending (newest first).
    const Result<TableRowsPage> def = source->rows(TableQuery {});
    BOOST_REQUIRE(def.ok());
    BOOST_TEST(row_ids(*def.value) == (std::vector<std::string> {j3, j2, j1}));

    // created_at ascending.
    TableQuery created_asc;
    created_asc.sort = "created_at";
    created_asc.descending = false;
    const Result<TableRowsPage> asc = source->rows(created_asc);
    BOOST_REQUIRE(asc.ok());
    BOOST_TEST(row_ids(*asc.value) == (std::vector<std::string> {j1, j2, j3}));

    // command ascending: aaa.cmd < link.export < link.import.
    TableQuery command_asc;
    command_asc.sort = "command";
    command_asc.descending = false;
    const Result<TableRowsPage> by_cmd = source->rows(command_asc);
    BOOST_REQUIRE(by_cmd.ok());
    BOOST_TEST(row_ids(*by_cmd.value)
               == (std::vector<std::string> {j3, j2, j1}));

    // state ascending: "failed" < "succeeded" (j-2 first).
    TableQuery state_asc;
    state_asc.sort = "state";
    state_asc.descending = false;
    const Result<TableRowsPage> by_state = source->rows(state_asc);
    BOOST_REQUIRE(by_state.ok());
    BOOST_REQUIRE(by_state.value->rows_json.size() == 3u);
    BOOST_TEST(field_value(by_state.value->rows_json[0], "id") == j2);
    BOOST_TEST(field_value(by_state.value->rows_json[0], "state") == "failed");
}

// Type: unit (paging windows and total; empty runner).
// Scenario: an empty runner reports total 0 and no rows and row_count 0. With
//   five jobs, offset 1/limit 2 yields two rows out of total 5, and an offset
//   past the end yields no rows but total 5.
// Boundaries: offset at/beyond total is empty; total is independent of the
//   window.
// On failure first check: did the window math drift, or did total start
//   reflecting the page size?
BOOST_AUTO_TEST_CASE(paging_windows_and_total)
{
    ManualClock clock = make_clock();
    JobRunner runner(clock, JobRunnerOptions {1, 16, 256});
    std::shared_ptr<ITableSource> source = make_jobs_table_source(runner);

    // Empty runner.
    const Result<std::size_t> empty_count = source->row_count();
    BOOST_REQUIRE(empty_count.ok());
    BOOST_TEST(*empty_count.value == 0u);
    const Result<TableRowsPage> empty_page = source->rows(TableQuery {});
    BOOST_REQUIRE(empty_page.ok());
    BOOST_TEST(empty_page.value->total == 0u);
    BOOST_TEST(empty_page.value->rows_json.empty());

    for (int i = 0; i < 5; ++i) {
        const std::string id = submit_ok(runner, "link.import", ok_body());
        wait_terminal(runner, id);
    }

    const Result<std::size_t> count = source->row_count();
    BOOST_REQUIRE(count.ok());
    BOOST_TEST(*count.value == 5u);

    TableQuery window;
    window.offset = 1;
    window.limit = 2;
    const Result<TableRowsPage> page = source->rows(window);
    BOOST_REQUIRE(page.ok());
    BOOST_TEST(page.value->total == 5u);
    BOOST_TEST(page.value->rows_json.size() == 2u);

    TableQuery past_end;
    past_end.offset = 99;
    past_end.limit = 10;
    const Result<TableRowsPage> beyond = source->rows(past_end);
    BOOST_REQUIRE(beyond.ok());
    BOOST_TEST(beyond.value->total == 5u);
    BOOST_TEST(beyond.value->rows_json.empty());
}

// Type: unit (rows reflect queued/running/cancelled lifecycle states).
// Scenario: with a single worker occupied by a gated running job (j-1), a queued
//   job (j-2) is cancelled before it runs, and a third job (j-3) stays queued.
//   The snapshot shows one running, one cancelled and one queued row, and
//   row_count includes active jobs.
// Boundaries: the snapshot is not capped at max_retained_jobs; active jobs are
//   always present.
// On failure first check: did rows() start filtering out active jobs, or did the
//   snapshot cap at retention?
BOOST_AUTO_TEST_CASE(rows_reflect_active_and_cancelled_states)
{
    ManualClock clock = make_clock();
    JobRunner runner(clock, JobRunnerOptions {1, 8, 256});

    Gate gate;
    std::promise<void> running;
    JobBody gated = [&gate, &running](IJobContext&)
    {
        running.set_value();
        gate.wait();
        JobOutcome out;
        out.ok = true;
        return out;
    };
    const std::string g_id =
        submit_ok(runner, "link.export", std::move(gated));  // j-1
    running.get_future().wait();  // the single worker now holds j-1

    const std::string cancel_id =
        submit_ok(runner, "link.import", ok_body());  // j-2 (queued)
    const std::string queued_id =
        submit_ok(runner, "link.import", ok_body());  // j-3 (queued)

    std::optional<JobRecord> cancelled = runner.cancel(cancel_id);
    BOOST_REQUIRE(cancelled.has_value());
    BOOST_TEST((cancelled->state == JobState::cancelled));

    std::shared_ptr<ITableSource> source = make_jobs_table_source(runner);
    const Result<std::size_t> count = source->row_count();
    BOOST_REQUIRE(count.ok());
    BOOST_TEST(*count.value == 3u);

    TableQuery query;
    query.limit = 500;
    const Result<TableRowsPage> page = source->rows(query);
    BOOST_REQUIRE(page.ok());
    BOOST_REQUIRE(page.value->rows_json.size() == 3u);

    std::string g_state;
    std::string cancel_state;
    std::string queued_state;
    for (const std::string& row : page.value->rows_json) {
        const std::string id = field_value(row, "id");
        if (id == g_id) {
            g_state = field_value(row, "state");
        }
        else if (id == cancel_id) {
            cancel_state = field_value(row, "state");
        }
        else if (id == queued_id) {
            queued_state = field_value(row, "state");
        }
    }
    BOOST_TEST(g_state == "running");
    BOOST_TEST(cancel_state == "cancelled");
    BOOST_TEST(queued_state == "queued");

    gate.open();  // let the worker drain before the runner is destroyed.
    wait_terminal(runner, g_id);
}

// Type: integration (db.tables/db.rows over a three-table catalog, User caller).
// Scenario: a catalog holding links, click_events and the real jobs source lists
//   exactly those three names in that order via db.tables, and db.rows
//   {"table":"jobs"} succeeds for a read-only User caller and returns the job
//   row.
// Boundaries: db.* needs only read permission; the jobs source is reachable
//   through the generic handlers without any job-specific plumbing.
// On failure first check: did registration order change, or did the jobs source
//   stop satisfying the ITableSource contract the handlers expect?
BOOST_AUTO_TEST_CASE(db_rows_and_db_tables_over_catalog_for_user)
{
    ManualClock clock = make_clock();
    JobRunner runner(clock, JobRunnerOptions {1, 4, 256});
    const std::string id = submit_ok(runner, "link.import", ok_body());
    wait_terminal(runner, id);

    TableCatalog catalog;
    catalog.add(std::make_shared<StubTableSource>("links"));
    catalog.add(std::make_shared<StubTableSource>("click_events"));
    catalog.add(make_jobs_table_source(runner));

    CommandRegistry registry;
    registry.add(make_db_tables_descriptor(catalog));
    registry.add(make_db_rows_descriptor(catalog));

    const CommandResponse tables =
        registry.execute(user_caller(), request("db.tables", "{}"));
    BOOST_TEST(tables.ok);
    const std::size_t p_links = tables.result_json.find("\"name\":\"links\"");
    const std::size_t p_events =
        tables.result_json.find("\"name\":\"click_events\"");
    const std::size_t p_jobs = tables.result_json.find("\"name\":\"jobs\"");
    BOOST_REQUIRE(p_links != std::string::npos);
    BOOST_REQUIRE(p_events != std::string::npos);
    BOOST_REQUIRE(p_jobs != std::string::npos);
    BOOST_TEST(p_links < p_events);
    BOOST_TEST(p_events < p_jobs);

    const CommandResponse jobs = registry.execute(
        user_caller(), request("db.rows", "{\"table\":\"jobs\"}"));
    BOOST_TEST(jobs.ok);
    BOOST_TEST(jobs.result_json.find("\"table\":\"jobs\"")
               != std::string::npos);
    BOOST_TEST(jobs.result_json.find(id) != std::string::npos);
}
