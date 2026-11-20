#define BOOST_TEST_MODULE TuiNonblockingUiTest

#include <atomic>
#include <chrono>
#include <cstddef>
#include <future>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include <boost/test/unit_test.hpp>
#include <ftxui/component/event.hpp>
#include <url_shortener/commands/envelope.hpp>
#include <url_shortener/commands/error_codes.hpp>
#include <url_shortener/tui/async_caller.hpp>
#include <url_shortener/tui/command_client.hpp>
#include <url_shortener/tui/router.hpp>
#include <url_shortener/tui/screen.hpp>
#include <url_shortener/tui/screens/jobs_screen.hpp>
#include <url_shortener/tui/screens/links_screen.hpp>
#include <url_shortener/tui/theme.hpp>
#include <url_shortener/tui/tui_options.hpp>

#include "inline_poster.hpp"
#include "mock_command_client.hpp"
#include "render_helpers.hpp"

/**
 * @file 33_nonblocking_ui_test.cpp
 * @brief ADR 0003 rule 3 (the UI thread never blocks on a command), proven with
 *        a real @ref AsyncCaller worker thread and @ref MockCommandClient
 *        latency (07.0/27, gap `tui__33`).
 *
 * Type: unit (real tab screens over a real @ref AsyncCaller worker thread plus
 * the headless @ref InlinePoster; links url_shortener_tui + FTXUI). These are
 * the only `tui__3x` tests that start a real background thread.
 *
 * Scenario: a screen submits a command, a worker runs it off the UI thread, and
 * the completion is delivered only when the test drains the poster. Because the
 * in-flight count and the model are both advanced on the UI thread inside
 * `run_pending` - never on the worker - the proofs synchronize on posted
 * completions (@ref InlinePoster::wait_for_posted) and
 * @ref AsyncCaller::in_flight, never on a wall clock or a sleep.
 * @ref MockCommandClient::set_delay only models a slow transport (it keeps a
 * worker genuinely mid-call during a render); no assertion depends on its
 * duration. A slow command therefore cannot freeze the render/key path, input
 * is accepted while a call is pending, a drained completion applies exactly
 * once, a stale (cancelled) completion is dropped in favour of the latest,
 * teardown with a call in flight neither deadlocks nor runs a handler against a
 * destroyed screen (T-120), and a throwing client is contained as an internal
 * error with no crash.
 *
 * Boundaries: in flight (submitted, not drained) vs delivered (drained); a live
 * token vs a cancelled one; a client that returns vs one that throws.
 *
 * On failure first check: a screen handler that calls `ICommandClient::call`
 * (or otherwise blocks) on the UI thread instead of going through
 * @ref AsyncCaller::submit; the epoch/cancel stale-drop in a screen or in
 * async_caller.cpp; or the `m_alive` teardown guard (T-120).
 */

namespace tui = url_shortener::tui;
namespace commands = url_shortener::commands;

using tui::testing::InlinePoster;
using tui::testing::MockCommandClient;
using tui::testing::render_to_text;
using tui::testing::send_events;

namespace
{

/// A short latency that keeps a worker genuinely mid-call while the UI thread
/// renders; no assertion depends on its value (it models a slow transport).
constexpr std::chrono::milliseconds k_latency {50};

tui::CommandInfo cmd(std::string name, std::string permission, bool mutating)
{
    tui::CommandInfo info;
    info.name = std::move(name);
    info.summary = info.name + " summary";
    info.permission = std::move(permission);
    info.mutating = mutating;
    return info;
}

/// The `link.*` catalog the Links screen resolves commands from.
std::vector<tui::CommandInfo> links_catalog()
{
    return {
        cmd("link.list", "read", false),
        cmd("link.create", "write", true),
        cmd("link.update", "write", true),
        cmd("link.enable", "write", true),
        cmd("link.disable", "write", true),
        cmd("link.delete", "write", true),
        cmd("link.restore", "write", true),
        cmd("link.preview", "read", false),
    };
}

/// The `job.*` catalog the Jobs screen resolves commands from.
std::vector<tui::CommandInfo> jobs_catalog()
{
    return {cmd("job.list", "read", false),
            cmd("job.get", "read", false),
            cmd("job.cancel", "write", true)};
}

commands::CommandResponse ok(const std::string& result)
{
    return commands::make_success("tui-x", result);
}

/// A one-row `link.list` page whose single row renders as `docs`.
std::string one_link_page()
{
    return R"({"items":[)"
           R"({"slug":"docs","url":"https://example.com/docs",)"
           R"("status":"active","stats":{"total_redirects":12}})"
           R"(],"offset":0,"limit":20,"total":1})";
}

/// A one-row `job.list` page.
std::string one_job_page()
{
    return R"({"items":[)"
           R"({"id":"j-1","command":"link.import","state":"running",)"
           R"("progress":{"done":1,"total":2}})"
           R"(],"total":1})";
}

commands::CommandRequest make_request(std::string command, std::string id)
{
    commands::CommandRequest request;
    request.command = std::move(command);
    request.id = std::move(id);
    return request;
}

/// A client whose `call` always throws, to exercise the internal-error path on
/// a real worker thread (the `client_exception_is_contained` case).
class ThrowingClient final : public tui::ICommandClient
{
public:
    commands::CommandResponse call(const commands::CommandRequest&) override
    {
        throw std::runtime_error("boom");
    }

    std::vector<tui::CommandInfo> commands() const override
    {
        return links_catalog();
    }

    tui::SessionInfo session() const override { return {}; }
};

/// Owns a Links screen over a real @ref AsyncCaller worker thread.
struct LinksFixture
{
    MockCommandClient client;
    InlinePoster poster;
    tui::AsyncCaller caller {client, poster, 1};
    tui::Router router;
    tui::SessionInfo session;
    tui::TuiOptions options;
    tui::ScreenContext context {caller, session, router, options};

    LinksFixture() { client.set_commands(links_catalog()); }

    std::unique_ptr<tui::IScreen> screen()
    {
        return tui::make_links_screen(context, links_catalog());
    }
};

}  // namespace

// ---------------------------------------------------------------------------
// A slow command does not block the render/key path: with a worker held in a
// delayed `link.list`, `on_enter` returns at once, the frame renders the
// loading state (the model is only advanced on drain, so nothing has arrived),
// and the in-flight count is still 1 - the UI thread never waited on the call.
// Draining then shows the data.
//
// The chosen proof is `in_flight() == 1` with the loading frame, NOT a
// steady_clock upper bound on the event/render path. That is deliberate project
// policy: the wall-clock bound on `tui__10` was removed after it flaked under
// CI load, so these tests synchronize on posted completions and in_flight()
// only (set_delay models latency, nothing asserts on its duration). A handler
// that blocked the UI thread on the call would be caught here regardless:
// on_enter would not return until the call finished, so the data (not
// "loading...") would already be in the frame and in_flight() would be 0.
//
// On failure first check: a Links handler that calls the client synchronously
// on the UI thread instead of through AsyncCaller::submit.
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(slow_command_does_not_block_render)
{
    LinksFixture fx;
    fx.client.set_delay(k_latency);
    fx.client.enqueue("link.list", ok(one_link_page()));

    std::unique_ptr<tui::IScreen> screen = fx.screen();
    screen->on_enter();  // submits link.list; must not block

    // The call is in flight (counted until its completion is drained) and the
    // frame renders without the response - the loading placeholder, not data.
    BOOST_TEST(fx.caller.in_flight() == 1u);
    const std::string loading = render_to_text(screen->component(), 80, 24);
    BOOST_TEST(loading.find("loading...") != std::string::npos);
    BOOST_TEST(loading.find("docs") == std::string::npos);
    BOOST_TEST(fx.caller.in_flight() == 1u);  // still not blocked/drained

    // When the worker finishes and the completion is drained, the data shows.
    fx.poster.wait_for_posted(1);
    fx.poster.run_pending();
    BOOST_TEST(fx.caller.in_flight() == 0u);
    render_to_text(screen->component(), 80, 24);  // settle measured width
    const std::string loaded = render_to_text(screen->component(), 80, 24);
    BOOST_TEST(loaded.find("docs") != std::string::npos);
}

// ---------------------------------------------------------------------------
// Input is accepted while a request is pending: with a delayed link.list still
// in flight (not yet drained), focusing the filter and typing shows the typed
// text in the frame. The keystrokes never wait on the call.
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(input_accepted_while_request_in_flight)
{
    LinksFixture fx;
    fx.client.set_delay(k_latency);
    fx.client.enqueue("link.list", ok(one_link_page()));

    std::unique_ptr<tui::IScreen> screen = fx.screen();
    screen->on_enter();
    render_to_text(screen->component(), 80, 24);
    BOOST_TEST(fx.caller.in_flight() == 1u);

    // Focus the filter (`/`) and type while the call is still in flight.
    send_events(screen->component(), {ftxui::Event::Character("/")});
    send_events(screen->component(), {ftxui::Event::Character("abc")});
    const std::string grid = render_to_text(screen->component(), 80, 24);
    BOOST_TEST(grid.find("abc") != std::string::npos);
    BOOST_TEST(fx.caller.in_flight() == 1u);  // input did not drain the call

    // Drain so the worker completes before teardown.
    fx.poster.wait_for_posted(1);
    fx.poster.run_pending();
    BOOST_TEST(fx.caller.in_flight() == 0u);
}

// ---------------------------------------------------------------------------
// A completion is applied exactly once: one link.list is submitted, one
// completion is posted, and draining it a second time runs nothing more. The
// data shows and the client recorded exactly one request.
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(response_posted_exactly_once)
{
    LinksFixture fx;
    fx.client.enqueue("link.list", ok(one_link_page()));

    std::unique_ptr<tui::IScreen> screen = fx.screen();
    screen->on_enter();

    fx.poster.wait_for_posted(1);
    const std::size_t ran = fx.poster.run_pending();
    BOOST_TEST(ran == 1u);
    BOOST_TEST(fx.poster.run_pending() == 0u);  // not re-posted
    BOOST_TEST(fx.caller.in_flight() == 0u);

    render_to_text(screen->component(), 80, 24);
    const std::string grid = render_to_text(screen->component(), 80, 24);
    BOOST_TEST(grid.find("docs") != std::string::npos);

    std::size_t list_calls = 0;
    for (const commands::CommandRequest& r : fx.client.recorded_requests()) {
        if (r.command == "link.list") {
            ++list_calls;
        }
    }
    BOOST_TEST(list_calls == 1u);
}

// ---------------------------------------------------------------------------
// A stale completion is dropped: two refreshes race on a real worker, the first
// (slow) request is superseded and its token cancelled - exactly the stale-drop
// a screen does on each re-query. When both complete, only the latest request's
// handler runs; the first is suppressed by AsyncCaller's cancel (T-121), so the
// stale data never reaches the UI.
// On failure first check: deliver() consulting the cancel set in
// src/tui/async_caller.cpp.
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(stale_response_discarded)
{
    MockCommandClient client;
    InlinePoster poster;
    tui::AsyncCaller caller {client, poster, 2};

    std::atomic<bool> first_ran {false};
    std::string latest_id;

    // The first (stale) request is delayed so it is still in flight when the
    // second is issued; the UI then supersedes it and cancels its token.
    client.set_delay(k_latency);
    const tui::CallToken first = caller.submit(
        make_request("link.list", "req-1"),
        [&](const commands::CommandResponse&) { first_ran.store(true); });

    caller.cancel(first);  // the screen's stale-drop on re-query
    const tui::CallToken second = caller.submit(
        make_request("link.list", "req-2"),
        [&](const commands::CommandResponse& resp) { latest_id = resp.id; });

    // Both completions post; draining runs the latest's handler only.
    poster.wait_for_posted(2);
    poster.run_pending();

    BOOST_TEST(!first_ran.load());  // stale completion dropped
    BOOST_TEST(latest_id == "req-2");  // latest data shows
    BOOST_TEST((first != second));
    BOOST_TEST(caller.in_flight() == 0u);
}

// ---------------------------------------------------------------------------
// Job polling is timer-driven, not blocking: a delayed job.list submitted by
// the Jobs screen's `on_enter` is in flight at once and the frame still
// renders, so a poll never freezes the UI. The 5 s cadence itself is owned by
// `tui__16_poll_ticker_test`; here the point is that the poll's command path is
// non-blocking.
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(job_polling_is_timer_driven_not_blocking)
{
    MockCommandClient client;
    InlinePoster poster;
    client.set_commands(jobs_catalog());
    client.set_delay(k_latency);
    tui::AsyncCaller caller {client, poster, 1};
    tui::Router router;
    tui::SessionInfo session;
    tui::TuiOptions options;
    tui::ScreenContext context {caller, session, router, options};

    std::unique_ptr<tui::IScreen> screen =
        tui::make_jobs_screen(context, jobs_catalog());
    client.enqueue("job.list", ok(one_job_page()));

    render_to_text(screen->component(), 80, 24);
    screen->on_enter();  // submits the first poll; must not block

    BOOST_TEST(caller.in_flight() == 1u);
    render_to_text(screen->component(), 80, 24);  // renders while polling
    BOOST_TEST(caller.in_flight() == 1u);

    poster.wait_for_posted(1);
    poster.run_pending();
    BOOST_TEST(caller.in_flight() == 0u);
}

// ---------------------------------------------------------------------------
// Teardown waits for workers without deadlock: with a delayed call in flight,
// destroying the caller finishes within a 2 s budget and the posted-but-
// undrained completion is a no-op after destruction, so no handler runs against
// freed state (T-120). Mirrors `tui__04_app_teardown_test`.
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(shutdown_waits_for_workers)
{
    MockCommandClient client;
    InlinePoster poster;
    client.set_delay(k_latency);
    client.enqueue("link.list", ok(one_link_page()));

    std::atomic<bool> handler_ran {false};
    auto caller = std::make_unique<tui::AsyncCaller>(client, poster, 1);
    caller->submit(make_request("link.list", "s-1"),
                   [&](const commands::CommandResponse&)
                   { handler_ran.store(true); });

    // Let the worker post its completion (it sits undrained in the poster).
    poster.wait_for_posted(1);

    // Destroy within the 2 s budget (success criterion 4); joining the worker
    // must not deadlock.
    std::future<void> done =
        std::async(std::launch::async, [&] { caller.reset(); });
    const bool finished =
        done.wait_for(std::chrono::seconds(2)) == std::future_status::ready;
    BOOST_TEST(finished);
    done.get();

    // Draining after destruction runs the guarded task, which is a no-op.
    poster.run_pending();
    BOOST_TEST(!handler_ran.load());
}

// ---------------------------------------------------------------------------
// A throwing client is contained: the worker turns the exception into a
// well-formed internal-error envelope delivered on the UI thread, and a screen
// driven over that client renders without crashing (the error surfaces instead
// of propagating).
// On failure first check: the try/catch around client.call() in worker_loop()
// (src/tui/async_caller.cpp).
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(client_exception_is_contained)
{
    // Direct proof: the exception becomes an internal-error envelope.
    {
        ThrowingClient client;
        InlinePoster poster;
        tui::AsyncCaller caller {client, poster, 1};

        std::atomic<bool> ok_flag {true};
        commands::CommandErrorCode code = commands::CommandErrorCode::none;
        caller.submit(make_request("link.list", "t-1"),
                      [&](const commands::CommandResponse& resp)
                      {
                          ok_flag.store(resp.ok);
                          code = resp.error.code;
                      });
        poster.wait_for_posted(1);
        poster.run_pending();
        BOOST_TEST(!ok_flag.load());
        BOOST_TEST((code == commands::CommandErrorCode::internal));
        BOOST_TEST(caller.in_flight() == 0u);
    }

    // Screen proof: a Links screen over a throwing client renders (no crash)
    // after the contained error is drained.
    {
        ThrowingClient client;
        InlinePoster poster;
        tui::AsyncCaller caller {client, poster, 1};
        tui::Router router;
        tui::SessionInfo session;
        tui::TuiOptions options;
        tui::ScreenContext context {caller, session, router, options};

        std::unique_ptr<tui::IScreen> screen =
            tui::make_links_screen(context, links_catalog());
        screen->on_enter();
        poster.wait_for_posted(1);
        poster.run_pending();
        BOOST_TEST(caller.in_flight() == 0u);

        render_to_text(screen->component(), 80, 24);
        const std::string grid = render_to_text(screen->component(), 80, 24);
        BOOST_TEST(!grid.empty());  // rendered, did not crash
    }
}
