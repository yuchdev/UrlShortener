#define BOOST_TEST_MODULE TuiSettingsModelTest

#include <optional>
#include <string>

#include <boost/test/unit_test.hpp>

#include <url_shortener/app/system_info.hpp>
#include <url_shortener/commands/command_descriptor.hpp>
#include <url_shortener/commands/envelope.hpp>
#include <url_shortener/commands/error_codes.hpp>
#include <url_shortener/commands/system_commands.hpp>
#include <url_shortener/tui/settings_model.hpp>

/**
 * @file 21_settings_model_test.cpp
 * @brief Unit tests for the Settings view-model (D13, subtask 06.0/21).
 *
 * Type: unit (pure view-model, links url_shortener_tui_model, no FTXUI).
 * Scenario: `apply` of a full success response populates every SettingsInfo /
 *           ShortenerConfig field exactly; `default_expiry_seconds` is nullopt
 *           for JSON null and a value for a number; a failure response moves the
 *           model to `unavailable` with info reset to default and the error
 *           message/request id captured; `read_only()` reflects the response's
 *           own `read_only`; `refresh_request()` carries no args and names the
 *           catalog's `system.info` command (permission `read`).
 * Boundaries: success vs. failure; null vs. numeric expiry; ready-then-failure
 *           resets info().
 * On failure first check: whether the system.info JSON field names in
 *           settings_model.cpp still match app::serializeSystemInfoJson, or
 *           whether get_u32's null/negative guard changed.
 */

namespace tui = url_shortener::tui;
namespace app = url_shortener::app;
namespace commands = url_shortener::commands;

namespace
{

/// Builds a fully-populated server snapshot (every field distinct and non-zero
/// where possible), serialized exactly as the real `system.info` handler does.
app::SystemInfo full_info()
{
    app::SystemInfo info;
    info.version = "1.2.3";
    info.metadata = "sqlite";
    info.cache = "redis";
    info.analytics = "postgres";
    info.rate_limit = "inmemory";
    info.persistent = true;
    info.shortener.base_domain = "http://localhost:8000";
    info.shortener.generated_slug_length = 7;
    info.shortener.default_redirect_type = "temporary";
    info.shortener.default_expiry_seconds = 3600;
    info.shortener.allow_private_targets = true;
    info.shortener.analytics_enabled = true;
    info.shortener.max_request_body_bytes = 65536;
    info.shortener.max_request_target_length = 2048;
    info.shortener.request_id_max_length = 64;
    return info;
}

commands::CommandResponse ok_response(const app::SystemInfo& info,
                                      bool read_only,
                                      const std::string& id = "tui-1")
{
    return commands::make_success(
        id, app::serializeSystemInfoJson(info, read_only));
}

}  // namespace

// ---------------------------------------------------------------------------
// apply() of a full success response populates every field exactly.
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(apply_success_populates_every_field)
{
    tui::SettingsModel model;
    model.apply(ok_response(full_info(), /*read_only=*/false));

    BOOST_TEST((model.state() == tui::PanelState::ready));
    const tui::SettingsInfo& got = model.info();
    BOOST_TEST(got.version == "1.2.3");
    BOOST_TEST(got.metadata == "sqlite");
    BOOST_TEST(got.cache == "redis");
    BOOST_TEST(got.analytics == "postgres");
    BOOST_TEST(got.rate_limit == "inmemory");
    BOOST_TEST(got.persistent);
    BOOST_TEST(got.shortener.base_domain == "http://localhost:8000");
    BOOST_TEST(got.shortener.generated_slug_length == 7u);
    BOOST_TEST(got.shortener.default_redirect_type == "temporary");
    BOOST_REQUIRE(got.shortener.default_expiry_seconds.has_value());
    BOOST_TEST(*got.shortener.default_expiry_seconds == 3600u);
    BOOST_TEST(got.shortener.allow_private_targets);
    BOOST_TEST(got.shortener.analytics_enabled);
    BOOST_TEST(got.shortener.max_request_body_bytes == 65536u);
    BOOST_TEST(got.shortener.max_request_target_length == 2048u);
    BOOST_TEST(got.shortener.request_id_max_length == 64u);
    BOOST_TEST(!model.read_only());
}

// ---------------------------------------------------------------------------
// default_expiry_seconds: JSON null -> nullopt ("never"); a number -> the value.
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(default_expiry_null_versus_number)
{
    app::SystemInfo never = full_info();
    never.shortener.default_expiry_seconds = std::nullopt;
    tui::SettingsModel model;
    model.apply(ok_response(never, /*read_only=*/false));
    BOOST_TEST(!model.info().shortener.default_expiry_seconds.has_value());

    app::SystemInfo bounded = full_info();
    bounded.shortener.default_expiry_seconds = 90;
    model.apply(ok_response(bounded, /*read_only=*/false));
    BOOST_REQUIRE(model.info().shortener.default_expiry_seconds.has_value());
    BOOST_TEST(*model.info().shortener.default_expiry_seconds == 90u);
}

// ---------------------------------------------------------------------------
// A failure response -> unavailable; info() stays default; the message and
// request id are captured for display.
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(failure_sets_unavailable_and_keeps_info_default)
{
    tui::SettingsModel model;
    // Seed a ready state first to prove the failure resets info() back.
    model.apply(ok_response(full_info(), /*read_only=*/true));
    BOOST_REQUIRE((model.state() == tui::PanelState::ready));

    model.apply(commands::make_failure(
        "tui-9",
        commands::make_error(commands::CommandErrorCode::internal, "boom")));

    BOOST_TEST((model.state() == tui::PanelState::unavailable));
    BOOST_TEST(model.info().version.empty());
    BOOST_TEST(model.info().shortener.base_domain.empty());
    BOOST_TEST(!model.read_only());
    BOOST_TEST(model.error_message() == "boom");
    BOOST_TEST(model.error_request_id() == "tui-9");
}

// ---------------------------------------------------------------------------
// read_only() reflects the response's own read_only field, not a local flag.
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(read_only_reflects_response_field)
{
    tui::SettingsModel model;
    model.apply(ok_response(full_info(), /*read_only=*/true));
    BOOST_TEST(model.read_only());

    model.apply(ok_response(full_info(), /*read_only=*/false));
    BOOST_TEST(!model.read_only());
}

// ---------------------------------------------------------------------------
// refresh_request() names the catalog's system.info command, carries no args,
// and the descriptor's permission is read (asserted against the catalog).
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(refresh_request_is_read_only_system_info)
{
    tui::SettingsModel model;
    const commands::CommandRequest request =
        model.refresh_request([] { return std::string("tui-42"); });

    BOOST_TEST(request.command == "system.info");
    BOOST_TEST(request.id == "tui-42");
    BOOST_TEST(request.args_json == "{}");

    const commands::CommandDescriptor descriptor =
        commands::make_system_info_descriptor(app::SystemInfo {});
    BOOST_TEST(request.command == descriptor.name);
    BOOST_TEST((descriptor.permission == commands::Permission::read));
    BOOST_TEST(!descriptor.mutating);
}
