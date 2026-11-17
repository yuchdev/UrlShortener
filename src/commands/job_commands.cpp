#include <url_shortener/commands/job_commands.hpp>

#include <chrono>
#include <cstddef>
#include <optional>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include <url_shortener/commands/arg_spec.hpp>
#include <url_shortener/commands/command_descriptor.hpp>
#include <url_shortener/commands/command_registry.hpp>
#include <url_shortener/commands/envelope.hpp>
#include <url_shortener/commands/error_codes.hpp>
#include <url_shortener/commands/job_runner.hpp>
#include <url_shortener/commands/job_types.hpp>
#include <url_shortener/commands/json_scan.hpp>
#include <url_shortener/core/utils.hpp>

namespace url_shortener::commands
{

namespace
{

/// Default `job.list` page size when `limit` is omitted; mirrors the
/// `link.list` default so the two paging surfaces behave alike (catalog 3.4).
constexpr long long k_job_list_default_limit = 50;

/// Inclusive upper bound on the `job.list` `limit`; identical to `link.list`
/// (threat model bounded-work convention). A larger value is rejected
/// `out_of_range` -> `invalid_field` by the registry before the handler runs.
constexpr long long k_job_list_max_limit = 500;

/// Inclusive upper bound on the `job.list` `offset`; identical to `link.list`
/// (`link_list_max_offset`). Bounds the deep-skip a single request can request;
/// a larger value is rejected `out_of_range` -> `invalid_field` by the registry.
constexpr long long k_job_list_max_offset = 1000000;

/**
 * @brief Tests whether @p result_json is a well-formed JSON object within the
 * result scan limits.
 *
 * This is the runtime backstop for @ref serialize_job_json's trust invariant
 * (contract C9): `result_json` is opaque pre-serialized text an in-process
 * handler is trusted to have built as valid JSON, but because it is embedded
 * verbatim a malformed value would corrupt the whole job object. A job `result`
 * is always a JSON object (catalog 3.4: `link.import`/`link.export` build
 * `{...}`), so the value is scanned **directly** with @ref scan_object -- which
 * fully validates the object's top-level members (e.g. it rejects `{"broken":}`
 * with a missing value) -- rather than wrapped in a `{"r":<value>}` probe,
 * which would only brace-match the value as opaque raw text and miss such
 * internal corruption. @ref k_result_scan_limits (1 MiB / depth 16 / 4096
 * members) is reused so a legitimate inline-export result at the ~1 MiB cap is
 * accepted, not rejected as `too_large` by the stricter request limits.
 *
 * Chosen over a bare `assert`: an abort-on-malformed assert could not coexist
 * with a defined fallback (the caller must still never emit broken JSON in a
 * release build, and the fallback itself must be testable), whereas this cheap
 * O(n) scan both catches the bug and degrades safely by omitting `result`. It
 * runs only on the full-detail path (`job.get`/`job.cancel`, one record), never
 * per item in a `job.list` page, so it adds no cost to the bounded-work list
 * surface.
 *
 * @param result_json The opaque result text to validate; treated as a value to
 * embed, never re-escaped.
 * @return `true` when @p result_json is one well-formed JSON object within
 * @ref k_result_scan_limits; `false` otherwise (malformed, not an object, or
 * over the limits), meaning the caller must omit `result`.
 */
bool job_result_json_is_wellformed(const std::string& result_json)
{
    const scan_result scan = scan_object(result_json, k_result_scan_limits);
    return scan.ok;
}

/**
 * @brief Builds the required `id` string @ref ArgSpec shared by `job.get` and
 * `job.cancel`.
 *
 * @return A required string argument named `id`; a missing value fails the
 * registry's validation with reason `missing` before the handler runs.
 */
ArgSpec make_id_arg()
{
    ArgSpec id;
    id.name = "id";
    id.type = ArgType::string;
    id.required = true;
    id.doc = "Job id of the form j-<n> to address.";
    return id;
}

/**
 * @brief Builds the argument schema for `job.list` (catalog 3.4 table).
 *
 * Declares every bounded-work constraint on the descriptor so an out-of-range
 * page request is rejected by the registry before the runner is read: `state`
 * is an optional enumeration restricted to the five lifecycle names (a bad
 * token is `not_in_enum`), `limit` an integer in `1..500` (default 50) and
 * `offset` an integer in `0..1000000` (default 0). The bounds mirror
 * `link.list` so the two paging surfaces share one convention.
 *
 * @return The ordered @ref ArgSpec list for the descriptor.
 */
std::vector<ArgSpec> make_job_list_args()
{
    ArgSpec state;
    state.name = "state";
    state.type = ArgType::enumeration;
    state.required = false;
    state.enum_values = {"queued", "running", "succeeded", "failed",
                         "cancelled"};
    state.doc = "Lifecycle filter; lists all states when omitted.";

    ArgSpec limit;
    limit.name = "limit";
    limit.type = ArgType::integer;
    limit.min = 1;
    limit.max = k_job_list_max_limit;
    limit.default_value = "50";
    limit.doc = "Maximum number of jobs to return (1..500).";

    ArgSpec offset;
    offset.name = "offset";
    offset.type = ArgType::integer;
    offset.min = 0;
    offset.max = k_job_list_max_offset;
    offset.default_value = "0";
    offset.doc = "Zero-based index of the first job to return (0..1000000).";

    return {std::move(state), std::move(limit), std::move(offset)};
}

/**
 * @brief Handler for `job.get`: looks up one record and serializes it.
 *
 * Reads the registry-validated `id` (guaranteed present as a string), calls
 * @ref JobRunner::get, and returns the record serialized by
 * @ref serialize_job_json. An unknown id yields `not_found` with the fixed,
 * value-free message `job not found` (there is no `job_not_found` code; the id
 * is never echoed, contract C6).
 *
 * @param runner The process-wide job runner to read.
 * @param args The validated argument view; `id` is guaranteed present.
 * @return On a hit a @ref CommandResult carrying the serialized job object; on
 * a miss a `not_found` @ref CommandError.
 */
CommandResult handle_job_get(JobRunner& runner, const ArgsView& args)
{
    CommandResult out;
    const std::string id = args.get_string("id").value_or("");
    const std::optional<JobRecord> record = runner.get(id);
    if (!record.has_value()) {
        out.error = make_error(CommandErrorCode::not_found, "job not found");
        return out;
    }
    out.result_json = serialize_job_json(*record);
    return out;
}

/**
 * @brief Handler for `job.list`: pages the job table, optionally filtered by
 * state.
 *
 * Parses the optional `state` through @ref parse_job_state (the enum @ref
 * ArgSpec has already rejected any unknown token; a defensive `nullopt` is
 * mapped to `invalid_field`), reads the validated `limit`/`offset` (their
 * bounds enforced by the registry so the casts to `std::size_t` cannot narrow a
 * negative or over-large value), and calls @ref JobRunner::list_summaries. The
 * page is serialized as `{"items":[job...],"total":n}` where each item is the
 * @ref JobSummary overload of @ref serialize_job_json -- the list item carries
 * id/command/state/progress/error/timestamps but **never** `result`
 * (2026-11-18 user decision, threat model T-109 / CWE-400: a page of up to the
 * retained-job cap could otherwise inline one export result each, up to ~1 MiB,
 * and amplify into a multi-hundred-MiB response). The metadata-only
 * `list_summaries` additionally ensures the store never even *copies* those
 * result bodies under its mutex. Clients fetch a job's body with `job.get`.
 *
 * @param runner The process-wide job runner to read.
 * @param args The validated argument view.
 * @return A @ref CommandResult carrying the serialized page; or `invalid_field`
 * on the defensive bad-state path (unreachable given the enum spec).
 */
CommandResult handle_job_list(JobRunner& runner, const ArgsView& args)
{
    CommandResult out;

    std::optional<JobState> filter;
    if (const std::optional<std::string> state = args.get_string("state")) {
        filter = parse_job_state(*state);
        if (!filter.has_value()) {
            out.error = make_error(CommandErrorCode::invalid_field,
                                   "state: not_in_enum");
            return out;
        }
    }

    const std::size_t offset = static_cast<std::size_t>(
        args.get_int("offset").value_or(0));
    const std::size_t limit = static_cast<std::size_t>(
        args.get_int("limit").value_or(k_job_list_default_limit));

    const JobSummaryPage page = runner.list_summaries(filter, offset, limit);

    std::ostringstream os;
    os << "{\"items\":[";
    for (std::size_t i = 0; i < page.items.size(); ++i) {
        if (i != 0) {
            os << ',';
        }
        // Metadata-only summaries: list items omit `result` and the store never
        // copied one under its lock (T-109 / CWE-400); see above.
        os << serialize_job_json(page.items[i]);
    }
    os << "],\"total\":" << page.total << '}';

    out.result_json = os.str();
    return out;
}

/**
 * @brief Handler for `job.cancel`: requests cancellation and serializes the
 * affected record.
 *
 * Reads the registry-validated `id`, calls @ref JobRunner::cancel (a no-op that
 * returns the final record for an already-terminal job, per that method's
 * semantics), and serializes the returned record. An unknown id yields
 * `not_found` with the fixed message `job not found`. On success the job id is
 * recorded as the audit target of this mutating command -- a correlation handle
 * only, never an argument value (contract C6, threat model T-93/T-108).
 *
 * @param runner The process-wide job runner to cancel through.
 * @param args The validated argument view; `id` is guaranteed present.
 * @return On a hit a @ref CommandResult carrying the serialized job object and
 * the job id as audit target; on an unknown id a `not_found` @ref CommandError.
 */
CommandResult handle_job_cancel(JobRunner& runner, const ArgsView& args)
{
    CommandResult out;
    const std::string id = args.get_string("id").value_or("");
    const std::optional<JobRecord> record = runner.cancel(id);
    if (!record.has_value()) {
        out.error = make_error(CommandErrorCode::not_found, "job not found");
        return out;
    }
    out.result_json = serialize_job_json(*record);
    out.audit_target = record->id;
    return out;
}

/**
 * @brief Builds the `job.get` descriptor (catalog 3.4): `read`, `sync`,
 * non-`mutating`.
 *
 * @param runner The runner the handler captures by reference; must outlive the
 * registry the descriptor is registered on.
 * @return A fully populated @ref CommandDescriptor ready for
 * @ref CommandRegistry::add.
 */
CommandDescriptor make_job_get_descriptor(JobRunner& runner)
{
    CommandDescriptor descriptor;
    descriptor.name = "job.get";
    descriptor.summary = "Fetch one job record by id.";
    descriptor.args = {make_id_arg()};
    descriptor.permission = Permission::read;
    descriptor.kind = ExecutionKind::sync;
    descriptor.mutating = false;
    descriptor.supports_dry_run = false;
    descriptor.maps_to = {"GET /api/v1/jobs/{id}", "job get"};
    descriptor.handler = [&runner](CommandContext& /*context*/,
                                   const ArgsView& args)
    { return handle_job_get(runner, args); };
    return descriptor;
}

/**
 * @brief Builds the `job.list` descriptor (catalog 3.4): `read`, `sync`,
 * non-`mutating`.
 *
 * @param runner The runner the handler captures by reference; must outlive the
 * registry the descriptor is registered on.
 * @return A fully populated @ref CommandDescriptor ready for
 * @ref CommandRegistry::add.
 */
CommandDescriptor make_job_list_descriptor(JobRunner& runner)
{
    CommandDescriptor descriptor;
    descriptor.name = "job.list";
    descriptor.summary = "List job records with state filter and paging.";
    descriptor.args = make_job_list_args();
    descriptor.permission = Permission::read;
    descriptor.kind = ExecutionKind::sync;
    descriptor.mutating = false;
    descriptor.supports_dry_run = false;
    descriptor.maps_to = {"GET /api/v1/jobs", "job list"};
    descriptor.handler = [&runner](CommandContext& /*context*/,
                                   const ArgsView& args)
    { return handle_job_list(runner, args); };
    return descriptor;
}

/**
 * @brief Builds the `job.cancel` descriptor (catalog 3.4): `write`, `sync`,
 * `mutating`.
 *
 * `supports_dry_run` stays `false`, so an `options.dry_run` request is rejected
 * `unsupported` before the handler. Being `write`, a `User` caller is denied
 * `not_authorized` by `execute` (contract C7, threat model T-92).
 *
 * @param runner The runner the handler captures by reference; must outlive the
 * registry the descriptor is registered on.
 * @return A fully populated @ref CommandDescriptor ready for
 * @ref CommandRegistry::add.
 */
CommandDescriptor make_job_cancel_descriptor(JobRunner& runner)
{
    CommandDescriptor descriptor;
    descriptor.name = "job.cancel";
    descriptor.summary = "Request cancellation of a job by id.";
    descriptor.args = {make_id_arg()};
    descriptor.permission = Permission::write;
    descriptor.kind = ExecutionKind::sync;
    descriptor.mutating = true;
    descriptor.supports_dry_run = false;
    descriptor.maps_to = {"POST /api/v1/jobs/{id}/cancel", "job cancel"};
    descriptor.handler = [&runner](CommandContext& /*context*/,
                                   const ArgsView& args)
    { return handle_job_cancel(runner, args); };
    return descriptor;
}

/// Optional wall-clock instant, as carried by both @ref JobRecord and
/// @ref JobSummary.
using opt_time_point =
    std::optional<std::chrono::system_clock::time_point>;

/**
 * @brief Writes the leading metadata fields `"id","command","state",
 * "progress":{...}` (no surrounding braces, no `result`) shared by the full
 * record and the summary projection.
 *
 * Factored out so the @ref JobRecord and @ref JobSummary serializers emit these
 * common fields identically and cannot drift (a list item and a `job.get` of the
 * same job must agree on every non-`result` field).
 *
 * @param os Stream to append to; receives the four fields with no leading
 * comma (the object's `'{'` is written by the caller immediately before).
 * @param id Job id; `jsonString`-escaped.
 * @param command Dotted command name; `jsonString`-escaped.
 * @param state Lifecycle state, rendered via @ref to_string.
 * @param progress Progress counters emitted as `{"done":n,"total":m}`.
 */
void serialize_job_head(std::ostringstream& os,
                        const std::string& id,
                        const std::string& command,
                        JobState state,
                        const JobProgress& progress)
{
    os << "\"id\":" << jsonString(id);
    os << ",\"command\":" << jsonString(command);
    os << ",\"state\":" << jsonString(std::string(to_string(state)));
    os << ",\"progress\":{\"done\":" << progress.done
       << ",\"total\":" << progress.total << '}';
}

/**
 * @brief Writes the trailing fields `[,"error":{...}],"created_at"
 * [,"started_at"][,"finished_at"]` (no surrounding braces, no `result`) shared
 * by the full record and the summary projection.
 *
 * `error` is present for `failed` (its stored `{code, message}`) and for
 * `cancelled` (the fixed `{job_cancelled, "job cancelled"}`, surfaced by
 * `job.get` with HTTP 200, catalog error table); neither carries an argument
 * value (contract C6). Timestamps use `formatTimestamp` (RFC 3339 UTC) and the
 * two optional ones appear only when present.
 *
 * @param os Stream to append to; every field is written with a leading comma
 * (the head has already written at least `progress`).
 * @param state Lifecycle state deciding whether/which `error` is emitted.
 * @param failure Stored failure detail; read only when @p state is `failed`
 * (falls back to `{internal, "job failed"}` if somehow absent).
 * @param created_at Creation instant; always emitted.
 * @param started_at Running-start instant; emitted only when present.
 * @param finished_at Terminal instant; emitted only when present.
 */
void serialize_job_tail(std::ostringstream& os,
                        JobState state,
                        const std::optional<JobFailure>& failure,
                        const std::chrono::system_clock::time_point& created_at,
                        const opt_time_point& started_at,
                        const opt_time_point& finished_at)
{
    if (state == JobState::failed) {
        const std::string code =
            failure.has_value() ? failure->code : "internal";
        const std::string message =
            failure.has_value() ? failure->message : "job failed";
        os << ",\"error\":{\"code\":" << jsonString(code)
           << ",\"message\":" << jsonString(message) << '}';
    } else if (state == JobState::cancelled) {
        os << ",\"error\":{\"code\":\"job_cancelled\","
           << "\"message\":\"job cancelled\"}";
    }

    os << ",\"created_at\":" << jsonString(formatTimestamp(created_at));
    if (started_at.has_value()) {
        os << ",\"started_at\":"
           << jsonString(formatTimestamp(*started_at));
    }
    if (finished_at.has_value()) {
        os << ",\"finished_at\":"
           << jsonString(formatTimestamp(*finished_at));
    }
}

}  // namespace

std::string serialize_job_json(const JobRecord& record, JobDetail detail)
{
    std::ostringstream os;
    os << '{';
    serialize_job_head(os, record.id, record.command, record.state,
                       record.progress);

    // `result` is opaque pre-serialized object text (contract C9), embedded
    // verbatim. It is emitted only on the full-detail path and only when both
    // present and well-formed:
    //   - T-109: `job.list` items use the JobSummary overload (and this record
    //     overload at JobDetail::summary) and MUST omit it so a page of retained
    //     jobs cannot amplify ~1 MiB export results per row into a multi-hundred-
    //     MiB response (2026-11-18 user decision).
    //   - C9 backstop: a malformed value would be embedded unescaped and
    //     corrupt the whole object, so a failing well-formedness check omits
    //     `result` rather than emit broken JSON (never aborts, see helper).
    if (detail == JobDetail::full && record.result_json.has_value() &&
        job_result_json_is_wellformed(*record.result_json)) {
        os << ",\"result\":" << *record.result_json;
    }

    serialize_job_tail(os, record.state, record.failure, record.created_at,
                       record.started_at, record.finished_at);
    os << '}';
    return os.str();
}

std::string serialize_job_json(const JobSummary& summary)
{
    // Byte-for-byte the record overload at JobDetail::summary, minus `result`:
    // a JobSummary structurally has no result_json, so the listing path never
    // copied one (threat model T-109 / CWE-400). The shared head/tail helpers
    // guarantee the non-result fields match the single-record serializer.
    std::ostringstream os;
    os << '{';
    serialize_job_head(os, summary.id, summary.command, summary.state,
                       summary.progress);
    serialize_job_tail(os, summary.state, summary.failure, summary.created_at,
                       summary.started_at, summary.finished_at);
    os << '}';
    return os.str();
}

void register_job_commands(CommandRegistry& registry, JobRunner& runner)
{
    registry.add(make_job_get_descriptor(runner));
    registry.add(make_job_list_descriptor(runner));
    registry.add(make_job_cancel_descriptor(runner));
}

}  // namespace url_shortener::commands
