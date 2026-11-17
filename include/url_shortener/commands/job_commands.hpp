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
 * @brief Serializes one @ref JobRecord to its public catalog job object
 * (catalog 3.4).
 *
 * Emits exactly the catalog shape in this fixed key order, so the output is
 * stable across calls and runs:
 * `{"id","command","state","progress":{"done","total"}[,"result":<raw>]`
 * `[,"error":{"code","message"}],"created_at"[,"started_at"][,"finished_at"]}`.
 *
 * Field presence is state-driven: `result` appears only when the record carries
 * a `result_json` (embedded **verbatim** as a nested object -- it is opaque
 * pre-serialized text the producing handler is trusted to have made valid JSON,
 * contract C9); `error` appears for a `failed` job (its stored
 * `{code, message}`) and for a `cancelled` job (fixed `{"job_cancelled",
 * "job cancelled"}`, which `job.get` surfaces with HTTP 200, catalog error
 * table); `started_at` appears once the job has left `queued`; `finished_at`
 * appears once the job is terminal. Timestamps use `formatTimestamp`
 * (RFC 3339 UTC).
 *
 * Security (contract C6, threat model T-93/T-94): the record's `args_digest` is
 * never serialized and no argument value is emitted; every string is escaped
 * through `url_shortener::jsonString`. The id is a correlation handle only.
 *
 * @param record The job record to serialize; read, not retained.
 * @return One well-formed JSON object as text.
 */
std::string serialize_job_json(const JobRecord& record);

}  // namespace url_shortener::commands
