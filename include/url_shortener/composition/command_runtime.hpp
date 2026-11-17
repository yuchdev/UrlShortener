#pragma once

#include <memory>
#include <optional>
#include <string>

// Forward declarations only: this header is the public seam every transport
// includes to obtain a registry, so it must stay free of storage/app/http
// definitions. The owning `.cpp` (`src/composition/command_runtime.cpp`) is the
// one place that knows the concrete types (C8 layering). `ServerConfig` lives
// in the global namespace (see `core/config.hpp`).
struct ServerConfig;

// The create-throttle rate-limiter port lives in the global namespace (see
// `storage/i_rate_limiter.hpp`); forward-declared so this seam header stays
// free of storage definitions.
class IRateLimiter;

namespace url_shortener
{
namespace app
{
class LinkCommandService;
class LegacyLinkStatsReader;
class ILinkLister;
class TableCatalog;
class LinkBackendHandle;
}  // namespace app

namespace commands
{
class IAuditSink;
class CommandRegistry;
class JobRunner;
}  // namespace commands

namespace composition
{

/**
 * @brief The local-filesystem capability granted to a runtime's pair commands
 * (`link.import` / `link.export`), a composition-layer mirror of
 * `commands::PathPolicy`.
 *
 * **Default-deny (allowlist, threat model T-95).** A `path` argument is refused
 * with `unsupported` unless the composing mode explicitly grants the capability
 * here, independent of any transport name. The REST per-request runtime (Task
 * 04.0 subtask 02) and any future transport that leaves this default refuse
 * `path`; only the local-operator compositions opt in — `cmd` (04.0/13) and
 * `tui` (06.0/19) set @ref allow_paths `true`, and `serve-stdio` (05.0/17) sets
 * it from `{!--no-file-paths, --path-root}`. @ref build_command_runtime passes
 * this struct through unchanged and never forces the capability on.
 */
struct PairFileOptions
{
    ///< `true` only when the composing mode grants the file-path capability.
    ///< When `false` (the default), `link.import`/`link.export` refuse any
    ///< `path` with `unsupported` before touching the filesystem (T-95).
    bool allow_paths = false;

    ///< When set, every `path` is confined to this canonical root (component-
    ///< wise, by the 05.0/07 rules); a target outside it is `invalid_field`.
    ///< Absent means the local operator's full filesystem (decision Q6).
    std::optional<std::string> root;
};

/**
 * @brief Optional overrides for @ref build_command_runtime.
 *
 * The one knob tests need is the audit sink; everything else the builder
 * resolves from the process-wide holders. There is deliberately no option to
 * disable auditing: a null @ref audit_sink selects the production default (a
 * @ref commands::LoggerAuditSink on the `"audit"` logging component), so a
 * runtime always has a non-null sink installed on its registry (milestone ADR
 * 0002: every mutating command emits one audit line).
 */
struct CommandRuntimeOptions
{
    ///< Audit sink to install on the runtime's registry. `nullptr` (the
    ///< default) selects the production default: a
    ///< @ref commands::LoggerAuditSink on `LoggerFactory::Component("audit")`
    ///< (01.0/09). Tests inject a recording sink here. There is no way to build
    ///< a runtime without a sink.
    std::shared_ptr<commands::IAuditSink> audit_sink;

    ///< The local-filesystem capability the runtime's pair commands honour.
    ///< Default-deny (@ref PairFileOptions::allow_paths `== false`, threat model
    ///< T-95): a mode that omits it — including the REST per-request runtime —
    ///< gets a runtime whose `link.import`/`link.export` refuse `path` with
    ///< `unsupported`. Only local-operator modes (`cmd`/`tui`/`serve-stdio`) set
    ///< it `true`. @ref build_command_runtime forwards it unchanged.
    PairFileOptions file_paths;
};

/**
 * @brief Everything one process mode needs to serve commands: the populated
 * @ref commands::CommandRegistry plus the objects its handlers reference,
 * owned together so the registry never outlives a dependency.
 *
 * The members are declared in dependency order (a referenced object before the
 * object that references it); because members destroy in reverse declaration
 * order, the registry (declared last) is torn down first, then the table
 * catalog, the lister, the link service, its stats reader, and finally the
 * audit sink. Every handler the registry stores captures a raw reference or
 * pointer into one of these members, so keeping the whole struct alive as a
 * unit is the lifetime contract: do not move a single member out and drop the
 * rest.
 *
 * The runtime does **not** own the link **store** or the click-event
 * repository: both are process-wide holders (`app::LinkBackend::store()` and
 * the global `analyticsClickRepository()`) that outlive every runtime, so a
 * runtime is cheap to build and discard. The @ref link_service's `ServerConfig`
 * reference is likewise borrowed from the caller and must outlive the runtime.
 */
struct CommandRuntime
{
    ///< The installed audit sink (never null; see @ref CommandRuntimeOptions),
    ///< shared with the registry via `set_audit_sink`. Declared first so it is
    ///< destroyed last.
    std::shared_ptr<commands::IAuditSink> audit_sink;

    ///< Analytics stats reader the @ref link_service borrows by reference;
    ///< owned here so it outlives the service that reads it.
    std::shared_ptr<app::LegacyLinkStatsReader> stats_reader;

    ///< The link command service every `link.*` handler delegates to; borrows
    ///< @ref stats_reader, the process-wide link store, and the caller's
    ///< `ServerConfig`.
    std::shared_ptr<app::LinkCommandService> link_service;

    ///< The browse port `link.list` and the `links` table source read; borrows
    ///< @ref link_service by reference.
    std::shared_ptr<app::ILinkLister> link_lister;

    ///< The `db.*` table allowlist; its `links` source borrows @ref link_lister
    ///< and its `click_events` source borrows the process-wide click
    ///< repository.
    std::shared_ptr<app::TableCatalog> tables;

    ///< The populated registry (link commands, `link.list`, `db.tables`,
    ///< `db.rows`, `system.info`, `command.list`). Declared last so it is
    ///< destroyed first, before any object its handlers reference.
    std::shared_ptr<commands::CommandRegistry> registry;

    ///< The selected create-throttle limiter (threat model T-75), copied from
    ///< `app::LinkBackendHandle::createRateLimiter()` at build time: the
    ///< process-wide, config-bound limiter the REST create adapter keys on the
    ///< client peer address. Not referenced by any registry handler (the
    ///< throttle is a transport gate applied before `execute`, never a command
    ///< result), so its position among the members is immaterial to teardown.
    ///< Null when the runtime was built from a handle with no limiter wired (a
    ///< default-constructed handle, e.g. the bridge's unit-test fallback), in
    ///< which case the create adapter skips the throttle (fail-open).
    std::shared_ptr<IRateLimiter> create_rate_limiter;

    ///< The one process-wide @ref commands::JobRunner (== @ref
    ///< composition::process_job_runner()), shared by every @ref CommandRuntime
    ///< of the process (milestone contract C11, decision log D13). Held as a
    ///< `std::shared_ptr` so a job submitted through this (possibly per-request)
    ///< runtime keeps running after the runtime and its registry are destroyed:
    ///< the runner outlives every runtime and its bodies capture only shared
    ///< ownership and values (threat model T-87). The `cmd` dispatcher waits on
    ///< it (`wait_for_terminal`); the process shuts it down via
    ///< @ref composition::shutdown_process_job_runtime. Never null after a
    ///< successful build.
    std::shared_ptr<commands::JobRunner> job_runner;
};

/**
 * @brief Builds the one fully-populated @ref commands::CommandRegistry every
 * transport executes against: the single registry builder of the product
 * (milestone contract C11).
 *
 * No transport or process mode builds its own registry; REST (Task 04.0
 * subtask 02) calls this per request while CLI/TUI/stdio call it once per
 * process. The call is cheap: it wires process-wide holders and opens no
 * connections (it never calls `InitLinkBackend`, binds no socket, and touches
 * no `uri.txt`/`UriMapSingleton`, milestone contract C5).
 *
 * Wiring order: build the link command service over the active link store
 * (`app::LinkBackend::store()`) -> choose the browse lister from
 * @p backend (`repository()` non-null -> `app::RepositoryLinkLister`, null ->
 * `app::LegacyLinkLister` over the global `linkRepository()`) -> build the
 * `app::TableCatalog` with the `links` and `click_events` sources -> register
 * the nine link commands, `link.list`/`db.tables`/`db.rows`/`system.info`, and
 * the `command.list` built-in -> register the job commands
 * (`job.get`/`job.list`/`job.cancel`) and pair commands (`link.import`, and
 * `link.export` bound to the active lister) over the process-wide
 * @ref commands::JobRunner, set that runner as the registry's job submitter, add
 * the `jobs` table source, and record the runner on
 * @ref CommandRuntime::job_runner -> install the audit sink. The file-path
 * capability comes from @p options `.file_paths` unchanged (default-deny, T-95):
 * the builder never forces it on, so a runtime built with default options
 * refuses `path`. The `system.info`
 * snapshot is read from the already-filtered process-wide
 * `app::process_system_info()` (set by the Task 02.0 bootstrap); this function
 * never reads live config and so discloses only allowlisted, non-secret values
 * (milestone contract C6).
 *
 * The @p backend is the only reason this takes a handle: its `repository()`
 * selects the lister. @p backend and @p server must both outlive the returned
 * runtime, because the lister and the link service borrow them by reference.
 *
 * @param server Server configuration the link service borrows by reference
 * (base domain, slug length, defaults); must outlive the returned runtime.
 * @param backend The process-wide backend handle (active when
 * `--storage-config` was given, inactive for the legacy in-memory default);
 * its `repository()` selects the lister. Must outlive the returned runtime.
 * @param options Optional overrides; a null `audit_sink` selects the production
 * `LoggerAuditSink` default. Defaulted so callers that need neither override
 * can omit it.
 * @return A @ref CommandRuntime owning the registry and every object its
 * handlers reference.
 * @throws std::runtime_error on an unrecoverable wiring failure (per the repo
 * rule that exceptions are reserved for programming errors and unrecoverable
 * startup failures); expected conditions never throw.
 */
CommandRuntime build_command_runtime(const ServerConfig& server,
                                     const app::LinkBackendHandle& backend,
                                     const CommandRuntimeOptions& options = {});

/**
 * @brief Publishes the one process-wide @ref CommandRuntime every transport
 * executes against, taking shared ownership of @p runtime for as long as it
 * stays installed.
 *
 * The runtime is built exactly once at startup (`main`, after
 * `app::InitLinkBackend` installs the active store and before any
 * request-serving thread or CLI dispatch exists) from the active
 * @ref app::LinkBackendHandle, so every transport shares one registry over the
 * one installed store — no transport rebuilds a runtime per request, and no
 * transport reads an empty legacy store while CRUD goes to a configured
 * backend (the split-brain this holder exists to prevent).
 *
 * ## Lifetime and thread-safety contract (read before calling)
 * - @ref process_command_runtime is **lock-free** on the read path: it returns
 *   an owning `std::shared_ptr` copy via an atomic load, so a concurrent
 *   @ref reset_process_command_runtime can never tear the runtime out from
 *   under an in-flight reader (the returned copy keeps it alive). It is safe to
 *   call from every request-handling thread.
 * - @ref install_process_command_runtime is **startup only** and asserts
 *   `!app::LinkBackend::serving()` in debug builds: install once, before the
 *   first request-serving thread (the same serving latch
 *   @ref app::LinkBackend uses). Installing twice replaces the previous runtime.
 * - @ref reset_process_command_runtime is **teardown only** and must run before
 *   the @ref app::LinkBackendHandle the runtime borrows from is destroyed: the
 *   runtime's link service references `app::LinkBackend::store()` and its lister
 *   holds the active metadata repository, so dropping the runtime first keeps
 *   that borrow well-ordered. It is memory-safe to call while readers run (they
 *   hold their own `shared_ptr`), but callers must still observe the
 *   store-teardown ordering above.
 *
 * @param runtime The runtime to install; shared ownership is retained by the
 * holder until the next install/reset. A null @p runtime is equivalent to
 * @ref reset_process_command_runtime.
 */
void install_process_command_runtime(std::shared_ptr<CommandRuntime> runtime);

/**
 * @brief Returns the installed process-wide @ref CommandRuntime, or a null
 * `std::shared_ptr` when none has been installed.
 *
 * Lock-free owning read (one atomic load of a `std::shared_ptr`): the returned
 * copy keeps the runtime alive for the caller's use even if
 * @ref reset_process_command_runtime runs concurrently. Safe to call from every
 * request-handling thread. A null return is the signal to a transport that no
 * process runtime was published (unit tests, tools), in which case the caller
 * falls back to its own lazily-built default.
 * @return A `shared_ptr` to the installed runtime, or `nullptr` when none is
 * installed.
 */
std::shared_ptr<CommandRuntime> process_command_runtime();

/**
 * @brief Drops the holder's ownership of the installed runtime, restoring the
 * "none installed" state.
 *
 * Teardown and tests only. Must run before the @ref app::LinkBackendHandle the
 * runtime borrows from is torn down (see the ordering note on
 * @ref install_process_command_runtime): `main` resets the runtime, then calls
 * `app::LinkBackend::shutdown()`, then lets the handle destruct. A no-op when
 * nothing is installed. Atomic store, so it never races a concurrent
 * @ref process_command_runtime read.
 */
void reset_process_command_runtime();

}  // namespace composition
}  // namespace url_shortener
