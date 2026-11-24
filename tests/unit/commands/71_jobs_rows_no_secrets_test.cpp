/**
 * @file 71_jobs_rows_no_secrets_test.cpp
 * @brief Gap case for the metadata-only `jobs` table seen through the generic
 * `db.rows` command (milestone contract C6, threat model T-94 / T-13): a job
 * whose args digest, result payload and failure message all carry distinctive
 * markers surfaces through `db.rows {"table":"jobs"}` with none of those markers
 * in the serialized response, only its metadata (id, command, state,
 * error_code).
 *
 * `commands__65_jobs_table_source` already proves the `ITableSource` projection
 * is metadata-only by calling `rows()` directly; this case proves the same
 * invariant holds end-to-end through the `db.rows` handler and the full
 * `CommandResponse` a caller actually receives, using the real `JobRunner` and
 * `make_jobs_table_source` over a `TableCatalog` exactly as the shipped jobs
 * tests do.
 */
#define BOOST_TEST_MODULE CommandsJobsRowsNoSecretsTest
#include <chrono>
#include <memory>
#include <optional>
#include <string>

#include <boost/test/unit_test.hpp>

#include "url_shortener/app/table_source.hpp"
#include "url_shortener/commands/browse_commands.hpp"
#include "url_shortener/commands/command_registry.hpp"
#include "url_shortener/commands/envelope.hpp"
#include "url_shortener/commands/error_codes.hpp"
#include "url_shortener/commands/job_runner.hpp"
#include "url_shortener/commands/job_types.hpp"
#include "url_shortener/commands/jobs_table_source.hpp"
#include "url_shortener/core/clock.hpp"

using url_shortener::app::ITableSource;
using url_shortener::app::TableCatalog;
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
using url_shortener::commands::JobSubmitResult;
using url_shortener::commands::make_db_rows_descriptor;
using url_shortener::commands::make_error;
using url_shortener::commands::make_jobs_table_source;

namespace
{

// A clock frozen at the epoch; the runner only reads it for record timestamps.
ManualClock make_clock()
{
    return ManualClock(std::chrono::system_clock::time_point {});
}

// A body that succeeds carrying the given result payload.
JobBody ok_body(std::string result)
{
    return [result = std::move(result)](IJobContext&)
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
    return [code, message = std::move(message)](IJobContext&)
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
                      const std::string& digest,
                      JobBody body)
{
    JobSubmitResult submitted = runner.submit(command, digest, std::move(body));
    BOOST_REQUIRE(submitted.job.has_value());
    return submitted.job->id;
}

// Blocks until the job is terminal, asserting it actually got there.
void wait_terminal(const JobRunner& runner, const std::string& id)
{
    std::optional<JobRecord> record =
        runner.wait_for_terminal(id, std::chrono::seconds(10));
    BOOST_REQUIRE(record.has_value());
    BOOST_REQUIRE(is_terminal(record->state));
}

// A read-only User caller: db.* requires only Permission::read.
CallerContext user_caller()
{
    CallerContext caller;
    caller.principal = "test-user";
    caller.transport = "test";
    caller.control_set = ControlSet::User;
    return caller;
}

CommandRequest db_rows_jobs()
{
    CommandRequest req;
    req.id = "client-1";
    req.command = "db.rows";
    // limit 500 so both jobs land in one page.
    req.args_json = R"({"table":"jobs","limit":500})";
    return req;
}

}  // namespace

// Type: unit (db.rows jobs never surfaces args/result/message, T-94/T-13).
// Scenario: two jobs run to terminal state through the real JobRunner. The
//   first succeeds carrying both an args-digest marker and a result-payload
//   marker; the second fails with a failure-message marker and the code
//   invalid_field. db.rows {"table":"jobs"} is then served to a read-only User
//   over a catalog holding the real jobs source. The serialized response names
//   both jobs (by id) and the failed job's error_code "invalid_field", but
//   contains none of the three markers.
// Boundaries: the jobs source projects only metadata columns, so neither the
//   args digest, the result JSON, nor the failure message can reach the wire,
//   whichever job they belong to.
// On failure first check: did serialize_job_row start reading args_digest/
//   result_json/failure message, or did db.rows stop going through the source?
BOOST_AUTO_TEST_CASE(jobs_rows_never_expose_args_or_results)
{
    ManualClock clock = make_clock();
    JobRunner runner(clock, JobRunnerOptions {1, 8, 256});

    const std::string ok_id =
        submit_ok(runner, "link.export", "ARGSDIGESTMARKER0",
                  ok_body(R"({"payload":"RESULTPAYLOADMARKER"})"));
    const std::string fail_id = submit_ok(
        runner, "link.import", "0123456789abcdef",
        fail_body(CommandErrorCode::invalid_field, "FAILUREMSGMARKER --arg"));
    wait_terminal(runner, ok_id);
    wait_terminal(runner, fail_id);

    TableCatalog catalog;
    catalog.add(make_jobs_table_source(runner));

    CommandRegistry registry;
    registry.add(make_db_rows_descriptor(catalog));

    const CommandResponse response =
        registry.execute(user_caller(), db_rows_jobs());
    BOOST_REQUIRE(response.ok);

    const std::string& body = response.result_json;

    // Both jobs are present as metadata, and the failure code surfaces...
    BOOST_TEST(body.find(ok_id) != std::string::npos);
    BOOST_TEST(body.find(fail_id) != std::string::npos);
    BOOST_TEST(body.find("invalid_field") != std::string::npos);

    // ...but no args digest, result payload or failure message leaks.
    BOOST_TEST(body.find("ARGSDIGESTMARKER") == std::string::npos);
    BOOST_TEST(body.find("RESULTPAYLOADMARKER") == std::string::npos);
    BOOST_TEST(body.find("FAILUREMSGMARKER") == std::string::npos);
}
