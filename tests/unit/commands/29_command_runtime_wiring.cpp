#define BOOST_TEST_MODULE CommandsCommandRuntimeWiringTest
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

#include <boost/test/unit_test.hpp>

#include "url_shortener/analytics/click_event.hpp"
#include "url_shortener/analytics/click_event_query.hpp"
#include "url_shortener/app/app_error.hpp"
#include "url_shortener/app/legacy_adapters.hpp"
#include "url_shortener/app/link_backend.hpp"
#include "url_shortener/app/link_command_service.hpp"
#include "url_shortener/app/link_lister.hpp"
#include "url_shortener/app/system_info.hpp"
#include "url_shortener/app/table_source.hpp"
#include "url_shortener/commands/audit.hpp"
#include "url_shortener/commands/browse_commands.hpp"
#include "url_shortener/commands/builtin_commands.hpp"
#include "url_shortener/commands/catalog.hpp"
#include "url_shortener/commands/command_registry.hpp"
#include "url_shortener/commands/envelope.hpp"
#include "url_shortener/commands/error_codes.hpp"
#include "url_shortener/commands/link_commands.hpp"
#include "url_shortener/commands/system_commands.hpp"
#include "url_shortener/composition/command_runtime.hpp"
#include "url_shortener/composition/link_backend_bootstrap.hpp"
#include "url_shortener/config/storage_config.hpp"
#include "url_shortener/core/config.hpp"
#include "url_shortener/storage/memory/in_memory_click_event_repository.hpp"

namespace url_shortener
{
// The process-global click-event repository the click_events table source
// reads; declared here as every other consumer does (no public header).
storage::memory::InMemoryClickEventRepository& analyticsClickRepository();
}  // namespace url_shortener

using url_shortener::Link;
using url_shortener::analytics::ClickEvent;
using url_shortener::analytics::ClickEventPage;
using url_shortener::analytics::ClickEventQuery;
using url_shortener::app::AppError;
using url_shortener::app::AppErrorCode;
using url_shortener::app::ILinkLister;
using url_shortener::app::ITableSource;
using url_shortener::app::LegacyLinkStatsReader;
using url_shortener::app::LinkCommandService;
using url_shortener::app::LinkListPage;
using url_shortener::app::LinkListQuery;
using url_shortener::app::make_system_info;
using url_shortener::app::Result;
using url_shortener::app::set_process_system_info;
using url_shortener::app::SystemInfo;
using url_shortener::app::TableColumn;
using url_shortener::app::TableQuery;
using url_shortener::app::TableRowsPage;
using url_shortener::commands::AuditRecord;
using url_shortener::commands::CallerContext;
using url_shortener::commands::CommandErrorCode;
using url_shortener::commands::CommandRegistry;
using url_shortener::commands::CommandRequest;
using url_shortener::commands::CommandResponse;
using url_shortener::commands::IAuditSink;
using url_shortener::commands::LoggerAuditSink;
using url_shortener::commands::register_browse_commands;
using url_shortener::commands::register_builtin_commands;
using url_shortener::commands::register_documented_commands;
using url_shortener::commands::make_system_info_descriptor;
using url_shortener::commands::register_link_commands;
using url_shortener::commands::render_catalog_markdown;
using url_shortener::composition::build_command_runtime;
using url_shortener::composition::CommandRuntime;
using url_shortener::composition::CommandRuntimeOptions;

namespace
{

bool contains(const std::string& haystack, const std::string& needle)
{
    return haystack.find(needle) != std::string::npos;
}

CallerContext caller(ControlSet role, const char* transport = "test")
{
    CallerContext c;
    c.principal = "test-principal";
    c.transport = transport;
    c.control_set = role;
    return c;
}

CommandRequest request(std::string command, std::string args_json)
{
    CommandRequest req;
    req.id = "client-1";
    req.command = std::move(command);
    req.args_json = std::move(args_json);
    return req;
}

// Extracts the trailing "total":N value from a link.list result body.
std::size_t parse_total(const std::string& json)
{
    const std::string key = "\"total\":";
    const std::size_t pos = json.rfind(key);
    BOOST_REQUIRE(pos != std::string::npos);
    return static_cast<std::size_t>(std::stoul(json.substr(pos + key.size())));
}

// The current total number of stored click events, via the same ListEvents
// path the click_events source counts through.
std::size_t click_total()
{
    ClickEventQuery query;
    ClickEventPage page;
    url_shortener::analyticsClickRepository().ListEvents(query, &page, nullptr);
    return page.total;
}

// An audit sink that records every record it receives, for the audit-wiring
// assertions.
class RecordingAuditSink : public IAuditSink
{
public:
    std::vector<AuditRecord> records;
    void record(const AuditRecord& record) noexcept override
    {
        records.push_back(record);
    }
};

// A counting ILinkStore: every method bumps a shared counter so a test can
// prove a link command handler never ran. Returns benign values.
class CountingLinkStore : public url_shortener::app::ILinkStore
{
public:
    int& m_calls;
    explicit CountingLinkStore(int& calls)
        : m_calls(calls)
    {
    }

    bool create(const Link&, AppError* = nullptr) override
    {
        ++m_calls;
        return true;
    }
    std::optional<Link> findBySlug(const std::string&) const override
    {
        ++m_calls;
        return std::nullopt;
    }
    std::optional<Link> findById(const std::string&) const override
    {
        ++m_calls;
        return std::nullopt;
    }
    bool slugExists(const std::string&) const override
    {
        ++m_calls;
        return false;
    }
    void invalidateCache(const std::string&) override { ++m_calls; }
    bool update(const Link&, AppError* = nullptr) override
    {
        ++m_calls;
        return true;
    }
};

// A counting ILinkLister: list() bumps a counter and returns an empty page.
class CountingLister : public ILinkLister
{
public:
    mutable int calls = 0;
    Result<LinkListPage> list(const LinkListQuery&) const override
    {
        ++calls;
        Result<LinkListPage> result;
        result.value = LinkListPage {};
        return result;
    }
};

// A counting ITableSource: row_count()/rows() bump a counter.
class CountingSource : public ITableSource
{
public:
    std::string m_name = "links";
    std::vector<TableColumn> m_columns;
    mutable int calls = 0;

    const std::string& name() const override { return m_name; }
    bool writable() const override { return true; }
    const std::vector<TableColumn>& columns() const override
    {
        return m_columns;
    }
    Result<std::size_t> row_count() const override
    {
        ++calls;
        Result<std::size_t> result;
        result.value = 0;
        return result;
    }
    Result<TableRowsPage> rows(const TableQuery&) const override
    {
        ++calls;
        Result<TableRowsPage> result;
        result.value = TableRowsPage {};
        return result;
    }
};

// Unique temp path helper for the SQLite case.
std::filesystem::path temp_path(const std::string& suffix)
{
    static int counter = 0;
    return std::filesystem::temp_directory_path()
        / (std::string("urlsh_runtime29_") + std::to_string(++counter)
           + suffix);
}

void write_file(const std::filesystem::path& path, const std::string& content)
{
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out << content;
}

}  // namespace

// Type: unit (default/legacy mode registers exactly the expected command set).
// Scenario: build_command_runtime over an inactive backend handle produces a
//   registry containing precisely the nine link commands, link.list, the two
//   db.* commands, system.info and command.list -- 14 names, no more, no less.
// Boundaries: an inactive handle selects the legacy lister; the registry is the
//   sole product registry (C11).
// On failure first check: did a factory stop being registered, or a name drift?
BOOST_AUTO_TEST_CASE(legacy_mode_registers_expected_names)
{
    url_shortener::app::LinkBackend::reset();
    ServerConfig server;
    url_shortener::app::LinkBackendHandle backend;  // inactive / legacy

    const CommandRuntime runtime = build_command_runtime(server, backend);
    BOOST_REQUIRE(runtime.registry != nullptr);

    std::set<std::string> names;
    for (const auto* descriptor : runtime.registry->list()) {
        names.insert(descriptor->name);
    }

    const std::set<std::string> expected = {"link.create",
                                            "link.get",
                                            "link.update",
                                            "link.delete",
                                            "link.enable",
                                            "link.disable",
                                            "link.restore",
                                            "link.preview",
                                            "link.stats",
                                            "link.list",
                                            "db.tables",
                                            "db.rows",
                                            "system.info",
                                            "command.list"};
    BOOST_TEST((names == expected));
}

// Type: unit (end-to-end lifecycle through the composed runtime).
// Scenario: create -> list -> get -> disable -> list(status=disabled) ->
//   delete -> list(status=deleted) all flow through execute on the in-memory
//   backend, each step observing the previous mutation.
// Boundaries: every call goes through execute (C7); writes need an Admin
// caller. On failure first check: did a handler stop reaching the live store,
// or a
//   lifecycle status stop being observable through link.list?
BOOST_AUTO_TEST_CASE(create_list_get_lifecycle_flow)
{
    url_shortener::app::LinkBackend::reset();
    ServerConfig server;
    url_shortener::app::LinkBackendHandle backend;
    const CommandRuntime runtime = build_command_runtime(server, backend);
    CommandRegistry& registry = *runtime.registry;

    const std::string slug = "flow-slug-29";

    const CommandResponse created = registry.execute(
        caller(ControlSet::Admin),
        request("link.create",
                R"({"target_url":"https://example.com/flow","slug":")" + slug
                    + R"("})"));
    BOOST_TEST(created.ok);

    const CommandResponse got =
        registry.execute(caller(ControlSet::User),
                         request("link.get", R"({"slug":")" + slug + R"("})"));
    BOOST_TEST(got.ok);
    BOOST_TEST(contains(got.result_json, slug));

    const CommandResponse listed_active = registry.execute(
        caller(ControlSet::User),
        request("link.list", R"({"status":"active","limit":500})"));
    BOOST_TEST(listed_active.ok);
    BOOST_TEST(contains(listed_active.result_json, slug));

    const CommandResponse disabled = registry.execute(
        caller(ControlSet::Admin),
        request("link.disable", R"({"slug":")" + slug + R"("})"));
    BOOST_TEST(disabled.ok);

    const CommandResponse listed_disabled = registry.execute(
        caller(ControlSet::User),
        request("link.list", R"({"status":"disabled","limit":500})"));
    BOOST_TEST(listed_disabled.ok);
    BOOST_TEST(contains(listed_disabled.result_json, slug));

    const CommandResponse deleted = registry.execute(
        caller(ControlSet::Admin),
        request("link.delete", R"({"slug":")" + slug + R"("})"));
    BOOST_TEST(deleted.ok);

    const CommandResponse listed_deleted = registry.execute(
        caller(ControlSet::User),
        request("link.list", R"({"status":"deleted","limit":500})"));
    BOOST_TEST(listed_deleted.ok);
    BOOST_TEST(contains(listed_deleted.result_json, slug));
}

// Type: unit (db.tables row counts agree with the browse ports).
// Scenario: db.tables reports links and click_events with row counts equal to
//   link.list(status=all)'s total and the ListEvents total respectively, after
//   seeding a link and two click events.
// Boundaries: db.tables counts come from the same ports the browse commands
// use. On failure first check: did a source's row_count diverge from its
// rows(), or
//   did the catalog lose a table?
BOOST_AUTO_TEST_CASE(db_tables_counts_match_browse_totals)
{
    url_shortener::app::LinkBackend::reset();
    ServerConfig server;
    url_shortener::app::LinkBackendHandle backend;
    const CommandRuntime runtime = build_command_runtime(server, backend);
    CommandRegistry& registry = *runtime.registry;

    registry.execute(
        caller(ControlSet::Admin),
        request(
            "link.create",
            R"({"target_url":"https://example.com/c","slug":"count-slug-29"})"));

    ClickEvent event;
    event.event_id = "evt-29-a";
    event.slug = "count-slug-29";
    event.domain = "example.com";
    event.occurred_at = std::chrono::system_clock::now();
    event.status_code = 302;
    event.client_id_hash = "hash-a";
    ClickEvent event2 = event;
    event2.event_id = "evt-29-b";
    event2.client_id_hash = "hash-b";
    url_shortener::analyticsClickRepository().InsertBatch({event, event2},
                                                          nullptr);

    const CommandResponse listed = registry.execute(
        caller(ControlSet::User),
        request("link.list", R"({"status":"all","limit":500})"));
    BOOST_REQUIRE(listed.ok);
    const std::size_t link_total = parse_total(listed.result_json);

    const CommandResponse tables =
        registry.execute(caller(ControlSet::User), request("db.tables", "{}"));
    BOOST_REQUIRE(tables.ok);
    BOOST_TEST(
        contains(tables.result_json,
                 R"("name":"links","rows":)" + std::to_string(link_total)));
    BOOST_TEST(contains(
        tables.result_json,
        R"("name":"click_events","rows":)" + std::to_string(click_total())));
}

// Type: unit (command.list advertises each descriptor's permission + mutating).
// Scenario: command.list output carries perm/mutating for representative
//   commands: link.create is write+mutating, system.info is read+non-mutating.
// Boundaries: the schema dump reflects the descriptors the runtime registered.
// On failure first check: did a descriptor flip permission/mutating, or did
//   command.list stop listing the runtime's registry?
BOOST_AUTO_TEST_CASE(command_list_reports_permission_and_mutating)
{
    url_shortener::app::LinkBackend::reset();
    ServerConfig server;
    url_shortener::app::LinkBackendHandle backend;
    const CommandRuntime runtime = build_command_runtime(server, backend);

    const CommandResponse listed = runtime.registry->execute(
        caller(ControlSet::User), request("command.list", "{}"));
    BOOST_REQUIRE(listed.ok);

    BOOST_TEST(
        contains(listed.result_json, R"("name":"link.create","summary":)"));
    BOOST_TEST(contains(listed.result_json, R"("perm":"write")"));
    BOOST_TEST(contains(listed.result_json, R"("mutating":true)"));
    BOOST_TEST(
        contains(listed.result_json, R"("name":"system.info","summary":)"));
    BOOST_TEST(contains(listed.result_json, R"("perm":"read")"));
}

// Type: unit (default audit sink is a non-null LoggerAuditSink).
// Scenario: a runtime built with default options installs a LoggerAuditSink
//   (never null), so every mutating command is audited in production (ADR
//   0002).
// Boundaries: there is no way to build a runtime without a sink.
// On failure first check: did build_command_runtime stop defaulting the sink,
//   or install a different type?
BOOST_AUTO_TEST_CASE(default_options_install_logger_audit_sink)
{
    url_shortener::app::LinkBackend::reset();
    ServerConfig server;
    url_shortener::app::LinkBackendHandle backend;
    const CommandRuntime runtime = build_command_runtime(server, backend);

    BOOST_REQUIRE(runtime.audit_sink != nullptr);
    BOOST_TEST(dynamic_cast<LoggerAuditSink*>(runtime.audit_sink.get())
               != nullptr);
}

// Type: unit (injected recording sink audits exactly the mutating command).
// Scenario: with a recording sink, link.create records exactly one record
//   (outcome ok, target = slug) and link.get / link.list record nothing.
// Boundaries: only mutating commands are audited (R-C); the target is the slug.
// On failure first check: did the registry stop emitting a record, or audit a
//   read command?
BOOST_AUTO_TEST_CASE(recording_sink_audits_only_mutating_commands)
{
    url_shortener::app::LinkBackend::reset();
    ServerConfig server;
    url_shortener::app::LinkBackendHandle backend;

    CommandRuntimeOptions options;
    auto sink = std::make_shared<RecordingAuditSink>();
    options.audit_sink = sink;
    const CommandRuntime runtime =
        build_command_runtime(server, backend, options);
    CommandRegistry& registry = *runtime.registry;

    const std::string slug = "audit-slug-29";
    const CommandResponse created = registry.execute(
        caller(ControlSet::Admin),
        request("link.create",
                R"({"target_url":"https://example.com/a","slug":")" + slug
                    + R"("})"));
    BOOST_REQUIRE(created.ok);

    BOOST_REQUIRE_EQUAL(sink->records.size(), 1u);
    BOOST_TEST(sink->records[0].command == "link.create");
    BOOST_TEST(sink->records[0].outcome == "ok");
    BOOST_TEST(sink->records[0].target == slug);

    registry.execute(caller(ControlSet::User),
                     request("link.get", R"({"slug":")" + slug + R"("})"));
    registry.execute(caller(ControlSet::User),
                     request("link.list", R"({"limit":10})"));
    BOOST_TEST(sink->records.size() == 1u);
}

// Type: unit (catalog rendering invokes no handler, T-58/T-19).
// Scenario: the link + browse descriptors registered over counting fakes, then
//   rendered with render_catalog_markdown, leave every counter at zero -- the
//   documented/metadata path never calls execute.
// Boundaries: register + render read metadata only; handlers run only through
//   execute.
// On failure first check: did render_catalog_markdown start invoking a handler,
//   or did registration execute one?
BOOST_AUTO_TEST_CASE(catalog_rendering_invokes_no_handler)
{
    int store_calls = 0;
    CountingLinkStore store(store_calls);
    LegacyLinkStatsReader stats;
    ServerConfig config;
    LinkCommandService service(store, stats, config);

    CountingLister lister;
    url_shortener::app::TableCatalog catalog;
    auto source = std::make_shared<CountingSource>();
    catalog.add(source);

    CommandRegistry registry;
    register_link_commands(registry, service);
    register_browse_commands(registry, lister, catalog, SystemInfo {});
    register_builtin_commands(registry);

    const std::string markdown = render_catalog_markdown(registry);
    BOOST_TEST(!markdown.empty());

    BOOST_TEST(store_calls == 0);
    BOOST_TEST(lister.calls == 0);
    BOOST_TEST(source->calls == 0);

    // The documented registry builds the same set without invoking a handler.
    CommandRegistry documented;
    register_documented_commands(documented);
    BOOST_TEST(documented.list().size() == 14u);
    BOOST_TEST(!render_catalog_markdown(documented).empty());
}

// Type: unit (SQLite-backed runtime persists across a second runtime, 02.0).
// Scenario: a runtime over a SQLite StorageConfig reports persistent=true in
//   system.info, a create flows to the file, and a second runtime built on the
//   same file sees the link -- the cross-process persistence promise.
// Boundaries: an active handle selects the RepositoryLinkLister; the store is
//   the installed RepositoryLinkStore.
// On failure first check: did build_command_runtime stop using the active
//   backend's repository, or did the store stop persisting?
BOOST_AUTO_TEST_CASE(sqlite_backed_runtime_persists_to_file)
{
    const auto db = temp_path(".db");
    const auto yaml = temp_path(".yaml");
    std::filesystem::remove(db);
    write_file(
        yaml,
        "metadata:\n  backend: sqlite\n  sqlite_path: " + db.string() + "\n");

    ServerConfig server;
    server.storage_config_path = yaml.string();

    StorageConfig sc;
    sc.metadata.backend = "sqlite";
    sc.metadata.sqlite_path = db.string();
    set_process_system_info(make_system_info(sc, server));

    const std::string slug = "sqlite-slug-29";
    {
        url_shortener::app::LinkBackend::reset();
        auto handle1 = url_shortener::app::InitLinkBackend(server);
        BOOST_REQUIRE(handle1.active());
        BOOST_REQUIRE(handle1.repository() != nullptr);

        const CommandRuntime runtime1 = build_command_runtime(server, handle1);

        const CommandResponse info = runtime1.registry->execute(
            caller(ControlSet::Admin), request("system.info", "{}"));
        BOOST_REQUIRE(info.ok);
        BOOST_TEST(contains(info.result_json, R"("persistent":true)"));

        const CommandResponse created = runtime1.registry->execute(
            caller(ControlSet::Admin),
            request("link.create",
                    R"({"target_url":"https://example.com/s","slug":")" + slug
                        + R"("})"));
        BOOST_REQUIRE(created.ok);

        // A second runtime built on the same file sees the link.
        auto handle2 = url_shortener::app::InitLinkBackend(server);
        const CommandRuntime runtime2 = build_command_runtime(server, handle2);
        const CommandResponse got = runtime2.registry->execute(
            caller(ControlSet::User),
            request("link.get", R"({"slug":")" + slug + R"("})"));
        BOOST_TEST(got.ok);
        BOOST_TEST(contains(got.result_json, slug));
    }

    // Restore the process-wide defaults for any following case / executable.
    url_shortener::app::LinkBackend::reset();
    set_process_system_info(
        make_system_info(StorageConfig {}, ServerConfig {}));
    std::filesystem::remove(db);
    std::filesystem::remove(yaml);
}

// Type: unit (fail-closed duplicate registration on the built runtime registry).
// Scenario: the runtime registry already holds "system.info"; adding a second
//   descriptor whose name collides with an existing one throws invalid_argument.
// Boundaries: CommandRegistry::add rejects a duplicate name rather than silently
//   shadowing a command (C11, one definition per command).
// On failure first check: did CommandRegistry::add stop enforcing unique names?
BOOST_AUTO_TEST_CASE(duplicate_command_name_throws)
{
    url_shortener::app::LinkBackend::reset();
    ServerConfig server;
    url_shortener::app::LinkBackendHandle backend;  // inactive / legacy

    const CommandRuntime runtime = build_command_runtime(server, backend);
    BOOST_REQUIRE(runtime.registry != nullptr);

    // "system.info" is already registered by build_command_runtime; registering
    // a second descriptor with that name must fail closed.
    BOOST_CHECK_THROW(
        runtime.registry->add(make_system_info_descriptor(SystemInfo {})),
        std::invalid_argument);
}
