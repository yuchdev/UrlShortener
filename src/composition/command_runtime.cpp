#include <memory>
#include <utility>

#include "url_shortener/composition/command_runtime.hpp"

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
#include "url_shortener/commands/link_commands.hpp"
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
    runtime.registry->set_audit_sink(runtime.audit_sink);

    return runtime;
}

}  // namespace composition
}  // namespace url_shortener
