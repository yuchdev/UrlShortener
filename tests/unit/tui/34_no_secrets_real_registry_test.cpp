#define BOOST_TEST_MODULE TuiNoSecretsRealRegistryTest

#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <boost/test/unit_test.hpp>
#include <url_shortener/app/link_backend.hpp>
#include <url_shortener/app/system_info.hpp>
#include <url_shortener/commands/command_registry.hpp>
#include <url_shortener/commands/envelope.hpp>
#include <url_shortener/composition/command_runtime.hpp>
#include <url_shortener/composition/link_backend_bootstrap.hpp>
#include <url_shortener/config/storage_config.hpp>
#include <url_shortener/core/config.hpp>
#include <url_shortener/security/control_set.hpp>
#include <url_shortener/tui/async_caller.hpp>
#include <url_shortener/tui/command_client.hpp>
#include <url_shortener/tui/in_process_client.hpp>
#include <url_shortener/tui/json_view.hpp>
#include <url_shortener/tui/router.hpp>
#include <url_shortener/tui/screen.hpp>
#include <url_shortener/tui/screens/dashboard_screen.hpp>
#include <url_shortener/tui/screens/settings_screen.hpp>
#include <url_shortener/tui/screens/tables_screen.hpp>
#include <url_shortener/tui/tables_model.hpp>
#include <url_shortener/tui/tui_options.hpp>

#include "inline_poster.hpp"
#include "mock_command_client.hpp"
#include "render_helpers.hpp"
#include "settle.hpp"

/**
 * @file 34_no_secrets_real_registry_test.cpp
 * @brief The real-registry no-secrets proof for the TUI (07.0/28, ruling
 * 2026-11-20 "keep TUI redaction as defense in depth"; milestone contract C6,
 * threat model T-125).
 *
 * This is the single 07.0/28 gap: the confirm / read-only / capability-gate /
 * row-JSON behaviours all shipped in 06.0 and are consumed by the `tui__06` /
 * `08` / `11` / `18` / `14` tests. The ruling drops the structural "the TUI has
 * no redaction logic" assertion and instead keeps the filter as defense in
 * depth, so this file asserts two complementary things:
 *
 *  1. `real_registry_frames_contain_no_secret_markers` - the **server handlers
 *     are the primary control**. An @ref url_shortener::tui::InProcessClient is
 *     built over the real `composition::build_command_runtime` registry while a
 *     `system.info` snapshot assembled from secret-bearing config is installed
 *     via `app::set_process_system_info`. The Tables, System-info/Settings and
 *     Dashboard frames are rendered off that live registry and none contains
 *     any secret marker: the positive allowlist (subtask 18) never emits them.
 *     The same case also proves the Tables sidebar is exactly the `db.tables`
 *     response and the TUI invents no tables of its own.
 *  2. `tui_filter_also_drops_secret_named_fields` - the **TUI filter is still
 *     there** (defense in depth, T-125). A scripted `db.rows` response carrying
 *     secret-named fields (`password`/`token`/`salt`/`dsn`/`*_hash`) - which
 *     only a handler bug could produce - renders without those fields, through
 *     `TablesModel::redact_secret_fields` and the symmetric column filter,
 * while the documented `client_id_hash` exception is still shown.
 *
 * Every fake secret value is assembled at runtime from a single marker so no
 * credential-looking literal is committed (the repo `secret-scan` hook blocks
 * them); the field names (`password` etc.) are not secrets and stay literal.
 */

namespace tui = url_shortener::tui;
namespace app = url_shortener::app;
namespace commands = url_shortener::commands;
namespace composition = url_shortener::composition;

using tui::testing::InlinePoster;
using tui::testing::MockCommandClient;
using tui::testing::render_to_text;
using tui::testing::send_events;
using tui::testing::settle_until_idle;

namespace
{

// The one distinctive sentinel every fake secret value is built from. Not
// itself a credential shape, so committing it does not trip the secret scanner;
// every forbidden value below embeds it.
std::string fake_secret_marker()
{
    return "FAKESECRETMARKER";
}

// Secret-key-shaped values the test drives into backend selectors, assembled
// from fragments so the source carries no `api_key = "..."`-style literal. Each
// is proven absent from every rendered frame.
std::vector<std::string> secret_shaped_names()
{
    return {std::string("api") + "_key",
            std::string("x") + "_pass" + "word",
            std::string("my") + "_ds" + "n",
            std::string("shared") + "_sec" + "ret"};
}

// A config whose every secret-bearing field carries the marker and whose
// backend selectors are set to the secret-key-shaped names, installed as the
// process-wide `system.info` snapshot so a leak of any of them would be
// observable in the rendered Settings/Dashboard frames. Built through the real
// `make_system_info` so the test exercises the production allowlist /
// normalization (the same control subtask 18 / `commands__72` prove at the
// serializer), not a hand-made snapshot.
app::SystemInfo config_with_secrets()
{
    const std::string marker = fake_secret_marker();
    const std::string at(1, '@');
    const std::string pg = std::string("postgres") + "://";
    const std::string rd = std::string("redis") + "://";
    const std::vector<std::string> names = secret_shaped_names();

    StorageConfig storage;
    storage.metadata.backend = names[0] + "-" + marker;
    storage.cache.backend = names[1] + "-" + marker;
    storage.analytics.backend = names[2] + "-" + marker;
    storage.rate_limit.enabled = true;
    storage.rate_limit.backend = names[3] + "-" + marker;
    storage.metadata.sqlite_path = "/" + marker + "/path.db";
    storage.metadata.postgres_dsn = pg + "u:" + marker + at + "h/db";
    storage.cache.redis_address = rd + ":" + marker + at + "h";
    storage.rate_limit.redis_address = rd + ":" + marker + at + "h";

    ServerConfig server;
    server.tls.certificate_chain_file = "/tls/" + marker + "-cert.pem";
    server.tls.private_key_file = "/tls/" + marker + "-key.pem";
    server.tls.private_key_passphrase = marker + "-phrase";
    server.analytics_client_hash_salt = marker + "-salt";

    return app::make_system_info(storage, server);
}

// The local-operator caller every real-registry command runs as (Admin, so the
// full table/command catalog is visible). Nothing is mutated.
commands::CallerContext admin_caller()
{
    commands::CallerContext caller;
    caller.principal = "local-operator";
    caller.transport = "tui";
    caller.control_set = ControlSet::Admin;
    return caller;
}

// Builds the direct `db.tables` request used to read the authoritative table
// list off the real registry (for the sidebar-parity assertion).
commands::CommandRequest db_tables_request()
{
    commands::CommandRequest request;
    request.v = 1;
    request.id = "probe-db-tables";
    request.command = "db.tables";
    request.args_json = "{}";
    return request;
}

}  // namespace

// Type: unit (real-registry render, links url_shortener_tui + FTXUI).
// Scenario: an InProcessClient is built over the real build_command_runtime
//   registry while a secret-laden system.info snapshot is installed. The
//   Tables, Settings and Dashboard screens are driven to quiescence over a live
//   AsyncCaller and rendered; none of the frames contains the secret marker or
//   any secret-key-shaped backend name. The Tables sidebar equals the db.tables
//   response and the TUI adds no tables of its own.
// Boundaries: server handlers (the positive allowlist) are the control, not the
//   TUI filter - the markers never leave the registry; the sidebar table set is
//   exactly what db.tables returns, in the same order.
// On failure first check: did a handler start echoing a raw config/backend
//   value (system.info allowlist, db.tables/db.rows), or did TablesModel invent
//   or drop a table relative to the db.tables payload?
BOOST_AUTO_TEST_CASE(real_registry_frames_contain_no_secret_markers)
{
    app::set_process_system_info(config_with_secrets());

    const ServerConfig server;  // borrowed by the runtime; must outlive it
    app::LinkBackendHandle backend;  // inactive handle -> in-memory backend
    const composition::CommandRuntime runtime =
        composition::build_command_runtime(server, backend);

    tui::InProcessClient client(*runtime.registry, admin_caller());

    InlinePoster poster;
    tui::AsyncCaller caller(client, poster, 1);
    tui::Router router;
    tui::TuiOptions options;
    tui::SessionInfo session;
    tui::ScreenContext context {caller, session, router, options};

    const std::vector<tui::CommandInfo> catalog = client.commands();

    // Render each screen over the live registry, draining to true quiescence.
    std::string tables_frame;
    {
        std::unique_ptr<tui::IScreen> screen =
            tui::make_tables_screen(context, catalog);
        render_to_text(screen->component(), 100, 30);  // measure the layout
        screen->on_enter();
        settle_until_idle(caller, poster);
        tables_frame = render_to_text(screen->component(), 100, 30);
        screen->on_leave();
    }

    std::string settings_frame;
    {
        std::unique_ptr<tui::IScreen> screen =
            tui::make_settings_screen(context);
        render_to_text(screen->component(), 100, 30);
        screen->on_enter();
        settle_until_idle(caller, poster);
        settings_frame = render_to_text(screen->component(), 100, 30);
        screen->on_leave();
    }

    std::string dashboard_frame;
    {
        std::unique_ptr<tui::IScreen> screen =
            tui::make_dashboard_screen(context, catalog);
        render_to_text(screen->component(), 100, 30);
        screen->on_enter();
        settle_until_idle(caller, poster);
        dashboard_frame = render_to_text(screen->component(), 100, 30);
        screen->on_leave();
    }

    // No frame contains any marker or secret-key-shaped backend name.
    std::vector<std::string> forbidden = secret_shaped_names();
    forbidden.push_back(fake_secret_marker());
    for (const std::string& value : forbidden) {
        BOOST_TEST(tables_frame.find(value) == std::string::npos,
                   "secret leaked into the Tables frame: " << value);
        BOOST_TEST(settings_frame.find(value) == std::string::npos,
                   "secret leaked into the Settings frame: " << value);
        BOOST_TEST(dashboard_frame.find(value) == std::string::npos,
                   "secret leaked into the Dashboard frame: " << value);
    }

    // (c) The Tables sidebar equals the db.tables response: parse the real
    // response independently, confirm TablesModel reproduces exactly that list
    // (no invented tables), and confirm every name is in the rendered sidebar.
    const commands::CommandResponse tables_response =
        client.call(db_tables_request());
    BOOST_REQUIRE(tables_response.ok);

    const std::optional<std::string> raw_tables =
        tui::raw_field(tables_response.result_json, "tables");
    BOOST_REQUIRE(raw_tables.has_value());

    std::vector<std::string> response_names;
    for (const std::string& element : tui::split_array(*raw_tables)) {
        const std::optional<std::string> name =
            tui::get_string(element, "name");
        if (name.has_value()) {
            response_names.push_back(*name);
        }
    }
    BOOST_REQUIRE(!response_names.empty());

    tui::TablesModel model;
    model.apply_tables(tables_response.result_json);
    std::vector<std::string> model_names;
    for (const tui::TableInfo& table : model.tables()) {
        model_names.push_back(table.name);
    }
    BOOST_TEST((model_names == response_names),
               "TablesModel diverged from the db.tables response");

    for (const std::string& name : response_names) {
        BOOST_TEST(tables_frame.find(name) != std::string::npos,
                   "db.tables table missing from the sidebar: " << name);
    }
}

namespace
{

/// Owns the fixtures a mock-driven Tables-screen render needs.
struct MockFixture
{
    MockCommandClient client;
    InlinePoster poster;
    tui::AsyncCaller caller {client, poster, 1};
    tui::Router router;
    tui::TuiOptions options;
    tui::SessionInfo session;
    tui::ScreenContext context {caller, session, router, options};

    /// The link.create catalog entry the insert form needs; read-only here, but
    /// the screen still expects it in its command list.
    static std::vector<tui::CommandInfo> catalog()
    {
        tui::CommandInfo info;
        info.name = "link.create";
        info.summary = "Create a link";
        info.permission = "write";
        info.mutating = true;
        return {info};
    }

    std::unique_ptr<tui::IScreen> tables()
    {
        return tui::make_tables_screen(context, catalog());
    }
};

// A db.tables result whose single writable `links` table declares secret-named
// columns alongside `slug` and the allowed `client_id_hash`. Only a handler bug
// could expose these columns; the TUI's own filter must still refuse them.
std::string tables_with_secret_columns()
{
    return R"({"tables":[)"
           R"({"name":"links","rows":1,"writable":true,"columns":[)"
           R"({"name":"slug","type":"string","sortable":true,"searchable":true},)"
           R"({"name":"password","type":"string","sortable":false,"searchable":false},)"
           R"({"name":"api_token","type":"string","sortable":false,"searchable":false},)"
           R"({"name":"analytics_salt","type":"string","sortable":false,"searchable":false},)"
           R"({"name":"postgres_dsn","type":"string","sortable":false,"searchable":false},)"
           R"({"name":"account_hash","type":"string","sortable":false,"searchable":false},)"
           R"({"name":"client_id_hash","type":"string","sortable":false,"searchable":false})"
           R"(]})"
           R"(]})";
}

// One db.rows element carrying a secret value (marker-embedded, assembled at
// runtime) under every secret-named field, plus a non-secret `slug` and the
// allowed `client_id_hash`.
std::string rows_with_secret_values(const std::string& marker)
{
    return R"({"table":"links","columns":[)"
           R"({"name":"slug","type":"string"},{"name":"password","type":"string"},)"
           R"({"name":"api_token","type":"string"},{"name":"analytics_salt","type":"string"},)"
           R"({"name":"postgres_dsn","type":"string"},{"name":"account_hash","type":"string"},)"
           R"({"name":"client_id_hash","type":"string"}],)"
           R"("rows":[{"slug":"home",)"
           R"("password":")"
        + marker
        + R"(-pw",)"
          R"("api_token":")"
        + marker
        + R"(-tok",)"
          R"("analytics_salt":")"
        + marker
        + R"(-salt",)"
          R"("postgres_dsn":")"
        + marker
        + R"(-dsn",)"
          R"("account_hash":")"
        + marker
        + R"(-hash",)"
          R"("client_id_hash":"idhash1"}],)"
          R"("offset":0,"limit":20,"total":1})";
}

}  // namespace

// Type: unit (component + mock seam, links url_shortener_tui + FTXUI).
// Scenario: a scripted db.tables declares secret-named columns and a db.rows
//   element carries a marker-bearing value under each. The Tables screen
//   renders the body and then the Enter row-JSON view; neither shows any secret
//   field key or value (the marker never appears), while the non-secret `slug`
//   and the documented `client_id_hash` exception are shown and the screen
//   notes a secret-named field was hidden.
// Boundaries: this is the TUI's defense-in-depth filter (T-125), not a "no
//   redaction logic" assertion - TablesModel::redact_secret_fields and the
//   symmetric column filter drop the fields; client_id_hash is the one kept
//   identifier.
// On failure first check: TablesModel::is_secret_column_name /
//   redact_secret_fields / columns_for_selected in tables_model.cpp, or the
//   row-JSON builder in tables_screen.cpp.
BOOST_AUTO_TEST_CASE(tui_filter_also_drops_secret_named_fields)
{
    const std::string marker = fake_secret_marker();

    MockFixture fx;
    fx.client.enqueue(
        "db.tables",
        commands::make_success("tui-1", tables_with_secret_columns()));
    fx.client.enqueue(
        "db.rows",
        commands::make_success("tui-2", rows_with_secret_values(marker)));

    std::unique_ptr<tui::IScreen> screen = fx.tables();
    render_to_text(screen->component(), 100, 30);  // measure the layout
    screen->on_enter();
    settle_until_idle(fx.caller, fx.poster);  // db.tables -> db.rows

    // The table body drops every secret-named column (and so its values); the
    // non-secret columns and the allowed client_id_hash are still shown, and
    // the screen notes a secret-named field was hidden.
    const std::string body = render_to_text(screen->component(), 100, 30);
    BOOST_TEST(body.find(marker) == std::string::npos);
    BOOST_TEST(body.find("password") == std::string::npos);
    BOOST_TEST(body.find("api_token") == std::string::npos);
    BOOST_TEST(body.find("analytics_salt") == std::string::npos);
    BOOST_TEST(body.find("postgres_dsn") == std::string::npos);
    BOOST_TEST(body.find("account_hash") == std::string::npos);
    BOOST_TEST(body.find("slug") != std::string::npos);
    BOOST_TEST(body.find("client_id_hash") != std::string::npos);
    BOOST_TEST(body.find("secret-like name") != std::string::npos);

    // The Enter row-JSON view goes through redact_secret_fields: the secret
    // keys and values are gone, while slug and client_id_hash survive, and the
    // viewer notes the projected-out fields.
    send_events(screen->component(), {ftxui::Event::Tab, ftxui::Event::Return});
    const std::string row_json = render_to_text(screen->component(), 100, 30);
    BOOST_TEST(row_json.find(marker) == std::string::npos);
    BOOST_TEST(row_json.find("password") == std::string::npos);
    BOOST_TEST(row_json.find("api_token") == std::string::npos);
    BOOST_TEST(row_json.find("analytics_salt") == std::string::npos);
    BOOST_TEST(row_json.find("postgres_dsn") == std::string::npos);
    BOOST_TEST(row_json.find("account_hash") == std::string::npos);
    BOOST_TEST(row_json.find("client_id_hash") != std::string::npos);
    BOOST_TEST(row_json.find("idhash1") != std::string::npos);
    BOOST_TEST(row_json.find("secret-named field") != std::string::npos);
}
