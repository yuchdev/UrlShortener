#define BOOST_TEST_MODULE TuiJobsScreenTest

#include <chrono>
#include <cstddef>
#include <memory>
#include <string>
#include <vector>

#include <boost/test/unit_test.hpp>
#include <ftxui/component/event.hpp>
#include <url_shortener/commands/envelope.hpp>
#include <url_shortener/core/utils.hpp>
#include <url_shortener/tui/async_caller.hpp>
#include <url_shortener/tui/command_client.hpp>
#include <url_shortener/tui/router.hpp>
#include <url_shortener/tui/screen.hpp>
#include <url_shortener/tui/screens/jobs_screen.hpp>
#include <url_shortener/tui/theme.hpp>
#include <url_shortener/tui/tui_options.hpp>

#include "golden.hpp"
#include "hostile_strings.hpp"
#include "inline_poster.hpp"
#include "mock_command_client.hpp"
#include "render_helpers.hpp"

/**
 * @file 16_jobs_screen_test.cpp
 * @brief Jobs screen tests (D8/D11, 06.0/16): list golden, non-overlapping
 *        polling, cancel confirm, detail envelope, focus-by-id, read-only gate
 *        and T-110 sanitization.
 *
 * Type: unit (component + mock seam, links url_shortener_tui + FTXUI).
 * Scenario: on_enter loads job.list once; a second refresh while one is in
 *        flight is skipped (max concurrency 1); `c` on a running job confirms
 *        then sends one job.cancel, on a succeeded job sends nothing; Enter
 *        opens the full job.get envelope; focus_job lands on a handed-over id;
 *        read-only disables cancel but keeps list/detail; a hostile failure
 * code renders sanitized in the OUTCOME cell and the detail envelope.
 * Boundaries: in-flight vs idle poll; cancellable vs terminal; full vs
 *        read-only session; list vs detail mode.
 * On failure first check: JobsComponent::poll_list/begin_cancel/open_detail in
 *        jobs_screen.cpp and JobsModel selection.
 */

namespace tui = url_shortener::tui;
namespace commands = url_shortener::commands;

using tui::testing::assert_matches_golden;
using tui::testing::has_display_hazard;
using tui::testing::InlinePoster;
using tui::testing::MockCommandClient;
using tui::testing::render_to_text;
using tui::testing::send_events;

namespace
{

tui::CommandInfo job_cancel_info()
{
    tui::CommandInfo info;
    info.name = "job.cancel";
    info.summary = "Cancel a job";
    info.permission = "write";
    return info;
}

std::vector<tui::CommandInfo> catalog()
{
    return {job_cancel_info()};
}

/// A deterministic three-job list (queued / running / succeeded) for the
/// golden.
std::string three_jobs()
{
    return R"({"items":[)"
           R"({"id":"j-1","command":"link.import","state":"queued",)"
           R"("progress":{"done":0,"total":0}},)"
           R"({"id":"j-2","command":"link.import","state":"running",)"
           R"("progress":{"done":62,"total":100},)"
           R"("started_at":"10:00:00"},)"
           R"({"id":"j-3","command":"db.rows","state":"succeeded",)"
           R"("progress":{"done":312,"total":312},)"
           R"("started_at":"09:00:00","finished_at":"09:00:03"})"
           R"(],"total":3})";
}

/// A list whose selected row is a cancellable running job `j-9`.
std::string list_with(const std::string& id, const std::string& state)
{
    return R"({"items":[{"id":")" + id
        + R"(","command":"link.import",)"
          R"("state":")"
        + state
        + R"(","progress":{"done":1,"total":4}}],)"
          R"("total":1})";
}

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

/// Owns the fixtures a Jobs-screen test needs.
struct Fixture
{
    MockCommandClient client;
    InlinePoster poster;
    tui::AsyncCaller caller {client, poster, 1};
    tui::Router router;
    tui::TuiOptions options;
    tui::SessionInfo session;
    tui::ScreenContext context {caller, session, router, options};

    explicit Fixture(bool read_only = false, bool assume_yes = false)
        : options(make_options(read_only, assume_yes))
        , context {caller, session, router, options}
    {
    }

    static tui::TuiOptions make_options(bool read_only, bool assume_yes)
    {
        tui::TuiOptions o;
        o.read_only = read_only;
        o.assume_yes = assume_yes;
        return o;
    }

    std::unique_ptr<tui::IScreen> jobs()
    {
        return tui::make_jobs_screen(context, catalog());
    }

    /// Waits for @p new_posts further completions since the previous drain,
    /// then runs every pending UI task. Counts are relative (one submit == one
    /// post) so a test need not track the cumulative total across steps.
    void drain(std::size_t new_posts)
    {
        m_expected_ += new_posts;
        poster.wait_for_posted(m_expected_);
        poster.run_pending();
    }

    ///< Cumulative number of completions awaited across @ref drain calls.
    std::size_t m_expected_ = 0;
};

}  // namespace

// ---------------------------------------------------------------------------
// The list view matches its D8 golden.
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(list_matches_golden)
{
    Fixture fx;
    fx.client.enqueue("job.list",
                      commands::make_success("tui-1", three_jobs()));

    std::unique_ptr<tui::IScreen> screen = fx.jobs();
    render_to_text(screen->component(), 80, 24);  // measure
    screen->on_enter();
    fx.drain(1);

    const std::string grid = render_to_text(screen->component(), 80, 24);
    assert_matches_golden(grid, "d08_jobs_80x24.txt");
}

// ---------------------------------------------------------------------------
// An empty job list renders its golden (and notes non-persistence).
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(empty_list_matches_golden)
{
    Fixture fx;
    fx.client.enqueue(
        "job.list",
        commands::make_success("tui-1", R"({"items":[],"total":0})"));

    std::unique_ptr<tui::IScreen> screen = fx.jobs();
    render_to_text(screen->component(), 80, 24);
    screen->on_enter();
    fx.drain(1);

    const std::string grid = render_to_text(screen->component(), 80, 24);
    assert_matches_golden(grid, "d08_jobs_empty_80x24.txt");
}

// ---------------------------------------------------------------------------
// The mono theme renders its golden (ASCII borders and gauge glyphs).
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(list_mono_matches_golden)
{
    Fixture fx;
    fx.context.options.theme = tui::ThemeName::mono;  // context holds the copy
    fx.client.enqueue("job.list",
                      commands::make_success("tui-1", three_jobs()));

    std::unique_ptr<tui::IScreen> screen = fx.jobs();
    render_to_text(screen->component(), 80, 24);
    screen->on_enter();
    fx.drain(1);

    const std::string grid = render_to_text(screen->component(), 80, 24);
    assert_matches_golden(grid, "d08_jobs_mono_80x24.txt");
}

// ---------------------------------------------------------------------------
// Polling does not overlap: a refresh while a job.list is in flight is skipped
// (at most one in flight, success criterion 2).
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(polling_does_not_overlap)
{
    Fixture fx;
    fx.client.set_delay(std::chrono::milliseconds(40));  // slow transport
    fx.client.enqueue("job.list",
                      commands::make_success("tui-1", three_jobs()));
    fx.client.enqueue("job.list",
                      commands::make_success("tui-2", three_jobs()));

    std::unique_ptr<tui::IScreen> screen = fx.jobs();
    render_to_text(screen->component(), 80, 24);
    screen->on_enter();  // submits job.list #1 (still running)

    // A manual refresh while #1 is in flight must be dropped, not queued.
    send_events(screen->component(), {ftxui::Event::Character("r")});
    send_events(screen->component(), {ftxui::Event::Character("r")});

    fx.drain(1);
    BOOST_TEST(count_command(fx.client.recorded_requests(), "job.list") == 1u);
}

// ---------------------------------------------------------------------------
// `c` on a running job confirms then sends exactly one job.cancel.
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(cancel_running_job_confirms_and_sends_one)
{
    Fixture fx(/*read_only=*/false, /*assume_yes=*/false);
    fx.client.enqueue("job.list",
                      commands::make_success("tui-1", three_jobs()));
    fx.client.enqueue("job.cancel",
                      commands::make_success("tui-2", R"({"id":"j-2"})"));
    fx.client.enqueue("job.list",
                      commands::make_success("tui-3", three_jobs()));

    std::unique_ptr<tui::IScreen> screen = fx.jobs();
    render_to_text(screen->component(), 80, 24);
    screen->on_enter();
    fx.drain(1);

    // Select the running job (row 1), then cancel.
    send_events(screen->component(), {ftxui::Event::ArrowDown});
    send_events(screen->component(), {ftxui::Event::Character("c")});
    BOOST_TEST(count_command(fx.client.recorded_requests(), "job.cancel")
               == 0u);  // confirm gates it

    send_events(screen->component(), {ftxui::Event::Return});  // confirm
    fx.drain(1);

    const std::vector<commands::CommandRequest> reqs =
        fx.client.recorded_requests();
    BOOST_TEST(count_command(reqs, "job.cancel") == 1u);
    bool found = false;
    for (const commands::CommandRequest& r : reqs) {
        if (r.command == "job.cancel"
            && r.args_json.find("j-2") != std::string::npos)
        {
            found = true;
        }
    }
    BOOST_TEST(found);
}

// ---------------------------------------------------------------------------
// `c` on a succeeded (terminal) job sends nothing (no-op toast).
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(cancel_terminal_job_sends_nothing)
{
    Fixture fx;
    fx.client.enqueue("job.list",
                      commands::make_success("tui-1", three_jobs()));

    std::unique_ptr<tui::IScreen> screen = fx.jobs();
    render_to_text(screen->component(), 80, 24);
    screen->on_enter();
    fx.drain(1);

    // Select the succeeded job (row 2) and press cancel.
    send_events(screen->component(),
                {ftxui::Event::ArrowDown, ftxui::Event::ArrowDown});
    send_events(screen->component(), {ftxui::Event::Character("c")});
    fx.poster.run_pending();  // nothing submitted

    BOOST_TEST(count_command(fx.client.recorded_requests(), "job.cancel")
               == 0u);
    const std::string grid = render_to_text(screen->component(), 80, 24);
    BOOST_TEST(grid.find("already succeeded") != std::string::npos);
}

// ---------------------------------------------------------------------------
// Enter opens the detail view with the full error envelope (criterion 4).
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(enter_shows_detail_envelope_golden)
{
    Fixture fx;
    fx.client.enqueue(
        "job.list",
        commands::make_success("tui-1", list_with("j-9", "failed")));
    fx.client.enqueue(
        "job.get",
        commands::make_success(
            "tui-2",
            R"({"id":"j-9","command":"link.import","state":"failed",)"
            R"("progress":{"done":5,"total":10},)"
            R"("error":{"code":"invalid_url","message":"target was not a URL"},)"
            R"("created_at":"09:00:00","finished_at":"09:00:02"})"));

    std::unique_ptr<tui::IScreen> screen = fx.jobs();
    render_to_text(screen->component(), 80, 24);
    screen->on_enter();
    fx.drain(1);

    send_events(screen->component(), {ftxui::Event::Return});  // open detail
    fx.drain(1);

    const std::string grid = render_to_text(screen->component(), 80, 24);
    BOOST_TEST(grid.find("invalid_url") != std::string::npos);
    BOOST_TEST(grid.find("job j-9") != std::string::npos);
    assert_matches_golden(grid, "d08_job_detail_failed_80x24.txt");
}

// ---------------------------------------------------------------------------
// focus_job (from Import/Export hand-off) lands on the named row; Enter then
// fetches that id's detail.
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(focus_job_selects_handed_over_id)
{
    Fixture fx;
    fx.context.focus_job_id = "j-9";
    fx.client.enqueue(
        "job.list",
        commands::make_success(
            "tui-1",
            R"({"items":[)"
            R"({"id":"j-1","command":"c","state":"queued","progress":{"done":0,"total":0}},)"
            R"({"id":"j-9","command":"c","state":"running","progress":{"done":1,"total":2}})"
            R"(],"total":2})"));
    fx.client.enqueue(
        "job.get",
        commands::make_success("tui-2", list_with("j-9", "running")));

    std::unique_ptr<tui::IScreen> screen = fx.jobs();
    render_to_text(screen->component(), 80, 24);
    screen->on_enter();
    fx.drain(1);
    BOOST_TEST(fx.context.focus_job_id.empty());  // consumed

    send_events(screen->component(),
                {ftxui::Event::Return});  // open j-9 detail
    fx.drain(1);
    const std::vector<commands::CommandRequest> reqs =
        fx.client.recorded_requests();
    bool found = false;
    for (const commands::CommandRequest& r : reqs) {
        if (r.command == "job.get"
            && r.args_json.find("j-9") != std::string::npos)
        {
            found = true;
        }
    }
    BOOST_TEST(found);
}

// ---------------------------------------------------------------------------
// Read-only: cancel is refused (no job.cancel) but list and detail still work.
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(read_only_disables_cancel_but_allows_read)
{
    Fixture fx(/*read_only=*/true);
    fx.client.enqueue("job.list",
                      commands::make_success("tui-1", three_jobs()));
    fx.client.enqueue(
        "job.get",
        commands::make_success("tui-2", list_with("j-2", "running")));

    std::unique_ptr<tui::IScreen> screen = fx.jobs();
    render_to_text(screen->component(), 80, 24);
    screen->on_enter();
    fx.drain(1);  // list loaded (a read command worked)

    // Cancel the running job: run_mutation refuses in a read-only session with
    // no confirm dialog and no submit.
    send_events(screen->component(), {ftxui::Event::ArrowDown});
    send_events(screen->component(), {ftxui::Event::Character("c")});
    fx.poster.run_pending();
    BOOST_TEST(count_command(fx.client.recorded_requests(), "job.cancel")
               == 0u);
    const std::string refused = render_to_text(screen->component(), 80, 24);
    BOOST_TEST(refused.find("not_authorized") != std::string::npos);

    // Detail (a read command) still works: Enter opens it and fetches job.get.
    send_events(screen->component(), {ftxui::Event::Return});
    fx.drain(1);
    BOOST_TEST(count_command(fx.client.recorded_requests(), "job.get") == 1u);
}

// ---------------------------------------------------------------------------
// T-110: a hostile failure code renders sanitized in the OUTCOME cell and in
// the detail envelope.
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(t110_hostile_failure_renders_sanitized)
{
    const std::string hazard =
        tui::testing::esc_clear_screen() + tui::testing::bidi_override_slug();

    for (const std::string& where :
         {std::string("list"), std::string("detail")})
    {
        BOOST_TEST_CONTEXT("where=" << where)
        {
            Fixture fx;
            // Build the failed job JSON with the hostile code embedded.
            const std::string job =
                R"({"id":"j-9","command":"link.import","state":"failed",)"
                R"("progress":{"done":1,"total":2},"error":{"code":)"
                + url_shortener::jsonString(hazard) + R"(,"message":)"
                + url_shortener::jsonString(hazard) + R"(}})";
            fx.client.enqueue(
                "job.list",
                commands::make_success(
                    "tui-1", R"({"items":[)" + job + R"(],"total":1})"));
            fx.client.enqueue("job.get", commands::make_success("tui-2", job));

            std::unique_ptr<tui::IScreen> screen = fx.jobs();
            render_to_text(screen->component(), 140, 24);
            screen->on_enter();
            fx.drain(1);

            std::string grid = render_to_text(screen->component(), 140, 24);
            if (where == "detail") {
                send_events(screen->component(), {ftxui::Event::Return});
                fx.drain(1);
                grid = render_to_text(screen->component(), 140, 24);
            }
            // The grid is multi-line; check each row (newlines are not a
            // display hazard, but the sink check runs per rendered line).
            for (const std::string& line :
                 url_shortener::tui::testing::split_lines(grid))
            {
                BOOST_TEST(!has_display_hazard(line));
            }
        }
    }
}
