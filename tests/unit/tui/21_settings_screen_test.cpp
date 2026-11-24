#define BOOST_TEST_MODULE TuiSettingsScreenTest

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <boost/test/unit_test.hpp>

#include <ftxui/component/event.hpp>

#include <url_shortener/app/system_info.hpp>
#include <url_shortener/commands/envelope.hpp>
#include <url_shortener/commands/error_codes.hpp>
#include <url_shortener/tui/async_caller.hpp>
#include <url_shortener/tui/command_client.hpp>
#include <url_shortener/tui/router.hpp>
#include <url_shortener/tui/screen.hpp>
#include <url_shortener/tui/screens/settings_screen.hpp>
#include <url_shortener/tui/theme.hpp>
#include <url_shortener/tui/tui_options.hpp>

#include "golden.hpp"
#include "hostile_strings.hpp"
#include "inline_poster.hpp"
#include "mock_command_client.hpp"
#include "render_helpers.hpp"

/**
 * @file 21_settings_screen_test.cpp
 * @brief Render + behaviour tests for the Settings screen (D13, subtask
 *        06.0/21).
 *
 * Type: unit (render/snapshot + component events, links url_shortener_tui +
 *       FTXUI).
 * Scenario: the complete, read-only-session, unavailable and mono renders match
 *           their goldens; `r` issues exactly one new system.info call and the
 *           screen's whole request set is exactly {system.info}; T-110 - a
 *           hostile base_domain never reaches the rendered grid.
 * Boundaries: ready vs. unavailable; dark vs. mono (ASCII-only) themes;
 *           read_only yes vs. no header; one vs. two calls after `r`.
 * On failure first check: whether the D13 layout/columns in
 *           settings_screen.cpp changed (regenerate with UPDATE_GOLDEN=1 and
 *           review), or whether a render sink stopped calling
 *           sanitize_for_terminal.
 */

namespace tui = url_shortener::tui;
namespace app = url_shortener::app;
namespace commands = url_shortener::commands;

using tui::testing::assert_matches_golden;
using tui::testing::has_display_hazard;
using tui::testing::hostile_samples;
using tui::testing::InlinePoster;
using tui::testing::MockCommandClient;
using tui::testing::render_to_text;

namespace
{

/// A representative, column-fitting snapshot for the goldens.
app::SystemInfo sample_info()
{
    app::SystemInfo info;
    info.version = "1.0.0";
    info.metadata = "sqlite";
    info.cache = "redis";
    info.analytics = "sqlite";
    info.rate_limit = "redis";
    info.persistent = true;
    info.shortener.base_domain = "http://localhost:8000";
    info.shortener.generated_slug_length = 7;
    info.shortener.default_redirect_type = "temporary";
    info.shortener.default_expiry_seconds = std::nullopt;  // -> "never"
    info.shortener.allow_private_targets = false;
    info.shortener.analytics_enabled = true;
    info.shortener.max_request_body_bytes = 65536;
    info.shortener.max_request_target_length = 2048;
    info.shortener.request_id_max_length = 64;
    return info;
}

commands::CommandResponse ok_response(const app::SystemInfo& info,
                                      bool read_only)
{
    return commands::make_success(
        "tui-1", app::serializeSystemInfoJson(info, read_only));
}

/// Drives a fresh screen: scripts @p response, enters the screen, drains the
/// one completion, and returns the rendered 80x24 grid for @p theme.
std::string render_settings(commands::CommandResponse response,
                            tui::ThemeName theme)
{
    MockCommandClient client;
    client.enqueue("system.info", std::move(response));

    InlinePoster poster;
    tui::AsyncCaller caller(client, poster, 1);
    tui::Router router;
    tui::TuiOptions options;
    options.theme = theme;
    tui::SessionInfo session;
    tui::ScreenContext context {caller, session, router, options};

    std::unique_ptr<tui::IScreen> screen = tui::make_settings_screen(context);
    screen->on_enter();
    poster.wait_for_posted(1);
    poster.run_pending();
    return render_to_text(screen->component(), 80, 24);
}

}  // namespace

// ---------------------------------------------------------------------------
// The complete D13 layout at 80x24 matches its golden.
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(complete_render_matches_golden)
{
    const std::string actual =
        render_settings(ok_response(sample_info(), /*read_only=*/false),
                        tui::ThemeName::dark);
    assert_matches_golden(actual, "d13_settings_80x24.txt");
}

// ---------------------------------------------------------------------------
// A read-only session renders "Read-only session  yes" (a rendering difference,
// not a disabled control).
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(read_only_session_render_matches_golden)
{
    const std::string actual =
        render_settings(ok_response(sample_info(), /*read_only=*/true),
                        tui::ThemeName::dark);
    BOOST_TEST(actual.find("Read-only session") != std::string::npos);
    BOOST_TEST(actual.find("yes") != std::string::npos);
    assert_matches_golden(actual, "d13_settings_readonly_80x24.txt");
}

// ---------------------------------------------------------------------------
// A failing system.info call renders the single unavailable panel with the
// error message and request id, never a blank or crashed screen.
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(unavailable_render_matches_golden)
{
    commands::CommandResponse failure = commands::make_failure(
        "tui-1",
        commands::make_error(commands::CommandErrorCode::internal,
                             "settings temporarily unavailable"));
    const std::string actual =
        render_settings(std::move(failure), tui::ThemeName::dark);

    BOOST_TEST(actual.find("unavailable") != std::string::npos);
    BOOST_TEST(actual.find("settings temporarily unavailable")
               != std::string::npos);
    BOOST_TEST(actual.find("tui-1") != std::string::npos);
    assert_matches_golden(actual, "d13_settings_unavailable_80x24.txt");
}

// ---------------------------------------------------------------------------
// The mono render is pure ASCII (box glyphs folded) and matches its golden.
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(mono_render_is_ascii_and_matches_golden)
{
    const std::string actual =
        render_settings(ok_response(sample_info(), /*read_only=*/false),
                        tui::ThemeName::mono);
    for (unsigned char byte : actual)
    {
        BOOST_TEST(byte <= 0x7Eu,
                   "non-ASCII byte in the mono grid: "
                       << static_cast<int>(byte));
    }
    assert_matches_golden(actual, "d13_settings_mono_80x24.txt");
}

// ---------------------------------------------------------------------------
// `r` issues exactly one new system.info call, and the screen's whole request
// set is exactly {system.info}.
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(refresh_issues_exactly_one_system_info_call)
{
    MockCommandClient client;
    client.enqueue("system.info", ok_response(sample_info(), false));
    client.enqueue("system.info", ok_response(sample_info(), false));

    InlinePoster poster;
    tui::AsyncCaller caller(client, poster, 1);
    tui::Router router;
    tui::TuiOptions options;
    tui::SessionInfo session;
    tui::ScreenContext context {caller, session, router, options};

    std::unique_ptr<tui::IScreen> screen = tui::make_settings_screen(context);

    screen->on_enter();
    poster.wait_for_posted(1);
    poster.run_pending();

    tui::testing::send_events(screen->component(),
                              {ftxui::Event::Character("r")});
    poster.wait_for_posted(2);
    poster.run_pending();

    const std::vector<commands::CommandRequest> requests =
        client.recorded_requests();
    BOOST_REQUIRE_EQUAL(requests.size(), 2u);
    for (const commands::CommandRequest& request : requests)
    {
        BOOST_TEST(request.command == "system.info");
        BOOST_TEST(request.args_json == "{}");
    }
}

// ---------------------------------------------------------------------------
// T-110: a hostile base_domain never reaches the rendered grid - no ESC and no
// display hazard survives any cell, in either theme.
// On failure first check: field_cell stopped calling sanitize_for_terminal, or
// the value width stopped bounding the cell.
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(hostile_server_strings_never_reach_grid)
{
    for (const auto& sample : hostile_samples())
    {
        app::SystemInfo info = sample_info();
        info.shortener.base_domain = sample.raw;
        info.version = sample.raw;
        info.metadata = sample.raw;

        for (tui::ThemeName theme :
             {tui::ThemeName::dark, tui::ThemeName::mono})
        {
            const std::string grid =
                render_settings(ok_response(info, false), theme);
            BOOST_TEST(grid.find('\x1b') == std::string::npos,
                       "ESC reached the grid for sample: " << sample.name);
            for (const std::string& line : tui::testing::split_lines(grid))
            {
                BOOST_TEST(!has_display_hazard(line),
                           "display hazard in a cell for sample: "
                               << sample.name);
            }
        }
    }
}
