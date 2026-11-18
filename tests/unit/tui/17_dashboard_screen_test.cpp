#define BOOST_TEST_MODULE TuiDashboardScreenTest

#include <chrono>
#include <cstddef>
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
#include <url_shortener/tui/screens/dashboard_screen.hpp>
#include <url_shortener/tui/theme.hpp>
#include <url_shortener/tui/tui_options.hpp>

#include "golden.hpp"
#include "hostile_strings.hpp"
#include "inline_poster.hpp"
#include "mock_command_client.hpp"
#include "render_helpers.hpp"

/**
 * @file 17_dashboard_screen_test.cpp
 * @brief Render + behaviour tests for the Dashboard screen (D2, subtask
 *        06.0/17).
 *
 * Type: unit (render/snapshot + component events, links url_shortener_tui +
 *       FTXUI).
 * Scenario: the complete D2, a partial-failure, a memory-backend, a
 *        stats.summary and a mono render match their goldens; `r` issues
 * exactly one new batch; leaving cancels in-flight work and stops the 5 s
 * timer; every issued command is a read command; a hostile slug never reaches
 *        the grid (T-110).
 * Boundaries: ready vs. unavailable panels; sampled vs. stats.summary; dark vs.
 *        mono; persistent vs. memory backend.
 * On failure first check: whether the D2 layout in dashboard_screen.cpp changed
 *        (regenerate with UPDATE_GOLDEN=1 and review), or a render sink stopped
 *        calling sanitize_for_terminal.
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

namespace
{

/// Number of requests the dashboard issues per refresh batch.
constexpr std::size_t k_batch = 9;

/// A one-item `link.list` page whose `total` is @p total.
std::string counts_page(unsigned long long total)
{
    return R"({"items":[],"total":)" + std::to_string(total) + "}";
}

/// The five newest-updated rows for the Recent changes panel.
std::string recent_page()
{
    return R"({"items":[)"
           R"({"slug":"docs2","status":"active","updated_at":"2026-11-19T10:42:00Z"},)"
           R"({"slug":"promo","status":"disabled","updated_at":"2026-11-19T10:40:00Z"},)"
           R"({"slug":"docs","status":"active","updated_at":"2026-11-19T10:31:00Z"})"
           R"(],"total":3})";
}

/// The sample page used for Top slugs / 24h redirects.
std::string sample_page()
{
    return R"({"items":[)"
           R"({"slug":"docs","status":"active","updated_at":"2026-11-19T10:42:00Z","stats":{"redirects_24h":12044}},)"
           R"({"slug":"launch","status":"active","updated_at":"2026-11-19T10:40:00Z","stats":{"redirects_24h":8730}},)"
           R"({"slug":"promo","status":"active","updated_at":"2026-11-19T10:31:00Z","stats":{"redirects_24h":4112}})"
           R"(],"total":3})";
}

/// One running import job for the jobs line.
std::string running_jobs()
{
    return R"({"items":[{"id":"j-1","command":"link.import","state":"running",)"
           R"("progress":{"done":62,"total":100}}],"total":1})";
}

/// A minimal `system.info` result carrying the fields the dashboard reads.
std::string system_info(bool persistent)
{
    return std::string(R"({"version":"1.0.0","backend":{"metadata":"sqlite",)")
        + R"("cache":"redis","analytics":"sqlite","rate_limit":"redis"},)"
        + R"("persistent":)" + (persistent ? "true" : "false") + "}";
}

/// A `db.tables` result with the click_events row count.
std::string db_tables()
{
    return R"({"tables":[)"
           R"({"name":"links","rows":1204,"writable":true,"columns":[]},)"
           R"({"name":"click_events","rows":48311,"writable":false,"columns":[]})"
           R"(]})";
}

/// Scripts a full healthy batch (in submission order) onto @p client.
void script_healthy(MockCommandClient& client, bool persistent)
{
    client.enqueue("link.list", commands::make_success("a", counts_page(1204)));
    client.enqueue("link.list", commands::make_success("b", counts_page(1180)));
    client.enqueue("link.list", commands::make_success("c", counts_page(9)));
    client.enqueue("link.list", commands::make_success("d", counts_page(15)));
    client.enqueue("link.list", commands::make_success("e", recent_page()));
    client.enqueue("link.list", commands::make_success("f", sample_page()));
    client.enqueue("job.list", commands::make_success("g", running_jobs()));
    client.enqueue("system.info",
                   commands::make_success("h", system_info(persistent)));
    client.enqueue("db.tables", commands::make_success("i", db_tables()));
}

/// Owns the fixtures a dashboard-screen test needs.
struct Fixture
{
    MockCommandClient client;
    InlinePoster poster;
    tui::AsyncCaller caller {client, poster, 1};  // serial: call order is fixed
    tui::Router router;
    tui::TuiOptions options;
    tui::SessionInfo session;
    tui::ScreenContext context {caller, session, router, options};

    std::unique_ptr<tui::IScreen> dashboard(
        std::vector<tui::CommandInfo> cat = {})
    {
        return tui::make_dashboard_screen(context, std::move(cat));
    }

    void drain(std::size_t new_posts)
    {
        m_expected_ += new_posts;
        poster.wait_for_posted(m_expected_);
        poster.run_pending();
    }

    std::size_t m_expected_ = 0;
};

std::size_t count_command(const std::vector<commands::CommandRequest>& reqs,
                          const std::string& name)
{
    std::size_t n = 0;
    for (const commands::CommandRequest& r : reqs) {
        if (r.command == name) {
            ++n;
        }
    }
    return n;
}

}  // namespace

// ---------------------------------------------------------------------------
// The complete D2 layout at 80x24 matches its golden.
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(complete_render_matches_golden)
{
    Fixture fx;
    script_healthy(fx.client, /*persistent=*/true);

    std::unique_ptr<tui::IScreen> screen = fx.dashboard();
    render_to_text(screen->component(), 80, 24);  // measure the box
    screen->on_enter();
    fx.drain(k_batch);

    const std::string grid = render_to_text(screen->component(), 80, 24);
    BOOST_TEST(grid.find("Links") != std::string::npos);
    BOOST_TEST(grid.find("Redirects (24h)") != std::string::npos);
    BOOST_TEST(grid.find("Top slugs") != std::string::npos);
    BOOST_TEST(grid.find("Recent changes") != std::string::npos);
    BOOST_TEST(grid.find("Jobs:") != std::string::npos);
    BOOST_TEST(grid.find("Backend:") != std::string::npos);
    assert_matches_golden(grid, "d02_dashboard_80x24.txt");
}

// ---------------------------------------------------------------------------
// One failing request (system.info) marks only its panel n/a; the others stay
// populated (success criterion 2).
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(partial_failure_render_matches_golden)
{
    Fixture fx;
    fx.client.enqueue("link.list",
                      commands::make_success("a", counts_page(1204)));
    fx.client.enqueue("link.list",
                      commands::make_success("b", counts_page(1180)));
    fx.client.enqueue("link.list", commands::make_success("c", counts_page(9)));
    fx.client.enqueue("link.list",
                      commands::make_success("d", counts_page(15)));
    fx.client.enqueue("link.list", commands::make_success("e", recent_page()));
    fx.client.enqueue("link.list", commands::make_success("f", sample_page()));
    fx.client.enqueue("job.list", commands::make_success("g", running_jobs()));
    fx.client.enqueue(
        "system.info",
        commands::make_failure(
            "h",
            commands::make_error(commands::CommandErrorCode::internal,
                                 "backend offline")));
    fx.client.enqueue("db.tables", commands::make_success("i", db_tables()));

    std::unique_ptr<tui::IScreen> screen = fx.dashboard();
    render_to_text(screen->component(), 80, 24);
    screen->on_enter();
    fx.drain(k_batch);

    const std::string grid = render_to_text(screen->component(), 80, 24);
    BOOST_TEST(grid.find("Backend: n/a") != std::string::npos);
    BOOST_TEST(grid.find("1,204") != std::string::npos);  // counts still shown
    assert_matches_golden(grid, "d02_dashboard_partial_failure_80x24.txt");
}

// ---------------------------------------------------------------------------
// A non-persistent (memory) backend adds the "(this session)" counts suffix.
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(memory_backend_render_matches_golden)
{
    Fixture fx;
    script_healthy(fx.client, /*persistent=*/false);

    std::unique_ptr<tui::IScreen> screen = fx.dashboard();
    render_to_text(screen->component(), 80, 24);
    screen->on_enter();
    fx.drain(k_batch);

    const std::string grid = render_to_text(screen->component(), 80, 24);
    BOOST_TEST(grid.find("(this session)") != std::string::npos);
    assert_matches_golden(grid, "d02_dashboard_memory_80x24.txt");
}

// ---------------------------------------------------------------------------
// With a stats.summary command present the sample labels disappear and the
// exact figures render (success criterion 4).
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(stats_summary_render_matches_golden)
{
    Fixture fx;
    // Counts + recent are still link.list; the redirects panel is
    // stats.summary.
    fx.client.enqueue("link.list",
                      commands::make_success("a", counts_page(1204)));
    fx.client.enqueue("link.list",
                      commands::make_success("b", counts_page(1180)));
    fx.client.enqueue("link.list", commands::make_success("c", counts_page(9)));
    fx.client.enqueue("link.list",
                      commands::make_success("d", counts_page(15)));
    fx.client.enqueue("link.list", commands::make_success("e", recent_page()));
    fx.client.enqueue(
        "stats.summary",
        commands::make_success(
            "f",
            R"({"redirects_24h":48311,"success_pct":99.2,"p50_ms":0.4,)"
            R"("top_slugs":[{"slug":"docs","count":12044},)"
            R"({"slug":"launch","count":8730},{"slug":"promo","count":4112}]})"));
    fx.client.enqueue("job.list", commands::make_success("g", running_jobs()));
    fx.client.enqueue("system.info",
                      commands::make_success("h", system_info(true)));
    fx.client.enqueue("db.tables", commands::make_success("i", db_tables()));

    tui::CommandInfo stats;
    stats.name = "stats.summary";
    stats.permission = "read";

    std::unique_ptr<tui::IScreen> screen = fx.dashboard({stats});
    render_to_text(screen->component(), 80, 24);
    screen->on_enter();
    fx.drain(k_batch);

    const std::string grid = render_to_text(screen->component(), 80, 24);
    BOOST_TEST(grid.find("(sample)") == std::string::npos);  // no sample label
    BOOST_TEST(grid.find("48,311") != std::string::npos);
    BOOST_TEST(grid.find("99.2%") != std::string::npos);
    assert_matches_golden(grid, "d02_dashboard_stats_summary_80x24.txt");
}

// ---------------------------------------------------------------------------
// The mono render is pure ASCII (box glyphs and bars folded) and matches its
// golden.
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(mono_render_is_ascii_and_matches_golden)
{
    Fixture fx;
    fx.context.options.theme = tui::ThemeName::mono;
    script_healthy(fx.client, /*persistent=*/true);

    std::unique_ptr<tui::IScreen> screen = fx.dashboard();
    render_to_text(screen->component(), 80, 24);
    screen->on_enter();
    fx.drain(k_batch);

    const std::string grid = render_to_text(screen->component(), 80, 24);
    for (unsigned char byte : grid) {
        BOOST_TEST(
            byte <= 0x7Eu,
            "non-ASCII byte in the mono grid: " << static_cast<int>(byte));
    }
    assert_matches_golden(grid, "d02_dashboard_mono_80x24.txt");
}

// ---------------------------------------------------------------------------
// `r` issues exactly one new batch, and every issued command is a read command.
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(refresh_issues_exactly_one_batch_of_read_commands)
{
    Fixture fx;
    script_healthy(fx.client, /*persistent=*/true);
    script_healthy(fx.client, /*persistent=*/true);  // for the second batch

    std::unique_ptr<tui::IScreen> screen = fx.dashboard();
    render_to_text(screen->component(), 80, 24);
    screen->on_enter();
    fx.drain(k_batch);

    send_events(screen->component(), {ftxui::Event::Character("r")});
    fx.drain(k_batch);

    const std::vector<commands::CommandRequest> reqs =
        fx.client.recorded_requests();
    BOOST_REQUIRE_EQUAL(reqs.size(), 2u * k_batch);
    // Only read commands are ever issued (success criterion 3).
    BOOST_TEST(count_command(reqs, "link.list") == 12u);  // 6 per batch
    BOOST_TEST(count_command(reqs, "job.list") == 2u);
    BOOST_TEST(count_command(reqs, "system.info") == 2u);
    BOOST_TEST(count_command(reqs, "db.tables") == 2u);
    for (const commands::CommandRequest& r : reqs) {
        const bool allowed = r.command == "link.list" || r.command == "job.list"
            || r.command == "system.info" || r.command == "db.tables";
        BOOST_TEST(allowed, "unexpected command issued: " << r.command);
    }
}

// ---------------------------------------------------------------------------
// Leaving cancels in-flight work and stops the 5 s timer: a batch in flight at
// leave never reaches the model (panels stay loading) and no new batch starts.
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(leaving_cancels_in_flight_and_stops_timer)
{
    Fixture fx;
    fx.client.set_delay(std::chrono::milliseconds(30));  // slow transport
    script_healthy(fx.client, /*persistent=*/true);

    std::unique_ptr<tui::IScreen> screen = fx.dashboard();
    render_to_text(screen->component(), 80, 24);
    screen->on_enter();  // submits the batch (still in flight)
    screen->on_leave();  // cancels every outstanding request

    // Let the suppressed completions post and drain; none must reach the model.
    fx.poster.wait_for_posted(k_batch);
    fx.poster.run_pending();

    const std::string grid = render_to_text(screen->component(), 80, 24);
    BOOST_TEST(grid.find("loading") != std::string::npos);  // still loading
    BOOST_TEST(grid.find("1,204") == std::string::npos);  // no data applied
    // No further batch was issued after leaving (the timer is stopped).
    BOOST_TEST(count_command(fx.client.recorded_requests(), "link.list") == 6u);
}

// ---------------------------------------------------------------------------
// T-110: a hostile slug (in the sample / recent rows) never reaches the grid -
// no ESC and no display hazard survives any cell, in either theme.
// On failure first check: the row builders stopped calling
// sanitize_for_terminal, or a value width stopped bounding the cell.
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(hostile_server_strings_never_reach_grid)
{
    for (const auto& sample : hostile_samples()) {
        for (tui::ThemeName theme :
             {tui::ThemeName::dark, tui::ThemeName::mono})
        {
            Fixture fx;
            fx.context.options.theme = theme;
            fx.client.enqueue("link.list",
                              commands::make_success("a", counts_page(1)));
            fx.client.enqueue("link.list",
                              commands::make_success("b", counts_page(1)));
            fx.client.enqueue("link.list",
                              commands::make_success("c", counts_page(0)));
            fx.client.enqueue("link.list",
                              commands::make_success("d", counts_page(0)));
            // Recent + sample carry the hostile slug.
            const std::string hostile_slug =
                url_shortener::jsonString(sample.raw);
            fx.client.enqueue(
                "link.list",
                commands::make_success(
                    "e",
                    R"({"items":[{"slug":)" + hostile_slug
                        + R"(,"status":"active","updated_at":"2026-11-19T10:42:00Z"}],"total":1})"));
            fx.client.enqueue(
                "link.list",
                commands::make_success(
                    "f",
                    R"({"items":[{"slug":)" + hostile_slug
                        + R"(,"status":"active","updated_at":"2026-11-19T10:42:00Z","stats":{"redirects_24h":10}}],"total":1})"));
            fx.client.enqueue("job.list",
                              commands::make_success("g", running_jobs()));
            fx.client.enqueue("system.info",
                              commands::make_success("h", system_info(true)));
            fx.client.enqueue("db.tables",
                              commands::make_success("i", db_tables()));

            std::unique_ptr<tui::IScreen> screen = fx.dashboard();
            render_to_text(screen->component(), 80, 24);
            screen->on_enter();
            fx.drain(k_batch);

            const std::string grid =
                render_to_text(screen->component(), 80, 24);
            BOOST_TEST(grid.find('\x1b') == std::string::npos,
                       "ESC reached the grid for sample: " << sample.name);
            for (const std::string& line : tui::testing::split_lines(grid)) {
                BOOST_TEST(
                    !has_display_hazard(line),
                    "display hazard in a cell for sample: " << sample.name);
            }
        }
    }
}
