#pragma once

#include <string>

#include <url_shortener/commands/job_types.hpp>

namespace url_shortener::commands
{

// Forward declaration: `register_job_commands` takes the registry by reference
// and only calls `add`. The full definition lives in
// `commands/command_registry.hpp`, included by the registering `.cpp`, so this
// header stays free of the registry's `std::map`/envelope includes (01.0/13
// include-hygiene test).
class CommandRegistry;

// Forward declaration: the three job handlers capture `&runner` and call its
// `get`/`list`/`cancel`. The full definition lives in `commands/job_runner.hpp`
// (which pulls in `<thread>`/`<condition_variable>` and the store), included by
// the registering `.cpp`, so this header keeps that weight out of every
// translation unit that only needs the registration entry point.
class JobRunner;

/**
 * @brief Registers the three job-inspection commands on @p registry (milestone
 * contract C1): `job.get`, `job.list` and `job.cancel`.
 *
 * The single registration entry point for the job-inspection surface. All three
 * are plain `sync` commands over the one process-wide @ref JobRunner (decision
 * log D13): `job.get` and `job.list` are `read` and non-`mutating` (both
 * `ControlSet` roles may call them); `job.cancel` is `write` and `mutating`, so
 * a `User` caller is denied `not_authorized` by `execute` before the handler
 * runs (contract C7, threat model T-92: the REST gate therefore reaches cancel
 * only as a loopback `admin`). Jobs carry no owner in V1, so any authorized
 * caller sees and cancels any job; ids are correlation handles only, never an
 * authorization capability (threat model T-93). Milestone 0002 must add
 * owner/principal filtering before multi-user sessions.
 *
 * This function only builds descriptors and binds their handlers to @p runner;
 * it never invokes a handler, submits a job, opens a listener or touches
 * `uri.txt`/`UriMapSingleton`, so it is safe to call from a registry builder.
 *
 * Lifetime: every handler captures `&runner`, so @p runner must outlive
 * @p registry. The process-wide runner is a `std::shared_ptr` held by the
 * composition root and outlives every (per-request) registry (subtask 20).
 * Because the registry stores the handlers and is non-copyable/non-movable, the
 * handlers never outlive the registry, but the registry must not outlive
 * @p runner.
 *
 * @param registry The registry the three descriptors are added to; must be free
 * of these three names (a duplicate name makes @ref CommandRegistry::add throw).
 * @param runner The process-wide job runner the handlers inspect; must outlive
 * @p registry.
 */
void register_job_commands(CommandRegistry& registry, JobRunner& runner);

/**
 * @brief How much of a @ref JobRecord @ref serialize_job_json emits.
 *
 * The two levels differ in exactly one field, `result`: `full` is the complete
 * catalog object a single-record fetch returns, `summary` is the deliberately
 * `result`-free projection the `job.list` page uses (2026-11-18 user decision,
 * threat model T-109). Every other field is identical across the two levels, so
 * a list item and a `job.get` of the same job agree on id/command/state/
 * progress/error/timestamps and differ only by the (potentially large) result
 * body.
 */
enum class JobDetail
{
    ///< Omit `result`. Used for each `job.list` item so a page of up to the
    ///< retained-job cap cannot inline one export result (up to ~1 MiB each)
    ///< per row and amplify into a multi-hundred-MiB list response (T-109).
    ///< Clients fetch a job's body separately with `job.get`.
    summary,

    ///< Emit the full catalog object including `result` when present (and
    ///< well-formed, see below). Used by `job.get` and `job.cancel`, which
    ///< return exactly one record.
    full
};

/**
 * @brief Serializes one @ref JobRecord to its public catalog job object
 * (catalog 3.4).
 *
 * Emits exactly the catalog shape in this fixed key order, so the output is
 * stable across calls and runs:
 * `{"id","command","state","progress":{"done","total"}[,"result":<raw>]`
 * `[,"error":{"code","message"}],"created_at"[,"started_at"][,"finished_at"]}`.
 *
 * Field presence is state-driven: `result` appears only at @ref JobDetail::full
 * and only when the record carries a `result_json`; `error` appears for a
 * `failed` job (its stored `{code, message}`) and for a `cancelled` job (fixed
 * `{"job_cancelled", "job cancelled"}`, which `job.get` surfaces with HTTP 200,
 * catalog error table); `started_at` appears once the job has left `queued`;
 * `finished_at` appears once the job is terminal. Timestamps use
 * `formatTimestamp` (RFC 3339 UTC).
 *
 * `result` projection (threat model T-109): at @ref JobDetail::summary the
 * `result` field is **always omitted** regardless of whether the record carries
 * one, so the `job.list` page stays bounded. At @ref JobDetail::full it is
 * embedded **verbatim** as a nested value.
 *
 * Trust invariant on `result_json` (contract C9): `result_json` must be
 * well-formed JSON produced by an in-process builder/serializer (e.g. a
 * `link.import`/`link.export` handler), **never** raw caller text. Because it
 * is embedded without re-escaping, a malformed value would corrupt the whole
 * job object. A job `result` is always a JSON object (catalog 3.4), so as a
 * cheap backstop the full path re-scans it with `scan_object` at
 * @ref k_result_scan_limits; if it is not a well-formed JSON object within
 * those limits, `result` is omitted rather than emitting broken JSON.
 *
 * Security (contract C6, threat model T-93/T-94): the record's `args_digest` is
 * never serialized and no argument value is emitted; every string is escaped
 * through `url_shortener::jsonString`. The id is a correlation handle only.
 *
 * @param record The job record to serialize; read, not retained.
 * @param detail Whether to emit the full object (`result` included when present
 * and well-formed) or the `result`-free summary for a list item; defaults to
 * @ref JobDetail::full so single-record callers need not pass it.
 * @return One well-formed JSON object as text.
 */
std::string serialize_job_json(const JobRecord& record,
                               JobDetail detail = JobDetail::full);

}  // namespace url_shortener::commands
