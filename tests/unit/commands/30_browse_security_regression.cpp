#define BOOST_TEST_MODULE CommandsBrowseSecurityRegressionTest
#include <chrono>
#include <cstddef>
#include <memory>
#include <string>
#include <vector>

#include <boost/test/unit_test.hpp>

#include "url_shortener/analytics/click_event.hpp"
#include "url_shortener/app/link_backend.hpp"
#include "url_shortener/app/system_info.hpp"
#include "url_shortener/commands/audit.hpp"
#include "url_shortener/commands/command_registry.hpp"
#include "url_shortener/commands/envelope.hpp"
#include "url_shortener/commands/error_codes.hpp"
#include "url_shortener/composition/command_runtime.hpp"
#include "url_shortener/composition/link_backend_bootstrap.hpp"
#include "url_shortener/config/storage_config.hpp"
#include "url_shortener/core/config.hpp"
#include "url_shortener/storage/memory/in_memory_click_event_repository.hpp"

namespace url_shortener
{
// The process-global click-event repository the click_events table source
// reads.
storage::memory::InMemoryClickEventRepository& analyticsClickRepository();
}  // namespace url_shortener

using url_shortener::analytics::ClickEvent;
using url_shortener::app::LinkBackend;
using url_shortener::app::LinkBackendHandle;
using url_shortener::app::make_system_info;
using url_shortener::app::set_process_system_info;
using url_shortener::commands::AuditRecord;
using url_shortener::commands::CallerContext;
using url_shortener::commands::CommandErrorCode;
using url_shortener::commands::CommandRegistry;
using url_shortener::commands::CommandRequest;
using url_shortener::commands::CommandResponse;
using url_shortener::commands::IAuditSink;
using url_shortener::composition::build_command_runtime;
using url_shortener::composition::CommandRuntime;
using url_shortener::composition::CommandRuntimeOptions;

namespace
{

bool contains(const std::string& haystack, const std::string& needle)
{
    return haystack.find(needle) != std::string::npos;
}

std::size_t count_occurrences(const std::string& haystack,
                              const std::string& needle)
{
    std::size_t count = 0;
    std::size_t pos = haystack.find(needle);
    while (pos != std::string::npos) {
        ++count;
        pos = haystack.find(needle, pos + needle.size());
    }
    return count;
}

CallerContext caller(ControlSet role)
{
    CallerContext c;
    c.principal = "test-principal";
    c.transport = "test";
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

CommandRequest dry_run_request(std::string command, std::string args_json)
{
    CommandRequest req = request(std::move(command), std::move(args_json));
    req.options.dry_run = true;
    return req;
}

// Builds a fresh legacy runtime over the in-memory backend.
CommandRuntime fresh_runtime(const ServerConfig& server,
                             const LinkBackendHandle& backend)
{
    return build_command_runtime(server, backend);
}

// An audit sink that records every record it receives, so a test can inspect
// exactly which fields reach the audit line.
class RecordingAuditSink : public IAuditSink
{
public:
    std::vector<AuditRecord> records;
    void record(const AuditRecord& record) noexcept override
    {
        records.push_back(record);
    }
};

// Every field of AuditRecord (see include/url_shortener/commands/audit.hpp),
// collected for marker inspection: principal, transport, command, target and
// outcome. If a field is ever added to AuditRecord it must be added here too.
std::vector<std::string> audit_fields_of(const AuditRecord& record)
{
    return {record.principal,
            record.transport,
            record.command,
            record.target,
            record.outcome};
}

}  // namespace

// Type: unit (db.rows refuses every non-allowlisted name, T-51).
// Scenario: auth tables, the generic users table, sqlite_master/
//   information_schema, case variants of links, and injection-shaped names all
//   return not_found with the fixed "table not found" message, never echoing
//   the requested name.
// Boundaries: find() matches exactly; the catalog is the whole allowlist (C6).
// On failure first check: did find() start matching loosely, or did the handler
//   build its message from the argument?
BOOST_AUTO_TEST_CASE(db_rows_rejects_non_allowlisted_tables)
{
    LinkBackend::reset();
    ServerConfig server;
    LinkBackendHandle backend;
    const CommandRuntime runtime = fresh_runtime(server, backend);

    const std::vector<std::string> names = {
        "users",
        "app_users",
        "auth_sessions",
        "service_clients",
        "auth_audit_log",
        "user_credentials",
        "sqlite_master",
        "information_schema.tables",
        "links;--",
        "LINKS",
        "../links",
        "Click_Events",
    };

    for (const std::string& name : names) {
        const CommandResponse response = runtime.registry->execute(
            caller(ControlSet::User),
            request("db.rows", R"({"table":")" + name + R"("})"));

        BOOST_TEST(!response.ok, "name should not resolve: " << name);
        BOOST_TEST((response.error.code == CommandErrorCode::not_found));
        BOOST_TEST(response.error.message == "table not found");
        BOOST_TEST(response.error.message.find(name) == std::string::npos);
    }
}

// Type: unit (db.tables exposes exactly links and click_events, C6).
// Scenario: db.tables lists precisely two logical tables -- links and
//   click_events -- and no auth table.
// Boundaries: the catalog is built from exactly two add() calls.
// On failure first check: did the composition root register a third source, or
//   drop one?
BOOST_AUTO_TEST_CASE(db_tables_lists_exactly_links_and_click_events)
{
    LinkBackend::reset();
    ServerConfig server;
    LinkBackendHandle backend;
    const CommandRuntime runtime = fresh_runtime(server, backend);

    const CommandResponse response = runtime.registry->execute(
        caller(ControlSet::User), request("db.tables", "{}"));
    BOOST_REQUIRE(response.ok);

    // Each table object carries a `,"rows":` immediately after its name; column
    // objects never do, so this counts table entries exactly.
    BOOST_TEST(count_occurrences(response.result_json, R"(,"rows":)") == 2u);
    BOOST_TEST(contains(response.result_json, R"("name":"links","rows":)"));
    BOOST_TEST(
        contains(response.result_json, R"("name":"click_events","rows":)"));
    BOOST_TEST(!contains(response.result_json, "app_users"));
    BOOST_TEST(!contains(response.result_json, "auth_sessions"));
}

// Type: unit (click_events rows expose only the hashed client id, T-52).
// Scenario: a seeded click event lists through db.rows with a client_id_hash
//   cell and no ip/address/client_id/fingerprint key.
// Boundaries: the column list is a literal, hashed-only set.
// On failure first check: did the source start projecting a raw identifier?
BOOST_AUTO_TEST_CASE(click_events_rows_expose_only_hashed_client_id)
{
    LinkBackend::reset();
    ServerConfig server;
    LinkBackendHandle backend;
    const CommandRuntime runtime = fresh_runtime(server, backend);

    ClickEvent event;
    event.event_id = "evt-30-hash";
    event.slug = "sec-slug-30";
    event.domain = "example.com";
    event.occurred_at = std::chrono::system_clock::now();
    event.status_code = 302;
    event.client_id_hash = "HASHED-CLIENT-ID";
    url_shortener::analyticsClickRepository().InsertBatch({event}, nullptr);

    const CommandResponse response = runtime.registry->execute(
        caller(ControlSet::User),
        request("db.rows", R"({"table":"click_events"})"));
    BOOST_REQUIRE(response.ok);

    BOOST_TEST(contains(response.result_json, R"("client_id_hash":)"));
    BOOST_TEST(!contains(response.result_json, R"("ip":)"));
    BOOST_TEST(!contains(response.result_json, R"("address":)"));
    BOOST_TEST(!contains(response.result_json, R"("client_id":)"));
    BOOST_TEST(!contains(response.result_json, R"("fingerprint":)"));
}

// Type: unit (hostile free text stays valid JSON through db.rows, T-52).
// Scenario: a click event whose user_agent carries a quote, backslash, newline
//   and script tag lists through db.rows with the raw newline/quote escaped.
// Boundaries: every free-text cell is routed through jsonString by the source.
// On failure first check: did a cell bypass jsonString on the db.rows path?
BOOST_AUTO_TEST_CASE(click_events_hostile_free_text_stays_valid_json)
{
    LinkBackend::reset();
    ServerConfig server;
    LinkBackendHandle backend;
    const CommandRuntime runtime = fresh_runtime(server, backend);

    ClickEvent event;
    event.event_id = "evt-30-nasty";
    event.slug = "sec-slug-30-nasty";
    event.domain = "example.com";
    event.occurred_at = std::chrono::system_clock::now();
    event.status_code = 302;
    event.client_id_hash = "hash";
    event.user_agent = "x\"y\\z\n<script>";
    url_shortener::analyticsClickRepository().InsertBatch({event}, nullptr);

    const CommandResponse response = runtime.registry->execute(
        caller(ControlSet::User),
        request("db.rows",
                R"({"table":"click_events","q":"sec-slug-30-nasty"})"));
    BOOST_REQUIRE(response.ok);

    BOOST_TEST(response.result_json.find('\n') == std::string::npos);
    BOOST_TEST(contains(response.result_json, "x\\\"y\\\\z\\n"));
}

// Type: unit (system.info leaks no secret, T-53).
// Scenario: the process snapshot is built from a StorageConfig with a DSN,
//   redis address and sqlite path plus a ServerConfig with TLS material and the
//   analytics salt; system.info's serialized output contains none of the
//   sentinels. Sentinels are assembled at runtime to satisfy the secret
//   scanner.
// Boundaries: only allowlisted backend names and the non-secret subset leave.
// On failure first check: did make_system_info start reading a secret field, or
//   did build_command_runtime stop using the filtered process snapshot?
BOOST_AUTO_TEST_CASE(system_info_leaks_no_secret)
{
    LinkBackend::reset();

    const std::string at(1, '@');
    const std::string pg = std::string("postgres") + "://";
    const std::string rd = std::string("redis") + "://";

    StorageConfig storage;
    storage.metadata.backend = pg + "backend-SECRETPW" + at + "h/db";
    storage.metadata.sqlite_path = "/secret/path.db";
    storage.metadata.postgres_dsn = pg + "u:SECRETPW" + at + "h/db";
    storage.cache.backend = rd + ":TOKEN" + at + "h";
    storage.cache.redis_address = rd + ":TOKEN" + at + "h";
    storage.rate_limit.enabled = true;
    storage.rate_limit.backend = rd + ":TOKEN" + at + "h";
    storage.rate_limit.redis_address = rd + ":TOKEN" + at + "h";

    ServerConfig server;
    server.tls.certificate_chain_file = "/tls/CERTSENTINEL.pem";
    server.tls.private_key_file = "/tls/KEYSENTINEL.pem";
    server.tls.private_key_passphrase = std::string("PASSPHRASE") + "SENTINEL";
    server.analytics_client_hash_salt = std::string("SALT") + "SENTINEL";

    set_process_system_info(make_system_info(storage, server));

    LinkBackendHandle backend;
    const CommandRuntime runtime = fresh_runtime(server, backend);
    const CommandResponse response = runtime.registry->execute(
        caller(ControlSet::User), request("system.info", "{}"));
    BOOST_REQUIRE(response.ok);

    const std::vector<std::string> forbidden = {"SECRETPW",
                                                "TOKEN",
                                                "/secret/path.db",
                                                pg,
                                                rd,
                                                at,
                                                "CERTSENTINEL",
                                                "KEYSENTINEL",
                                                "PASSPHRASESENTINEL",
                                                "SALTSENTINEL"};
    for (const std::string& marker : forbidden) {
        BOOST_TEST(!contains(response.result_json, marker),
                   "secret marker leaked into system.info: " << marker);
    }

    // Restore the default process snapshot for following cases / executables.
    set_process_system_info(
        make_system_info(StorageConfig {}, ServerConfig {}));
}

// Type: unit (link.list items expose no owner/attributes, C6).
// Scenario: a seeded link lists through link.list without an owner or
//   attributes key.
// Boundaries: the item projection is serializeLinkViewJson, which omits both.
// On failure first check: did the list item shape start leaking internal
//   fields?
BOOST_AUTO_TEST_CASE(link_list_items_expose_no_owner_or_attributes)
{
    LinkBackend::reset();
    ServerConfig server;
    LinkBackendHandle backend;
    const CommandRuntime runtime = fresh_runtime(server, backend);

    runtime.registry->execute(
        caller(ControlSet::Admin),
        request(
            "link.create",
            R"({"target_url":"https://example.com/o","slug":"owner-slug-30"})"));

    // status=all requires write/admin (threat model T-71); this case checks the
    // item projection, so it lists as Admin.
    const CommandResponse listed = runtime.registry->execute(
        caller(ControlSet::Admin),
        request("link.list", R"({"status":"all","limit":500})"));
    BOOST_REQUIRE(listed.ok);
    BOOST_TEST(contains(listed.result_json, "owner-slug-30"));
    BOOST_TEST(!contains(listed.result_json, "\"owner\""));
    BOOST_TEST(!contains(listed.result_json, "\"attributes\""));
}

// Type: unit (out-of-bounds limit/q rejected before the port, T-56).
// Scenario: limit 0 and 501 on link.list and db.rows, and an oversized q (257
//   bytes) on both, each fail invalid_field.
// Boundaries: the ArgSpec bounds gate every value before the handler.
// On failure first check: did a bound drift, or validation stop preceding the
//   handler?
BOOST_AUTO_TEST_CASE(out_of_bounds_limit_and_q_rejected)
{
    LinkBackend::reset();
    ServerConfig server;
    LinkBackendHandle backend;
    const CommandRuntime runtime = fresh_runtime(server, backend);

    const std::string big_q(257, 'a');
    const std::vector<CommandRequest> bad = {
        request("link.list", R"({"limit":0})"),
        request("link.list", R"({"limit":501})"),
        request("link.list", R"({"q":")" + big_q + R"("})"),
        request("db.rows", R"({"table":"links","limit":0})"),
        request("db.rows", R"({"table":"links","limit":501})"),
        request("db.rows", R"({"table":"links","q":")" + big_q + R"("})"),
    };

    for (const CommandRequest& req : bad) {
        const CommandResponse response =
            runtime.registry->execute(caller(ControlSet::User), req);
        BOOST_TEST(!response.ok, "should be rejected: " << req.command);
        BOOST_TEST((response.error.code == CommandErrorCode::invalid_field));
    }
}

// Type: unit (a read-only User cannot mutate; the store is byte-identical, C7).
// Scenario: with a seeded link, a ControlSet::User is denied every write link
//   command -- create, update, delete, enable, disable, restore, and the
//   dry_run variants of create/update -- all with not_authorized, and
//   link.list(all) is byte-identical before and after.
// Boundaries: permission is enforced in execute before validation/dry_run, so
//   even a dry_run write is denied, not "unsupported".
// On failure first check: did a write descriptor drop its write permission, or
//   did a denied path still mutate?
BOOST_AUTO_TEST_CASE(read_only_user_denied_every_write_store_unchanged)
{
    LinkBackend::reset();
    ServerConfig server;
    LinkBackendHandle backend;
    const CommandRuntime runtime = fresh_runtime(server, backend);
    CommandRegistry& registry = *runtime.registry;

    const std::string slug = "seed-30";
    BOOST_REQUIRE(
        registry
            .execute(
                caller(ControlSet::Admin),
                request("link.create",
                        R"({"target_url":"https://example.com/seed","slug":")"
                            + slug + R"("})"))
            .ok);

    // The full-store snapshot (status=all) requires write/admin (T-71); it is a
    // read-back to prove the writes below did not mutate, so it runs as Admin.
    const CommandResponse before = registry.execute(
        caller(ControlSet::Admin),
        request("link.list", R"({"status":"all","limit":500})"));
    BOOST_REQUIRE(before.ok);

    const std::vector<CommandRequest> writes = {
        request("link.create",
                R"({"target_url":"https://example.com/x","slug":"denied-30"})"),
        request("link.update",
                R"({"slug":")" + slug + R"(","redirect_type":"permanent"})"),
        request("link.delete", R"({"slug":")" + slug + R"("})"),
        request("link.enable", R"({"slug":")" + slug + R"("})"),
        request("link.disable", R"({"slug":")" + slug + R"("})"),
        request("link.restore", R"({"slug":")" + slug + R"("})"),
        dry_run_request(
            "link.create",
            R"({"target_url":"https://example.com/y","slug":"denied-dry-30"})"),
        dry_run_request(
            "link.update",
            R"({"slug":")" + slug + R"(","redirect_type":"permanent"})"),
    };

    for (const CommandRequest& req : writes) {
        const CommandResponse response =
            registry.execute(caller(ControlSet::User), req);
        BOOST_TEST(!response.ok, "write must be denied: " << req.command);
        BOOST_TEST((response.error.code == CommandErrorCode::not_authorized),
                   "expected not_authorized for " << req.command);
    }

    const CommandResponse after = registry.execute(
        caller(ControlSet::Admin),
        request("link.list", R"({"status":"all","limit":500})"));
    BOOST_REQUIRE(after.ok);

    // No mutation occurred, so the full listing is byte-identical.
    BOOST_TEST(after.result_json == before.result_json);
}

// Type: unit (no argument value ever reaches the audit line, C6/T-14/T-15).
// Scenario: with a recording sink, link.create runs with a distinctive target
//   URL, metadata value, tag and campaign name plus an explicit slug; the one
//   emitted record has target == slug and no AuditRecord field carries any
//   argument marker. A failing create (private target -> invalid_url) and a
//   dry_run create are also exercised: still no marker in any field of any
//   emitted record.
// Boundaries: the audit record deliberately carries no argument values (only
//   principal/transport/command/target/outcome); the target is the validated
//   slug, which is not treated as a marker here.
// On failure first check: did the registry start copying an argument value into
//   an AuditRecord field, or did a new field get added without sanitization?
BOOST_AUTO_TEST_CASE(audit_record_carries_no_argument_values)
{
    LinkBackend::reset();
    ServerConfig server;
    LinkBackendHandle backend;

    CommandRuntimeOptions options;
    auto sink = std::make_shared<RecordingAuditSink>();
    options.audit_sink = sink;
    const CommandRuntime runtime =
        build_command_runtime(server, backend, options);
    CommandRegistry& registry = *runtime.registry;

    // Distinctive markers carried only as ARGUMENT VALUES. None is ever used as
    // a slug, since the slug legitimately becomes the audit target.
    const std::vector<std::string> markers = {
        "audit-marker",    // target host
        "secret-path",     // target path
        "MDVALUEMARKER",   // metadata value
        "TAGMARKER",       // tag
        "CAMPAIGNMARKER",  // campaign name
    };

    // 1) Successful create: exactly one record, target == slug, no marker.
    const std::string ok_slug = "audit-arg-slug-30";
    const CommandResponse created = registry.execute(
        caller(ControlSet::Admin),
        request(
            "link.create",
            R"({"target_url":"https://audit-marker.example.com/secret-path",)"
            R"("slug":")"
                + ok_slug
                + R"(","tags":["TAGMARKER"],"metadata":{"mk":"MDVALUEMARKER"},)"
                  R"("campaign":{"name":"CAMPAIGNMARKER"}})"));
    BOOST_REQUIRE(created.ok);
    BOOST_REQUIRE_EQUAL(sink->records.size(), 1u);
    BOOST_TEST(sink->records[0].command == "link.create");
    BOOST_TEST(sink->records[0].outcome == "ok");
    BOOST_TEST(sink->records[0].target == ok_slug);

    // 2) Failing create (private target -> invalid_url): still no marker.
    registry.execute(
        caller(ControlSet::Admin),
        request("link.create",
                R"({"target_url":"http://127.0.0.1/secret-path",)"
                R"("slug":"audit-fail-slug-30",)"
                R"("metadata":{"mk":"MDVALUEMARKER"}})"));

    // 3) dry_run create: still no marker.
    registry.execute(
        caller(ControlSet::Admin),
        dry_run_request(
            "link.create",
            R"({"target_url":"https://audit-marker.example.com/secret-path",)"
            R"("slug":"audit-dry-slug-30","tags":["TAGMARKER"]})"));

    // No AuditRecord field of ANY emitted record carries an argument value.
    BOOST_TEST(!sink->records.empty());
    for (const AuditRecord& record : sink->records) {
        for (const std::string& value : audit_fields_of(record)) {
            for (const std::string& marker : markers) {
                BOOST_TEST(
                    value.find(marker) == std::string::npos,
                    "argument value leaked into audit field: " << marker);
            }
        }
    }
}
