#include <cstddef>
#include <functional>
#include <memory>
#include <string>
#include <utility>

#include "url_shortener/composition/command_runtime.hpp"

#include "url_shortener/app/app_error.hpp"
#include "url_shortener/app/click_events_table_source.hpp"
#include "url_shortener/app/legacy_adapters.hpp"
#include "url_shortener/app/legacy_link_lister.hpp"
#include "url_shortener/app/link_command_service.hpp"
#include "url_shortener/app/link_lister.hpp"
#include "url_shortener/app/links_table_source.hpp"
#include "url_shortener/app/repository_link_lister.hpp"
#include "url_shortener/app/system_info.hpp"
#include "url_shortener/app/table_source.hpp"
#include "url_shortener/commands/audit.hpp"
#include "url_shortener/commands/browse_commands.hpp"
#include "url_shortener/commands/builtin_commands.hpp"
#include "url_shortener/commands/command_registry.hpp"
#include "url_shortener/commands/job_commands.hpp"
#include "url_shortener/commands/job_runner.hpp"
#include "url_shortener/commands/jobs_table_source.hpp"
#include "url_shortener/commands/link_commands.hpp"
#include "url_shortener/commands/pair_commands.hpp"
#include "url_shortener/commands/pair_file_access.hpp"
#include "url_shortener/composition/job_runtime.hpp"
#include "url_shortener/composition/link_backend_bootstrap.hpp"
#include "url_shortener/composition/link_command_service_factory.hpp"
#include "url_shortener/core/config.hpp"
#include "url_shortener/observability/logger_factory.hpp"
#include "url_shortener/storage/i_metadata_repository.hpp"
#include "url_shortener/storage/link_repository.hpp"
#include "url_shortener/storage/memory/in_memory_click_event_repository.hpp"

namespace url_shortener
{

// The process-global in-memory click-event repository the server writes clicks
// to and the `click_events` table source reads. Declared here (as every other
// consumer does) rather than via a header, because it is an implementation
// detail of the legacy wiring with no public declaration; a persistent
// click store is a separate product decision (threat model T-59). The runtime
// borrows it by reference and never owns it.
storage::memory::InMemoryClickEventRepository& analyticsClickRepository();

namespace composition
{
namespace
{

/**
 * @brief Maps a validated `link.export` status token to its `app::list_status`.
 *
 * The export `status` argument is enum-validated by the descriptor before the
 * body runs, so an out-of-set value never reaches here; an unrecognized token
 * (and the default/empty one) maps to `active`, matching `link.list`.
 *
 * @param value The lifecycle token (`all`/`active`/`disabled`/`expired`/
 * `deleted`).
 * @return The corresponding `app::list_status`.
 */
app::list_status export_status_of(const std::string& value)
{
    if (value == "all") {
        return app::list_status::all;
    }
    if (value == "disabled") {
        return app::list_status::disabled;
    }
    if (value == "expired") {
        return app::list_status::expired;
    }
    if (value == "deleted") {
        return app::list_status::deleted;
    }
    return app::list_status::active;
}

/**
 * @brief Binds a @ref commands::LinkPageSource over the shared browse lister so
 * `link.export` pages through the one query path `link.list` uses (contract C1).
 *
 * The returned callable captures **both** @p lister and @p service by value
 * (`std::shared_ptr` copies), so a `link.export` job body that copies it keeps
 * the whole query graph alive after the submitting @ref CommandRuntime and its
 * registry are destroyed (threat model T-87). Capturing @p service is
 * load-bearing, not redundant: the concrete listers
 * (`LegacyLinkLister`/`RepositoryLinkLister`) hold the `LinkCommandService` by a
 * **raw** `const&` to project each `LinkView`, so without an owning copy here a
 * job that outlives the runtime would dereference a destroyed service (a
 * use-after-free the lifetime test guards). The callable never references the
 * runtime, the request or a `ServerConfig&`. It translates the string
 * @ref commands::LinkListFilter into an `app::LinkListQuery` (sort/order fixed to
 * `created_at desc` by the export command for stable offset paging) and projects
 * the resulting `app::LinkListPage` onto a @ref commands::LinkPage.
 *
 * @param lister The process-wide browse port, shared by ownership; must be
 * non-null.
 * @param service The link command service the @p lister projects views through,
 * shared by ownership so it outlives any body that copied the returned source.
 * @return A @ref commands::LinkPageSource ready to place in @ref
 * commands::PairCommandDeps::list_page.
 */
commands::LinkPageSource list_page_from(
    std::shared_ptr<app::ILinkLister> lister,
    std::shared_ptr<app::LinkCommandService> service)
{
    return [lister, service](const commands::LinkListFilter& filter,
                             std::size_t offset,
                             std::size_t limit) -> app::Result<commands::LinkPage>
    {
        app::LinkListQuery query;
        query.status = export_status_of(filter.status);
        if (!filter.q.empty()) {
            query.q = filter.q;
        }
        if (!filter.tag.empty()) {
            query.tag = filter.tag;
        }
        if (!filter.campaign.empty()) {
            query.campaign = filter.campaign;
        }
        query.sort = app::list_sort::created_at;  // export fixes created_at desc
        query.descending = true;
        query.offset = offset;
        query.limit = limit;

        app::Result<commands::LinkPage> out;
        app::Result<app::LinkListPage> listed = lister->list(query);
        if (!listed.ok()) {
            out.error = listed.error;
            return out;
        }
        commands::LinkPage page;
        if (listed.value.has_value()) {
            page.items = std::move(listed.value->items);
            page.total = listed.value->total;
        }
        out.value = std::move(page);
        return out;
    };
}

}  // namespace

CommandRuntime build_command_runtime(const ServerConfig& server,
                                     const app::LinkBackendHandle& backend,
                                     const CommandRuntimeOptions& options)
{
    CommandRuntime runtime;

    // 1. Audit sink: the injected recording sink (tests) or the production
    //    default LoggerAuditSink on the "audit" logging component. Never null,
    //    so the registry always audits mutating commands (ADR 0002). The
    //    logging destination is each mode's responsibility: the mode
    //    initializes logging before calling this function (decision log D14);
    //    this function does not initialize logging itself.
    runtime.audit_sink = options.audit_sink
        ? options.audit_sink
        : std::make_shared<commands::LoggerAuditSink>(
              observability::LoggerFactory::Component("audit"));

    // 2. Link command service over the active link store. The factory binds the
    //    service to LinkBackend::store() (the installed RepositoryLinkStore, or
    //    the LegacyLinkStore default) via a non-owning aliasing pointer, so the
    //    process-wide store outlives the runtime. The service borrows `server`
    //    by reference, which the caller guarantees outlives the runtime.
    app::LinkCommandServiceBundle bundle =
        app::BuildLegacyLinkCommandService(server);
    runtime.stats_reader = bundle.stats_reader;
    runtime.link_service = bundle.service;

    // 3. Browse lister: a configured backend exposes its record-based
    //    repository, which selects the RepositoryLinkLister that reads the live
    //    backend directly; the legacy in-memory default has no such repository
    //    (null), so the LegacyLinkLister snapshots the global linkRepository().
    //    Both project items through the same LinkView path as link.get.
    const std::shared_ptr<::IMetadataRepository> repo = backend.repository();
    if (repo) {
        runtime.link_lister = std::make_shared<app::RepositoryLinkLister>(
            repo, *runtime.link_service);
    }
    else {
        runtime.link_lister = std::make_shared<app::LegacyLinkLister>(
            linkRepository(), *runtime.link_service);
    }

    // 4. The db.* allowlist: exactly the two logical tables this product
    //    exposes. A reviewer answers "which tables can db.rows read?" from
    //    these two add() calls alone (success criterion 4). TableCatalog::add
    //    is allowlist-by-construction (it refuses auth-table and malformed
    //    names); neither source here is one of those.
    runtime.tables = std::make_shared<app::TableCatalog>();
    runtime.tables->add(
        std::make_shared<app::LinksTableSource>(*runtime.link_lister));
    runtime.tables->add(std::make_shared<app::ClickEventsTableSource>(
        analyticsClickRepository()));

    // 5. The system.info snapshot comes from the already-filtered process-wide
    //    snapshot the Task 02.0 bootstrap published (default: the legacy
    //    in-memory snapshot). It carries only allowlisted backend names and the
    //    non-secret ServerConfig subset, never a DSN/path/salt/TLS (C6, T-53).
    const app::SystemInfo info = app::process_system_info();

    // 6. The one registry: the nine link commands, the three browse commands
    //    plus system.info, and the command.list built-in. No transport or mode
    //    builds its own registry (C11).
    runtime.registry = std::make_shared<commands::CommandRegistry>();
    commands::register_link_commands(*runtime.registry, *runtime.link_service);
    commands::register_browse_commands(
        *runtime.registry, *runtime.link_lister, *runtime.tables, info);
    commands::register_builtin_commands(*runtime.registry);

    // 6b. Jobs: the one process-wide JobRunner (milestone C11, decision log
    //     D13), shared by every runtime of the process and held by the runtime
    //     as a std::shared_ptr. The job-inspection commands and the pair
    //     commands (link.import always; link.export bound to the shared lister)
    //     register on the same registry; set_job_submitter wires the runner as
    //     the kind=job dispatch target; the jobs table joins the db.* allowlist.
    //     File-path capability is forwarded from options unchanged (default-deny,
    //     threat model T-95): the builder never forces it on, so the REST
    //     per-request runtime refuses `path` while local-operator modes opt in.
    std::shared_ptr<commands::JobRunner> runner = process_job_runner();
    commands::register_job_commands(*runtime.registry, *runner);

    commands::PairCommandDeps pair_deps;
    pair_deps.service = runtime.link_service;
    pair_deps.max_inline_bytes =
        static_cast<std::size_t>(server.max_request_body_bytes);
    pair_deps.file_paths.allow_paths = options.file_paths.allow_paths;
    pair_deps.file_paths.root = options.file_paths.root;
    pair_deps.list_page =
        list_page_from(runtime.link_lister, runtime.link_service);
    commands::register_pair_commands(*runtime.registry, std::move(pair_deps));

    runtime.registry->set_job_submitter(runner);
    runtime.tables->add(commands::make_jobs_table_source(*runner));
    runtime.job_runner = runner;

    runtime.registry->set_audit_sink(runtime.audit_sink);

    // 7. The selected create-throttle limiter (threat model T-75): the
    //    in-memory default or the configured Redis limiter, chosen by the
    //    handle. Carried on the process-wide runtime so the REST create adapter
    //    reaches the same limiter `main` wired at startup, without a new
    //    process singleton. Null for a handle with no limiter wired (the
    //    bridge's unit-test fallback), where the adapter skips the throttle.
    runtime.create_rate_limiter = backend.createRateLimiter();

    return runtime;
}

}  // namespace composition
}  // namespace url_shortener
