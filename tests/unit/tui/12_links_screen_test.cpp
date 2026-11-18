#define BOOST_TEST_MODULE TuiLinksScreenTest

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
#include <url_shortener/tui/screens/links_screen.hpp>
#include <url_shortener/tui/theme.hpp>
#include <url_shortener/tui/tui_options.hpp>

#include "golden.hpp"
#include "hostile_strings.hpp"
#include "inline_poster.hpp"
#include "mock_command_client.hpp"
#include "render_helpers.hpp"

/**
 * @file 12_links_screen_test.cpp
 * @brief Render + behaviour tests for the Links tab screen (D3, subtask
 *        06.0/12).
 *
 * Type: unit (render/snapshot + component events, links url_shortener_tui +
 *       FTXUI).
 * Scenario: the first entry issues exactly one link.list with status:"all"; the
 *           4-row D3 body (long truncated URL + a deleted row) matches its
 *           golden; `n` opens the create form; `t` on an active row sends
 *           link.disable after a confirm; `d` requires the typed slug then
 * sends link.delete and refreshes; `u` is available only on a deleted row; a
 *           server error raises a toast and keeps the rows; an empty result
 *           renders the empty state; a read-only session dims the write keys
 * and sends nothing; T-110 - hostile item strings never reach the grid.
 * Boundaries: active vs deleted selection; confirm vs refused action; full vs
 *           read-only session; loaded vs empty vs error state.
 * On failure first check: whether the D3 layout in links_screen.cpp changed
 *           (regenerate with UPDATE_GOLDEN=1 and review), or an action routed
 *           around run_mutation.
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

/// The link.* catalog the Links screen resolves commands from.
std::vector<tui::CommandInfo> catalog()
{
    const tui::ArgInfo slug = arg("slug", tui::ArgKind::string, true);
    return {
        cmd("link.list", "read", false, {}),
        cmd("link.create",
            "write",
            true,
            {arg("target_url", tui::ArgKind::string, true), arg("slug")}),
        cmd("link.update",
            "write",
            true,
            {slug, arg("enabled", tui::ArgKind::boolean)}),
        cmd("link.enable", "write", true, {slug}),
        cmd("link.disable", "write", true, {slug}),
        cmd("link.delete", "write", true, {slug}),
        cmd("link.restore", "write", true, {slug}),
        cmd("link.preview", "read", false, {slug}),
    };
}

/// The 4-row D3 page: a long (truncated) URL and a deleted row, total 1204.
std::string list_four()
{
    return R"({"items":[)"
           R"({"slug":"docs","url":"https://example.com/docs/getting-started",)"
           R"("status":"active","stats":{"total_redirects":12044}},)"
           R"({"slug":"launch",)"
           R"("url":"https://example.com/launch?utm_source=twitter&utm_campaign=autumn",)"
           R"("status":"active","stats":{"total_redirects":8730}},)"
           R"({"slug":"promo","url":"https://shop.example.com/promo",)"
           R"("status":"disabled","stats":{"total_redirects":4112}},)"
           R"({"slug":"old-blog","url":"https://example.com/blog/2019/hello",)"
           R"("status":"deleted","stats":{"total_redirects":7}})"
           R"(],"offset":0,"limit":20,"total":1204})";
}

commands::CommandResponse ok(const std::string& result)
{
    return commands::make_success("tui-1", result);
}

std::vector<ftxui::Event> type(const std::string& text)
{
    std::vector<ftxui::Event> events;
    for (char ch : text) {
        events.push_back(ftxui::Event::Character(std::string(1, ch)));
    }
    return events;
}

/// Owns the fixtures a Links-screen test needs.
struct Fixture
{
    MockCommandClient client;
    InlinePoster poster;
    tui::AsyncCaller caller {client, poster, 1};
    tui::Router router;
    tui::TuiOptions options;
    tui::SessionInfo session;
    tui::ScreenContext context {caller, session, router, options};

    explicit Fixture(bool read_only = false)
        : options(make_options(read_only))
        , session(make_session(read_only))
        , context {caller, session, router, options}
    {
    }

    static tui::TuiOptions make_options(bool read_only)
    {
        tui::TuiOptions o;
        o.read_only = read_only;
        return o;
    }

    static tui::SessionInfo make_session(bool read_only)
    {
        tui::SessionInfo s;
        s.read_only = read_only;
        return s;
    }

    std::unique_ptr<tui::IScreen> screen()
    {
        return tui::make_links_screen(context, catalog());
    }

    /// Waits for @p total cumulative posts, then drains the queued completions.
    void settle(std::size_t total)
    {
        poster.wait_for_posted(total);
        poster.run_pending();
    }
};

// The measured-width table view needs one frame to learn its box; the goldens
// use the second (stable) frame.
std::string render_twice(const ftxui::Component& component, int w, int h)
{
    render_to_text(component, w, h);
    return render_to_text(component, w, h);
}

}  // namespace

// ---------------------------------------------------------------------------
// First entry issues exactly one link.list with status:"all".
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(first_entry_issues_one_link_list_all)
{
    Fixture fx;
    fx.client.enqueue("link.list", ok(list_four()));

    std::unique_ptr<tui::IScreen> screen = fx.screen();
    screen->on_enter();
    fx.settle(1);

    const std::vector<commands::CommandRequest> requests =
        fx.client.recorded_requests();
    BOOST_REQUIRE_EQUAL(requests.size(), 1u);
    BOOST_TEST(requests[0].command == "link.list");
    BOOST_TEST(requests[0].args_json
               == "{\"status\":\"all\",\"offset\":0,\"limit\":20}");
}

// ---------------------------------------------------------------------------
// The 4-row D3 body matches its golden.
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(d3_body_matches_golden)
{
    Fixture fx;
    fx.client.enqueue("link.list", ok(list_four()));

    std::unique_ptr<tui::IScreen> screen = fx.screen();
    screen->on_enter();
    fx.settle(1);

    const std::string grid = render_twice(screen->component(), 80, 24);
    BOOST_TEST(grid.find(">docs") != std::string::npos);
    BOOST_TEST(grid.find("page 1/61  (20 per page)") != std::string::npos);
    BOOST_TEST(grid.find("status: (all)") != std::string::npos);
    BOOST_TEST(grid.find("Enter detail") != std::string::npos);
    assert_matches_golden(grid, "d03_links_80x24.txt");
}

// ---------------------------------------------------------------------------
// `n` opens the link.create form.
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(n_opens_create_form)
{
    Fixture fx;
    fx.client.enqueue("link.list", ok(list_four()));

    std::unique_ptr<tui::IScreen> screen = fx.screen();
    screen->on_enter();
    fx.settle(1);
    render_twice(screen->component(), 80, 24);

    send_events(screen->component(), {ftxui::Event::Character("n")});
    const std::string grid = render_to_text(screen->component(), 80, 24);
    BOOST_TEST(grid.find("(command: link.create") != std::string::npos);
}

// ---------------------------------------------------------------------------
// `t` on an active row sends link.disable after a confirm.
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(t_on_active_sends_link_disable_after_confirm)
{
    Fixture fx;
    fx.client.enqueue("link.list", ok(list_four()));
    fx.client.enqueue("link.disable", ok(R"({"slug":"docs"})"));
    fx.client.enqueue("link.list", ok(list_four()));  // post-mutation refresh

    std::unique_ptr<tui::IScreen> screen = fx.screen();
    screen->on_enter();
    fx.settle(1);
    render_twice(screen->component(), 80, 24);

    // docs (row 0, active) is selected by default.
    send_events(screen->component(), {ftxui::Event::Character("t")});
    BOOST_TEST(fx.client.recorded_requests().size() == 1u);  // confirm gates it

    send_events(screen->component(), {ftxui::Event::Return});  // confirm
    fx.settle(2);  // link.disable completion
    fx.settle(3);  // the refresh it triggers

    const std::vector<commands::CommandRequest> requests =
        fx.client.recorded_requests();
    bool disabled = false;
    std::size_t list_calls = 0;
    for (const commands::CommandRequest& request : requests) {
        if (request.command == "link.disable") {
            disabled = true;
            BOOST_TEST(request.args_json.find("docs") != std::string::npos);
        }
        if (request.command == "link.list") {
            ++list_calls;
        }
    }
    BOOST_TEST(disabled);
    BOOST_TEST(list_calls == 2u);  // initial + post-mutation refresh
}

// ---------------------------------------------------------------------------
// `d` requires the typed slug, then sends link.delete and refreshes.
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(d_typed_slug_confirm_sends_link_delete_then_refresh)
{
    Fixture fx;
    fx.client.enqueue("link.list", ok(list_four()));
    fx.client.enqueue("link.delete", ok(R"({"slug":"docs"})"));
    fx.client.enqueue("link.list", ok(list_four()));

    std::unique_ptr<tui::IScreen> screen = fx.screen();
    screen->on_enter();
    fx.settle(1);
    render_twice(screen->component(), 80, 24);

    send_events(screen->component(), {ftxui::Event::Character("d")});
    send_events(screen->component(),
                {ftxui::Event::Return});  // wrong: no token
    BOOST_TEST(fx.client.recorded_requests().size() == 1u);  // not submitted

    send_events(screen->component(), type("docs"));  // the token
    send_events(screen->component(), {ftxui::Event::Return});  // now submits
    fx.settle(2);
    fx.settle(3);

    const std::vector<commands::CommandRequest> requests =
        fx.client.recorded_requests();
    bool deleted = false;
    std::size_t list_calls = 0;
    for (const commands::CommandRequest& request : requests) {
        if (request.command == "link.delete") {
            deleted = true;
            BOOST_TEST(request.args_json.find("docs") != std::string::npos);
        }
        if (request.command == "link.list") {
            ++list_calls;
        }
    }
    BOOST_TEST(deleted);
    BOOST_TEST(list_calls == 2u);
}

// ---------------------------------------------------------------------------
// `u` restores only a deleted row; on an active row it sends nothing.
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(u_restore_only_on_deleted)
{
    // Active row: refused, nothing sent.
    {
        Fixture fx;
        fx.client.enqueue("link.list", ok(list_four()));
        std::unique_ptr<tui::IScreen> screen = fx.screen();
        screen->on_enter();
        fx.settle(1);
        render_twice(screen->component(), 80, 24);

        send_events(screen->component(), {ftxui::Event::Character("u")});
        fx.poster.run_pending();
        BOOST_TEST(fx.client.recorded_requests().size() == 1u);
    }

    // Deleted row (old-blog, last): restores after confirm.
    {
        Fixture fx;
        fx.client.enqueue("link.list", ok(list_four()));
        fx.client.enqueue("link.restore", ok(R"({"slug":"old-blog"})"));
        fx.client.enqueue("link.list", ok(list_four()));
        std::unique_ptr<tui::IScreen> screen = fx.screen();
        screen->on_enter();
        fx.settle(1);
        render_twice(screen->component(), 80, 24);

        send_events(screen->component(),
                    {ftxui::Event::Character("G")});  // select last
        send_events(screen->component(), {ftxui::Event::Character("u")});
        send_events(screen->component(), {ftxui::Event::Return});  // confirm
        fx.settle(2);
        fx.settle(3);

        bool restored = false;
        for (const commands::CommandRequest& request :
             fx.client.recorded_requests())
        {
            if (request.command == "link.restore") {
                restored = true;
                BOOST_TEST(request.args_json.find("old-blog")
                           != std::string::npos);
            }
        }
        BOOST_TEST(restored);
    }
}

// ---------------------------------------------------------------------------
// A server error on refresh raises a toast and keeps the loaded rows.
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(server_error_toasts_and_keeps_rows)
{
    Fixture fx;
    fx.client.enqueue("link.list", ok(list_four()));
    fx.client.enqueue(
        "link.list",
        commands::make_failure(
            "tui-2",
            commands::make_error(commands::CommandErrorCode::internal,
                                 "backend unavailable")));

    std::unique_ptr<tui::IScreen> screen = fx.screen();
    screen->on_enter();
    fx.settle(1);
    render_twice(screen->component(), 80, 24);

    send_events(screen->component(),
                {ftxui::Event::Character("r")});  // refresh
    fx.settle(2);

    const std::string grid = render_twice(screen->component(), 80, 24);
    BOOST_TEST(grid.find("docs") != std::string::npos);  // rows kept
    BOOST_TEST(grid.find("backend unavailable") != std::string::npos);  // toast
    assert_matches_golden(grid, "d03_links_error_80x24.txt");
}

// ---------------------------------------------------------------------------
// An empty result renders the empty state.
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(empty_result_renders_empty_state)
{
    Fixture fx;
    fx.client.enqueue("link.list",
                      ok(R"({"items":[],"offset":0,"limit":20,)"
                         R"("total":0})"));

    std::unique_ptr<tui::IScreen> screen = fx.screen();
    screen->on_enter();
    fx.settle(1);

    const std::string grid = render_twice(screen->component(), 80, 24);
    BOOST_TEST(grid.find("no rows") != std::string::npos);
    assert_matches_golden(grid, "d03_links_empty_80x24.txt");
}

// ---------------------------------------------------------------------------
// A filter commit re-queries and shows the filter text.
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(filter_commit_requeries)
{
    Fixture fx;
    fx.client.enqueue("link.list", ok(list_four()));
    fx.client.enqueue(
        "link.list",
        ok(R"({"items":[{"slug":"launch",)"
           R"("url":"https://example.com/launch","status":"active",)"
           R"("stats":{"total_redirects":8730}}],"offset":0,"limit":20,)"
           R"("total":1})"));

    std::unique_ptr<tui::IScreen> screen = fx.screen();
    screen->on_enter();
    fx.settle(1);
    render_twice(screen->component(), 80, 24);

    send_events(screen->component(), {ftxui::Event::Character("/")});  // focus
    send_events(screen->component(), type("camp:autumn"));
    send_events(screen->component(), {ftxui::Event::Return});  // commit
    fx.settle(2);

    const std::vector<commands::CommandRequest> requests =
        fx.client.recorded_requests();
    BOOST_REQUIRE_EQUAL(requests.size(), 2u);
    BOOST_TEST(requests[1].args_json.find("\"campaign\":\"autumn\"")
               != std::string::npos);

    const std::string grid = render_twice(screen->component(), 80, 24);
    BOOST_TEST(grid.find("camp:autumn") != std::string::npos);
    assert_matches_golden(grid, "d03_links_filter_80x24.txt");
}

// ---------------------------------------------------------------------------
// A read-only session dims the write keys and sends no mutation.
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(read_only_dims_write_keys_and_sends_nothing)
{
    Fixture fx(/*read_only=*/true);
    fx.client.enqueue("link.list", ok(list_four()));

    std::unique_ptr<tui::IScreen> screen = fx.screen();
    screen->on_enter();
    fx.settle(1);

    const std::string grid = render_twice(screen->component(), 80, 24);
    assert_matches_golden(grid, "d03_links_readonly_80x24.txt");

    // Every write key is refused: nothing new is submitted.
    for (const char* key : {"n", "e", "t", "d", "u"}) {
        send_events(screen->component(), {ftxui::Event::Character(key)});
    }
    fx.poster.run_pending();
    BOOST_TEST(fx.client.recorded_requests().size() == 1u);  // only the list
}

// ---------------------------------------------------------------------------
// T-110: hostile item strings never reach the rendered grid.
// On failure first check: the table view's fit_cell sanitizes before fitting.
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(hostile_item_strings_never_reach_grid)
{
    for (const auto& sample : hostile_samples()) {
        Fixture fx;
        const std::string item =
            "{\"slug\":" + url_shortener::jsonString(sample.raw)
            + ",\"url\":" + url_shortener::jsonString(sample.raw)
            + ",\"status\":\"active\",\"stats\":{\"total_redirects\":1}}";
        fx.client.enqueue("link.list",
                          ok("{\"items\":[" + item
                             + "],\"offset\":0,\"limit\":20,\"total\":1}"));

        std::unique_ptr<tui::IScreen> screen = fx.screen();
        screen->on_enter();
        fx.settle(1);
        const std::string grid = render_twice(screen->component(), 80, 24);

        BOOST_TEST(grid.find('\x1b') == std::string::npos,
                   "ESC reached the grid for sample: " << sample.name);
        for (const std::string& line : split_lines(grid)) {
            BOOST_TEST(!has_display_hazard(line),
                       "display hazard in a cell for sample: " << sample.name);
        }
    }
}
