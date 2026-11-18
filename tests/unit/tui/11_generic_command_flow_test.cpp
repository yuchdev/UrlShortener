#define BOOST_TEST_MODULE TuiGenericCommandFlowTest

#include <memory>
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
#include <url_shortener/tui/screens/generic_command_screen.hpp>
#include <url_shortener/tui/screens/palette_screen.hpp>
#include <url_shortener/tui/theme.hpp>
#include <url_shortener/tui/tui_options.hpp>

#include "golden.hpp"
#include "inline_poster.hpp"
#include "mock_command_client.hpp"
#include "render_helpers.hpp"

/**
 * @file 11_generic_command_flow_test.cpp
 * @brief Palette -> GenericForm -> Confirm -> result flow tests (D9/D11,
 *        06.0/11).
 *
 * Type: unit (component + mock seam, links url_shortener_tui + FTXUI).
 * Scenario: choosing a no-arg read command sends exactly one request and shows
 *           the result viewer; a typed-slug delete confirms then sends one
 *           request; cancelling a confirm sends nothing; a job response raises
 *           the job toast; raw-JSON mode submits the reserialized object; a
 *           read-only write entry sends nothing; T-116 - a raw-JSON write
 *           and a no-arg mutating command each pass ConfirmPolicy before any
 *           call and send zero requests in read-only.
 * Boundaries: read vs mutating; confirm vs cancel; generated vs raw-JSON args;
 *           full vs read-only session.
 * On failure first check: GenericCommandComponent::OnEvent routing and
 *           submit_raw()/on_submit_result() in generic_command_screen.cpp, and
 *           that the palette forwards events to the nested command.
 */

namespace tui = url_shortener::tui;
namespace commands = url_shortener::commands;

using tui::testing::assert_matches_golden;
using tui::testing::InlinePoster;
using tui::testing::MockCommandClient;
using tui::testing::render_to_text;
using tui::testing::send_events;

namespace
{

tui::ArgInfo arg(const std::string& name, bool required = false)
{
    tui::ArgInfo info;
    info.name = name;
    info.kind = tui::ArgKind::string;
    info.required = required;
    return info;
}

tui::CommandInfo db_tables()
{
    tui::CommandInfo info;
    info.name = "db.tables";
    info.summary = "List logical tables";
    info.permission = "read";
    return info;
}

tui::CommandInfo link_delete()
{
    tui::CommandInfo info;
    info.name = "link.delete";
    info.summary = "Soft-delete a link";
    info.permission = "write";
    info.mutating = true;
    info.args = {arg("slug", true)};
    return info;
}

tui::CommandInfo link_create()
{
    tui::CommandInfo info;
    info.name = "link.create";
    info.summary = "Create a short link";
    info.permission = "write";
    info.mutating = true;
    info.args = {arg("target_url", true)};
    return info;
}

tui::CommandInfo link_import()
{
    tui::CommandInfo info;
    info.name = "link.import";
    info.summary = "Import pairs";
    info.permission = "write";
    info.mutating = true;
    info.is_job = true;
    return info;
}

tui::CommandInfo cache_purge()
{
    tui::CommandInfo info;
    info.name = "cache.purge";
    info.summary = "Purge the cache";
    info.permission = "write";
    info.mutating = true;  // no args
    return info;
}

std::vector<ftxui::Event> type(const std::string& text)
{
    std::vector<ftxui::Event> events;
    for (char ch : text) {
        events.push_back(ftxui::Event::Character(std::string(1, ch)));
    }
    return events;
}

/// Owns the fixtures a flow test needs.
struct Fixture
{
    MockCommandClient client;
    InlinePoster poster;
    tui::AsyncCaller caller {client, poster, 1};
    tui::Router router;
    tui::TuiOptions options;
    tui::SessionInfo session;
    tui::ScreenContext context {caller, session, router, options};

    Fixture(bool assume_yes, bool read_only)
        : options(make_options(assume_yes, read_only))
        , context {caller, session, router, options}
    {
    }

    static tui::TuiOptions make_options(bool assume_yes, bool read_only)
    {
        tui::TuiOptions o;
        o.assume_yes = assume_yes;
        o.read_only = read_only;
        return o;
    }

    std::unique_ptr<tui::IScreen> palette(std::vector<tui::CommandInfo> cmds)
    {
        return tui::make_palette_screen(context, std::move(cmds));
    }

    std::unique_ptr<tui::IScreen> generic(tui::CommandInfo cmd)
    {
        tui::GenericCommandConfig cfg;
        cfg.command = std::move(cmd);
        return tui::make_generic_command_screen(context, std::move(cfg));
    }

    void drain(std::size_t n)
    {
        poster.wait_for_posted(n);
        poster.run_pending();
    }
};

/// Filters the palette to @p name and opens it (Enter).
void open_via_palette(tui::IScreen& palette, const std::string& name)
{
    render_to_text(palette.component(), 80, 24);  // measure
    send_events(palette.component(), type(name));
    send_events(palette.component(), {ftxui::Event::Return});
    render_to_text(palette.component(), 80, 24);  // let the command measure
}

}  // namespace

// ---------------------------------------------------------------------------
// Choosing db.tables sends exactly one {"command":"db.tables"} request and the
// result viewer shows the result.
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(choose_db_tables_sends_one_and_shows_result)
{
    Fixture fx(/*assume_yes=*/true, /*read_only=*/false);
    fx.client.enqueue(
        "db.tables",
        commands::make_success("tui-1", R"({"tables":["links"]})"));

    std::unique_ptr<tui::IScreen> palette = fx.palette({db_tables()});
    open_via_palette(*palette, "db.tables");
    send_events(palette->component(), {ftxui::Event::CtrlS});
    fx.drain(1);

    const std::vector<commands::CommandRequest> requests =
        fx.client.recorded_requests();
    BOOST_REQUIRE_EQUAL(requests.size(), 1u);
    BOOST_TEST(requests[0].command == "db.tables");
    BOOST_TEST(requests[0].args_json == "{}");

    const std::string grid = render_to_text(palette->component(), 80, 24);
    BOOST_TEST(grid.find("links") != std::string::npos);
    BOOST_TEST(grid.find("result: db.tables") != std::string::npos);
}

// ---------------------------------------------------------------------------
// The result viewer layout matches its D9 golden.
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(result_view_matches_golden)
{
    Fixture fx(/*assume_yes=*/true, /*read_only=*/false);
    fx.client.enqueue(
        "db.tables",
        commands::make_success(
            "tui-1",
            R"({"tables":["links","click_events","jobs"],"count":3})"));

    std::unique_ptr<tui::IScreen> palette = fx.palette({db_tables()});
    open_via_palette(*palette, "db.tables");
    send_events(palette->component(), {ftxui::Event::CtrlS});
    fx.drain(1);

    const std::string grid = render_to_text(palette->component(), 80, 24);
    assert_matches_golden(grid, "d09_result_view_80x24.txt");
}

// ---------------------------------------------------------------------------
// Choosing link.delete, filling the slug and typing the confirm token sends
// exactly one request.
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(link_delete_confirm_sends_one)
{
    Fixture fx(/*assume_yes=*/false, /*read_only=*/false);
    fx.client.enqueue("link.delete",
                      commands::make_success("tui-1", R"({"slug":"promo"})"));

    std::unique_ptr<tui::IScreen> palette = fx.palette({link_delete()});
    open_via_palette(*palette, "link.delete");
    send_events(palette->component(), type("promo"));       // fill slug
    send_events(palette->component(), {ftxui::Event::CtrlS});  // -> confirm
    BOOST_TEST(fx.client.recorded_requests().empty());  // confirm gates it

    send_events(palette->component(), type("promo"));       // the token
    send_events(palette->component(), {ftxui::Event::Return});  // confirm
    fx.drain(1);

    const std::vector<commands::CommandRequest> requests =
        fx.client.recorded_requests();
    BOOST_REQUIRE_EQUAL(requests.size(), 1u);
    BOOST_TEST(requests[0].command == "link.delete");
    BOOST_TEST(requests[0].args_json.find("promo") != std::string::npos);
}

// ---------------------------------------------------------------------------
// Cancelling the confirm sends nothing.
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(cancel_at_confirm_sends_nothing)
{
    Fixture fx(/*assume_yes=*/false, /*read_only=*/false);

    std::unique_ptr<tui::IScreen> palette = fx.palette({link_delete()});
    open_via_palette(*palette, "link.delete");
    send_events(palette->component(), type("promo"));
    send_events(palette->component(), {ftxui::Event::CtrlS});   // -> confirm
    send_events(palette->component(), {ftxui::Event::Escape});  // cancel

    BOOST_TEST(fx.client.recorded_requests().empty());
}

// ---------------------------------------------------------------------------
// A job response raises the "job accepted" toast and leaves the form open.
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(job_response_shows_job_toast)
{
    Fixture fx(/*assume_yes=*/true, /*read_only=*/false);
    commands::JobInfo job;
    job.id = "j-9";
    job.state = commands::JobState::queued;
    fx.client.enqueue("link.import",
                      commands::make_job_accepted("tui-1", job));

    std::unique_ptr<tui::IScreen> palette = fx.palette({link_import()});
    open_via_palette(*palette, "link.import");
    send_events(palette->component(), {ftxui::Event::CtrlS});
    fx.drain(1);

    const std::string grid = render_to_text(palette->component(), 80, 24);
    BOOST_TEST(grid.find("job j-9 accepted") != std::string::npos);
}

// ---------------------------------------------------------------------------
// Raw-JSON mode submits the reserialized typed object.
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(raw_json_mode_sends_typed_object)
{
    Fixture fx(/*assume_yes=*/true, /*read_only=*/false);
    fx.client.enqueue("db.tables",
                      commands::make_success("tui-1", R"({"ok":true})"));

    std::unique_ptr<tui::IScreen> palette = fx.palette({db_tables()});
    open_via_palette(*palette, "db.tables");
    send_events(palette->component(), {ftxui::Event::CtrlR});  // raw mode ("{}")
    send_events(palette->component(),
                {ftxui::Event::Backspace, ftxui::Event::Backspace});  // clear
    send_events(palette->component(), type(R"({"a":1})"));
    send_events(palette->component(), {ftxui::Event::CtrlS});
    fx.drain(1);

    const std::vector<commands::CommandRequest> requests =
        fx.client.recorded_requests();
    BOOST_REQUIRE_EQUAL(requests.size(), 1u);
    BOOST_TEST(requests[0].command == "db.tables");
    BOOST_TEST(requests[0].args_json == R"({"a":1})");
}

// ---------------------------------------------------------------------------
// Read-only: Enter on a write entry opens nothing and sends nothing.
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(read_only_enter_on_write_sends_nothing)
{
    Fixture fx(/*assume_yes=*/false, /*read_only=*/true);

    std::unique_ptr<tui::IScreen> palette = fx.palette({link_create()});
    render_to_text(palette->component(), 80, 24);
    send_events(palette->component(), type("link.create"));
    send_events(palette->component(), {ftxui::Event::Return});
    send_events(palette->component(), {ftxui::Event::CtrlS});  // ignored
    fx.poster.run_pending();  // nothing was submitted, so nothing to deliver

    BOOST_TEST(fx.client.recorded_requests().empty());
    const std::string grid = render_to_text(palette->component(), 80, 24);
    BOOST_TEST(grid.find("not_authorized") != std::string::npos);
}

// ---------------------------------------------------------------------------
// T-116: a raw-JSON write passes ConfirmPolicy before any call, and
// sends zero requests in a read-only session.
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(t116_raw_json_write_confirm_and_readonly)
{
    // Non-read-only: the raw-JSON write is gated by the confirm dialog.
    {
        Fixture fx(/*assume_yes=*/false, /*read_only=*/false);
        fx.client.enqueue(
            "link.create",
            commands::make_success("tui-1", R"({"slug":"x"})"));
        std::unique_ptr<tui::IScreen> screen = fx.generic(link_create());
        render_to_text(screen->component(), 80, 24);
        send_events(screen->component(), {ftxui::Event::CtrlR});
        send_events(screen->component(),
                    {ftxui::Event::Backspace, ftxui::Event::Backspace});
        send_events(screen->component(), type(R"({"target_url":"https://x"})"));
        send_events(screen->component(), {ftxui::Event::CtrlS});
        BOOST_TEST(fx.client.recorded_requests().empty());  // confirm first

        send_events(screen->component(), {ftxui::Event::Return});  // confirm
        fx.drain(1);
        BOOST_TEST(fx.client.recorded_requests().size() == 1u);
    }

    // Read-only: the same raw-JSON write submits nothing.
    {
        Fixture fx(/*assume_yes=*/false, /*read_only=*/true);
        std::unique_ptr<tui::IScreen> screen = fx.generic(link_create());
        render_to_text(screen->component(), 80, 24);
        send_events(screen->component(), {ftxui::Event::CtrlR});
        send_events(screen->component(),
                    {ftxui::Event::Backspace, ftxui::Event::Backspace});
        send_events(screen->component(), type(R"({"target_url":"https://x"})"));
        send_events(screen->component(), {ftxui::Event::CtrlS});
        send_events(screen->component(), {ftxui::Event::Return});
        BOOST_TEST(fx.client.recorded_requests().empty());
    }
}

// ---------------------------------------------------------------------------
// T-116: a no-arg mutating command passes ConfirmPolicy before any call, and
// sends zero requests in a read-only session.
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(t116_no_arg_mutating_confirm_and_readonly)
{
    {
        Fixture fx(/*assume_yes=*/false, /*read_only=*/false);
        fx.client.enqueue("cache.purge",
                          commands::make_success("tui-1", R"({"ok":true})"));
        std::unique_ptr<tui::IScreen> screen = fx.generic(cache_purge());
        render_to_text(screen->component(), 80, 24);
        send_events(screen->component(), {ftxui::Event::CtrlS});
        BOOST_TEST(fx.client.recorded_requests().empty());  // confirm first

        send_events(screen->component(), {ftxui::Event::Return});
        fx.drain(1);
        BOOST_TEST(fx.client.recorded_requests().size() == 1u);
    }

    {
        Fixture fx(/*assume_yes=*/false, /*read_only=*/true);
        std::unique_ptr<tui::IScreen> screen = fx.generic(cache_purge());
        render_to_text(screen->component(), 80, 24);
        send_events(screen->component(), {ftxui::Event::CtrlS});
        send_events(screen->component(), {ftxui::Event::Return});
        BOOST_TEST(fx.client.recorded_requests().empty());
    }
}
