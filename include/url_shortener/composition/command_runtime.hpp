#pragma once

#include <memory>

// Forward declarations only: this header is the public seam every transport
// includes to obtain a registry, so it must stay free of storage/app/http
// definitions. The owning `.cpp` (`src/composition/command_runtime.cpp`) is the
// one place that knows the concrete types (C8 layering). `ServerConfig` lives
// in the global namespace (see `core/config.hpp`).
struct ServerConfig;

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
}  // namespace commands

namespace composition
{

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

    // Task 05.0 subtask 20 adds: std::shared_ptr<commands::JobRunner>
    // job_runner (process-wide, shared) and the job/pair commands + jobs table.
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
 * the `command.list` built-in -> install the audit sink. The `system.info`
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

}  // namespace composition
}  // namespace url_shortener
