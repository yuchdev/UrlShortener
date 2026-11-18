#define BOOST_TEST_MODULE TuiLinkDetailScreenTest

#include <cstddef>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include <boost/test/unit_test.hpp>
#include <ftxui/component/event.hpp>
#include <url_shortener/commands/envelope.hpp>
#include <url_shortener/commands/error_codes.hpp>
#include <url_shortener/core/utils.hpp>
#include <url_shortener/tui/async_caller.hpp>
#include <url_shortener/tui/command_client.hpp>
#include <url_shortener/tui/router.hpp>
#include <url_shortener/tui/screen.hpp>
#include <url_shortener/tui/screens/link_detail_screen.hpp>
#include <url_shortener/tui/theme.hpp>
#include <url_shortener/tui/tui_options.hpp>

#include "golden.hpp"
#include "hostile_strings.hpp"
#include "inline_poster.hpp"
#include "mock_command_client.hpp"
#include "render_helpers.hpp"

/**
 * @file 13_link_detail_screen_test.cpp
 * @brief Render + behaviour tests for the Link detail screen (D4, subtask
 *        06.0/13).
 *
 * Type: unit (render/snapshot + component events, url_shortener_tui + FTXUI).
 * Scenario: entering the detail issues exactly two requests (link.get,
 *           link.stats); the D4 grid + 7-day stats strip match their golden; a
 *           failing link.stats leaves the link pane intact and shows "stats
 *           unavailable: <code>"; a not_found link.get toasts and auto-pops via
 *           on_close; `e` opens the link.update form prefilled; `p` shows the
 *           link.preview JSON; the mono golden is ASCII; a deleted link and a
 *           zero-bucket stats report render; T-110 - hostile target/tags/
 *           metadata never reach the grid.
 * Boundaries: loaded vs failed pane; day vs no-day stats; dark vs mono theme.
 * On failure first check: whether the D4 layout in link_detail_screen.cpp
 *           changed (regenerate with UPDATE_GOLDEN=1 and review), or an action
 *           routed around run_mutation / AsyncCaller.
 */

namespace tui = url_shortener::tui;
namespace commands = url_shortener::commands;

using tui::testing::assert_matches_golden;
using tui::testing::has_display_hazard;
using tui::testing::hostile_samples;
using tui::testing::InlinePoster;
using tui::testing::MockCommandClient;
using tui::testing::render_to_text;
using tui::testing::send_events;
using tui::testing::split_lines;

namespace
{

tui::ArgInfo arg(const std::string& name,
                 tui::ArgKind kind = tui::ArgKind::string,
                 bool required = false)
{
    tui::ArgInfo info;
    info.name = name;
    info.kind = kind;
    info.required = required;
    return info;
}

tui::CommandInfo cmd(const std::string& name,
                     const std::string& permission,
                     bool mutating,
                     std::vector<tui::ArgInfo> args)
{
    tui::CommandInfo info;
    info.name = name;
    info.summary = name + " summary";
    info.permission = permission;
    info.mutating = mutating;
    info.args = std::move(args);
    return info;
}

/// The link.* catalog the detail screen resolves commands from.
std::vector<tui::CommandInfo> catalog()
{
    const tui::ArgInfo slug = arg("slug", tui::ArgKind::string, true);
    return {
        cmd("link.get", "read", false, {slug}),
        cmd("link.stats", "read", false, {slug}),
        cmd("link.preview", "read", false, {slug}),
        cmd("link.update",
            "write",
            true,
            {slug, arg("enabled", tui::ArgKind::boolean)}),
        cmd("link.restore", "write", true, {slug}),
    };
}

/// A full LinkView JSON (serializeLinkViewJson shape).
std::string full_link()
{
    return R"({"id":"l-1","slug":"docs",)"
           R"("url":"https://example.com/docs/getting-started",)"
           R"("short_url":"https://sho.rt/docs",)"
           R"("created_at":"2024-01-01T00:00:00Z",)"
           R"("updated_at":"2024-01-02T00:00:00Z","status":"active",)"
           R"("redirect_type":"permanent","tags":["guide","docs"],)"
           R"("metadata":{"team":"docs"},)"
           R"("campaign":{"name":"autumn","source":"twitter","medium":"social"}})";
}

/// A LinkView JSON with status deleted.
std::string deleted_link()
{
    return R"({"id":"l-9","slug":"old-blog",)"
           R"("url":"https://example.com/blog/2019/hello",)"
           R"("short_url":"https://sho.rt/old-blog",)"
           R"("created_at":"2019-01-01T00:00:00Z",)"
           R"("updated_at":"2019-02-01T00:00:00Z","status":"deleted",)"
           R"("redirect_type":"permanent","tags":[],"metadata":{},)"
           R"("campaign":null})";
}

/// A 7-day range report with one bucket per weekday (Thu..Wed).
std::string stats_week()
{
    return R"({"slug":"docs","total_attempts":84210,)"
           R"("successful_redirects":83720,"attempts_by_status_code":{},)"
           R"("attempts_by_domain":{},"time_buckets":[)"
           R"({"bucket_start":0,"count":120},)"
           R"({"bucket_start":86400,"count":340},)"
           R"({"bucket_start":172800,"count":80},)"
           R"({"bucket_start":259200,"count":500},)"
           R"({"bucket_start":345600,"count":260},)"
           R"({"bucket_start":432000,"count":410},)"
           R"({"bucket_start":518400,"count":150}],)"
           R"("from":0,"to":604800,"bucket":"day"})";
}

/// A range report with no day buckets.
std::string stats_empty()
{
    return R"({"slug":"docs","total_attempts":0,"successful_redirects":0,)"
           R"("attempts_by_status_code":{},"attempts_by_domain":{},)"
           R"("time_buckets":[],"from":0,"to":604800,"bucket":"day"})";
}

commands::CommandResponse ok(const std::string& result)
{
    return commands::make_success("tui-x", result);
}

commands::CommandResponse fail(commands::CommandErrorCode code,
                               const std::string& message)
{
    return commands::make_failure("tui-x", commands::make_error(code, message));
}

/// Owns the fixtures a detail-screen test needs.
struct Fixture
{
    MockCommandClient client;
    InlinePoster poster;
    tui::AsyncCaller caller {client, poster, 1};
    tui::Router router;
    tui::TuiOptions options;
    tui::SessionInfo session;
    tui::ScreenContext context {caller, session, router, options};
    bool closed = false;

    explicit Fixture(tui::ThemeName theme = tui::ThemeName::dark)
        : options(make_options(theme))
    {
        context.pending_slug = "docs";
    }

    static tui::TuiOptions make_options(tui::ThemeName theme)
    {
        tui::TuiOptions o;
        o.theme = theme;
        return o;
    }

    std::unique_ptr<tui::IScreen> screen()
    {
        return tui::make_link_detail_screen(
            context, catalog(), [this] { closed = true; });
    }

    /// Waits for @p total cumulative posts, then drains the queued completions.
    void settle(std::size_t total)
    {
        poster.wait_for_posted(total);
        poster.run_pending();
    }
};

// The measured-width layout needs one frame to learn its box; the goldens use
// the second (stable) frame.
std::string render_twice(const ftxui::Component& component, int w, int h)
{
    render_to_text(component, w, h);
    return render_to_text(component, w, h);
}

}  // namespace

// ---------------------------------------------------------------------------
// Entering the detail issues exactly two requests: link.get and link.stats.
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(enter_issues_get_and_stats)
{
    Fixture fx;
    fx.client.enqueue("link.get", ok(full_link()));
    fx.client.enqueue("link.stats", ok(stats_week()));

    std::unique_ptr<tui::IScreen> screen = fx.screen();
    screen->on_enter();
    fx.settle(2);

    const std::vector<commands::CommandRequest> requests =
        fx.client.recorded_requests();
    BOOST_REQUIRE_EQUAL(requests.size(), 2u);
    bool got_get = false;
    bool got_stats = false;
    for (const commands::CommandRequest& request : requests) {
        got_get = got_get || request.command == "link.get";
        got_stats = got_stats || request.command == "link.stats";
    }
    BOOST_TEST(got_get);
    BOOST_TEST(got_stats);
}

// ---------------------------------------------------------------------------
// The D4 grid + stats strip match their golden.
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(d4_detail_matches_golden)
{
    Fixture fx;
    fx.client.enqueue("link.get", ok(full_link()));
    fx.client.enqueue("link.stats", ok(stats_week()));

    std::unique_ptr<tui::IScreen> screen = fx.screen();
    screen->on_enter();
    fx.settle(2);

    const std::string grid = render_twice(screen->component(), 80, 24);
    BOOST_TEST(grid.find("[e]dit [s]tats") != std::string::npos);
    BOOST_TEST(grid.find("https://example.com/docs/getting-started")
               != std::string::npos);
    BOOST_TEST(grid.find("attempts 84,210") != std::string::npos);
    BOOST_TEST(grid.find("Mon") != std::string::npos);
    assert_matches_golden(grid, "d04_detail_80x24.txt");
}

// ---------------------------------------------------------------------------
// A failing link.stats leaves the link pane intact and shows the stats error.
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(stats_error_keeps_link_pane)
{
    Fixture fx;
    fx.client.enqueue("link.get", ok(full_link()));
    fx.client.enqueue("link.stats",
                      fail(commands::CommandErrorCode::internal, "backend"));

    std::unique_ptr<tui::IScreen> screen = fx.screen();
    screen->on_enter();
    fx.settle(2);

    const std::string grid = render_twice(screen->component(), 80, 24);
    BOOST_TEST(grid.find("https://example.com/docs/getting-started")
               != std::string::npos);  // link pane intact
    BOOST_TEST(grid.find("stats unavailable: internal") != std::string::npos);
    assert_matches_golden(grid, "d04_detail_stats_error_80x24.txt");
}

// ---------------------------------------------------------------------------
// A not_found link.get toasts and auto-pops via on_close.
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(not_found_auto_pops)
{
    Fixture fx;
    fx.client.enqueue("link.get",
                      fail(commands::CommandErrorCode::not_found, "gone"));
    fx.client.enqueue("link.stats", ok(stats_empty()));

    std::unique_ptr<tui::IScreen> screen = fx.screen();
    screen->on_enter();
    fx.settle(2);

    BOOST_TEST(fx.closed);  // on_close fired
}

// ---------------------------------------------------------------------------
// `e` opens the link.update form prefilled from the fetched JSON.
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(e_opens_prefilled_update_form)
{
    Fixture fx;
    fx.client.enqueue("link.get", ok(full_link()));
    fx.client.enqueue("link.stats", ok(stats_week()));

    std::unique_ptr<tui::IScreen> screen = fx.screen();
    screen->on_enter();
    fx.settle(2);
    render_twice(screen->component(), 80, 24);

    send_events(screen->component(), {ftxui::Event::Character("e")});
    const std::string grid = render_to_text(screen->component(), 80, 24);
    BOOST_TEST(grid.find("(command: link.update") != std::string::npos);
    BOOST_TEST(grid.find("docs") != std::string::npos);  // prefilled slug
}

// ---------------------------------------------------------------------------
// `p` shows the link.preview JSON in the inline viewer.
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(p_shows_preview_json)
{
    Fixture fx;
    fx.client.enqueue("link.get", ok(full_link()));
    fx.client.enqueue("link.stats", ok(stats_week()));
    fx.client.enqueue("link.preview", ok(R"({"body":"PREVIEW-BODY"})"));

    std::unique_ptr<tui::IScreen> screen = fx.screen();
    screen->on_enter();
    fx.settle(2);
    render_twice(screen->component(), 80, 24);

    send_events(screen->component(), {ftxui::Event::Character("p")});
    fx.settle(3);

    const std::string grid = render_twice(screen->component(), 80, 24);
    BOOST_TEST(grid.find("PREVIEW-BODY") != std::string::npos);
}

// ---------------------------------------------------------------------------
// A deleted link renders with its deleted status.
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(deleted_link_golden)
{
    Fixture fx;
    fx.context.pending_slug = "old-blog";
    fx.client.enqueue("link.get", ok(deleted_link()));
    fx.client.enqueue("link.stats", ok(stats_empty()));

    std::unique_ptr<tui::IScreen> screen = fx.screen();
    screen->on_enter();
    fx.settle(2);

    const std::string grid = render_twice(screen->component(), 80, 24);
    BOOST_TEST(grid.find("deleted") != std::string::npos);
    assert_matches_golden(grid, "d04_detail_deleted_80x24.txt");
}

// ---------------------------------------------------------------------------
// A zero-bucket stats report renders the header with no day bars.
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(no_stats_golden)
{
    Fixture fx;
    fx.client.enqueue("link.get", ok(full_link()));
    fx.client.enqueue("link.stats", ok(stats_empty()));

    std::unique_ptr<tui::IScreen> screen = fx.screen();
    screen->on_enter();
    fx.settle(2);

    const std::string grid = render_twice(screen->component(), 80, 24);
    assert_matches_golden(grid, "d04_detail_no_stats_80x24.txt");
}

// ---------------------------------------------------------------------------
// The mono theme renders the detail as ASCII.
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(mono_detail_golden)
{
    Fixture fx(tui::ThemeName::mono);
    fx.client.enqueue("link.get", ok(full_link()));
    fx.client.enqueue("link.stats", ok(stats_week()));

    std::unique_ptr<tui::IScreen> screen = fx.screen();
    screen->on_enter();
    fx.settle(2);

    const std::string grid = render_twice(screen->component(), 80, 24);
    BOOST_TEST(grid.find('\x1b') == std::string::npos);
    for (const std::string& line : split_lines(grid)) {
        BOOST_TEST(!has_display_hazard(line));
    }
    assert_matches_golden(grid, "d04_detail_mono_80x24.txt");
}

// ---------------------------------------------------------------------------
// T-110: hostile target / tags / metadata never reach the rendered grid.
// On failure first check: the detail rows sanitize before fit (fit()).
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(hostile_fields_never_reach_grid)
{
    for (const auto& sample : hostile_samples()) {
        Fixture fx;
        const std::string hostile = url_shortener::jsonString(sample.raw);
        const std::string link = "{\"id\":\"l-1\",\"slug\":\"docs\",\"url\":"
            + hostile + ",\"short_url\":\"https://s/x\",\"created_at\":\"t\","
                        "\"updated_at\":\"t\",\"status\":\"active\","
                        "\"redirect_type\":\"permanent\",\"tags\":["
            + hostile + "],\"metadata\":{\"k\":" + hostile
            + "},\"campaign\":null}";
        fx.client.enqueue("link.get", ok(link));
        fx.client.enqueue("link.stats", ok(stats_empty()));

        std::unique_ptr<tui::IScreen> screen = fx.screen();
        screen->on_enter();
        fx.settle(2);
        const std::string grid = render_twice(screen->component(), 80, 24);

        BOOST_TEST(grid.find('\x1b') == std::string::npos,
                   "ESC reached the grid for sample: " << sample.name);
        for (const std::string& line : split_lines(grid)) {
            BOOST_TEST(!has_display_hazard(line),
                       "display hazard in a cell for sample: " << sample.name);
        }
    }
}
