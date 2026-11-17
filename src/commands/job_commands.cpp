#include <url_shortener/commands/job_commands.hpp>

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
 * negative or over-large value), and calls @ref JobRunner::list. The page is
 * serialized as `{"items":[job...],"total":n}` where each item is
 * @ref serialize_job_json at @ref JobDetail::summary -- the list item carries
 * id/command/state/progress/error/timestamps but **never** `result`
 * (2026-11-18 user decision, threat model T-109: a page of up to the retained-
 * job cap could otherwise inline one export result each, up to ~1 MiB, and
 * amplify into a multi-hundred-MiB response). Clients fetch a job's body with
 * `job.get`.
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

    const JobPage page = runner.list(filter, offset, limit);

    std::ostringstream os;
    os << "{\"items\":[";
    for (std::size_t i = 0; i < page.items.size(); ++i) {
        if (i != 0) {
            os << ',';
        }
        // Summary detail: list items omit `result` (T-109); see above.
        os << serialize_job_json(page.items[i], JobDetail::summary);
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

}  // namespace

std::string serialize_job_json(const JobRecord& record, JobDetail detail)
{
    std::ostringstream os;
    os << '{';
    os << "\"id\":" << jsonString(record.id);
    os << ",\"command\":" << jsonString(record.command);
    os << ",\"state\":"
       << jsonString(std::string(to_string(record.state)));
    os << ",\"progress\":{\"done\":" << record.progress.done
       << ",\"total\":" << record.progress.total << '}';

    // `result` is opaque pre-serialized object text (contract C9), embedded
    // verbatim. It is emitted only on the full-detail path and only when both
    // present and well-formed:
    //   - T-109: `job.list` items use JobDetail::summary and MUST omit it so a
    //     page of retained jobs cannot amplify ~1 MiB export results per row
    //     into a multi-hundred-MiB response (2026-11-18 user decision).
    //   - C9 backstop: a malformed value would be embedded unescaped and
    //     corrupt the whole object, so a failing well-formedness check omits
    //     `result` rather than emit broken JSON (never aborts, see helper).
    if (detail == JobDetail::full && record.result_json.has_value() &&
        job_result_json_is_wellformed(*record.result_json)) {
        os << ",\"result\":" << *record.result_json;
    }

    // `error` is present for `failed` (its stored detail) and for `cancelled`
    // (fixed `job_cancelled`, surfaced by job.get with HTTP 200, catalog error
    // table). Neither carries an argument value (contract C6).
    if (record.state == JobState::failed) {
        const std::string code =
            record.failure.has_value() ? record.failure->code : "internal";
        const std::string message =
            record.failure.has_value() ? record.failure->message : "job failed";
        os << ",\"error\":{\"code\":" << jsonString(code)
           << ",\"message\":" << jsonString(message) << '}';
    } else if (record.state == JobState::cancelled) {
        os << ",\"error\":{\"code\":\"job_cancelled\","
           << "\"message\":\"job cancelled\"}";
    }

    os << ",\"created_at\":"
       << jsonString(formatTimestamp(record.created_at));
    if (record.started_at.has_value()) {
        os << ",\"started_at\":"
           << jsonString(formatTimestamp(*record.started_at));
    }
    if (record.finished_at.has_value()) {
        os << ",\"finished_at\":"
           << jsonString(formatTimestamp(*record.finished_at));
    }

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
