#define BOOST_TEST_MODULE TuiMutationFlowTest

#include <chrono>
#include <string>
#include <vector>

#include <boost/test/unit_test.hpp>
#include <ftxui/component/event.hpp>
#include <url_shortener/commands/envelope.hpp>
#include <url_shortener/core/clock.hpp>
#include <url_shortener/tui/async_caller.hpp>
#include <url_shortener/tui/command_client.hpp>
#include <url_shortener/tui/confirm.hpp>
#include <url_shortener/tui/mutation_flow.hpp>
#include <url_shortener/tui/theme.hpp>
#include <url_shortener/tui/toast.hpp>

#include "inline_poster.hpp"
#include "mock_command_client.hpp"

/**
 * @file 08_mutation_flow_test.cpp
 * @brief Confirm-choke-point tests for run_mutation / MutationFlow (06.0/08).
 *
 * Type: unit (component + mock seam, links url_shortener_tui + FTXUI).
 * Scenario: a mutating command shows the confirm before any call (mock sees 0
 *           requests until confirmed); link.delete needs the typed slug; --yes
 *           skips the confirm; a cancelled confirm sends 0 requests; a
 *           non-mutating command bypasses the confirm; a success pushes a
 * toast. Boundaries: yes-no vs type-token confirm; --yes session; cancel vs
 * confirm. On failure first check: ConfirmPolicy::decide, and
 * MutationFlow::overlay_event submitting only on a confirmed dialog.
 */

namespace tui = url_shortener::tui;
namespace commands = url_shortener::commands;

using tui::testing::InlinePoster;
using tui::testing::MockCommandClient;

namespace
{

tui::CommandInfo info(const std::string& name,
                      const std::string& permission,
                      bool mutating)
{
    tui::CommandInfo i;
    i.name = name;
    i.permission = permission;
    i.mutating = mutating;
    return i;
}

/// Owns the collaborators a MutationFlow references.
struct FlowFixture
{
    MockCommandClient client;
    InlinePoster poster;
    ManualClock clock {std::chrono::system_clock::time_point {}};
    tui::ToastQueue toasts {clock};
    tui::AsyncCaller caller {client, poster, 1};
    tui::ConfirmPolicy policy;
    tui::MutationFlow flow;

    explicit FlowFixture(bool assume_yes = false)
        : policy(assume_yes)
        , flow(tui::MutationDeps {
              caller, policy, toasts, tui::theme_for(tui::ThemeName::dark)})
    {
    }

    commands::CommandRequest request(const std::string& command,
                                     const std::string& args = "{}")
    {
        commands::CommandRequest r;
        r.command = command;
        r.id = "tui-1";
        r.args_json = args;
        return r;
    }

    void drain()
    {
        poster.wait_for_posted(1);
        poster.run_pending();
    }
};

}  // namespace

// ---------------------------------------------------------------------------
// A mutating command shows the confirm before any call; only confirming
// submits.
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(mutating_confirms_before_call)
{
    FlowFixture fx;
    fx.client.enqueue("link.update",
                      commands::make_success("tui-1", "{\"slug\":\"a\"}"));

    tui::run_mutation(fx.flow,
                      fx.request("link.update", "{\"slug\":\"a\"}"),
                      info("link.update", "write", true),
                      nullptr);

    BOOST_TEST(fx.flow.awaiting_confirm());
    BOOST_TEST(fx.client.recorded_requests().empty());  // nothing sent yet

    // Confirm (yes-no) with Enter via the overlay.
    fx.flow.overlay()->OnEvent(ftxui::Event::Return);
    BOOST_TEST(!fx.flow.awaiting_confirm());
    fx.drain();
    BOOST_TEST(fx.client.recorded_requests().size() == 1u);
}

// ---------------------------------------------------------------------------
// A cancelled confirm sends zero requests.
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(cancelled_confirm_sends_nothing)
{
    FlowFixture fx;
    fx.client.enqueue("link.update", commands::make_success("tui-1", "{}"));

    tui::run_mutation(fx.flow,
                      fx.request("link.update"),
                      info("link.update", "write", true),
                      nullptr);
    BOOST_TEST(fx.flow.awaiting_confirm());

    fx.flow.overlay()->OnEvent(ftxui::Event::Escape);  // cancel
    BOOST_TEST(!fx.flow.awaiting_confirm());
    BOOST_TEST(fx.client.recorded_requests().empty());
}

// ---------------------------------------------------------------------------
// link.delete needs the typed slug: Enter does nothing until the slug is typed.
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(delete_needs_typed_token)
{
    FlowFixture fx;
    fx.client.enqueue("link.delete", commands::make_success("tui-1", "{}"));

    tui::run_mutation(fx.flow,
                      fx.request("link.delete", "{\"slug\":\"promo\"}"),
                      info("link.delete", "write", true),
                      nullptr);
    BOOST_TEST(fx.flow.awaiting_confirm());

    // Enter with no token typed: still awaiting, nothing sent.
    fx.flow.overlay()->OnEvent(ftxui::Event::Return);
    BOOST_TEST(fx.flow.awaiting_confirm());
    BOOST_TEST(fx.client.recorded_requests().empty());

    // Type the slug, then Enter submits.
    for (char ch : std::string("promo")) {
        fx.flow.overlay()->OnEvent(ftxui::Event::Character(std::string(1, ch)));
    }
    fx.flow.overlay()->OnEvent(ftxui::Event::Return);
    BOOST_TEST(!fx.flow.awaiting_confirm());
    fx.drain();
    BOOST_TEST(fx.client.recorded_requests().size() == 1u);
}

// ---------------------------------------------------------------------------
// A --yes session skips the confirm and submits directly.
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(assume_yes_skips_confirm)
{
    FlowFixture fx(/*assume_yes=*/true);
    fx.client.enqueue("link.delete", commands::make_success("tui-1", "{}"));

    tui::run_mutation(fx.flow,
                      fx.request("link.delete", "{\"slug\":\"promo\"}"),
                      info("link.delete", "write", true),
                      nullptr);
    BOOST_TEST(!fx.flow.awaiting_confirm());
    fx.drain();
    BOOST_TEST(fx.client.recorded_requests().size() == 1u);
}

// ---------------------------------------------------------------------------
// A non-mutating command bypasses the confirm entirely.
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(non_mutating_bypasses_confirm)
{
    FlowFixture fx;
    fx.client.enqueue("link.list", commands::make_success("tui-1", "{}"));

    tui::run_mutation(fx.flow,
                      fx.request("link.list"),
                      info("link.list", "read", false),
                      nullptr);
    BOOST_TEST(!fx.flow.awaiting_confirm());
    fx.drain();
    BOOST_TEST(fx.client.recorded_requests().size() == 1u);
}

// ---------------------------------------------------------------------------
// A completed submit pushes a toast and calls the on_result handler.
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(success_pushes_toast_and_result)
{
    FlowFixture fx(/*assume_yes=*/true);
    fx.client.enqueue("link.create",
                      commands::make_success("tui-1", "{\"slug\":\"e\"}"));

    int results = 0;
    tui::run_mutation(fx.flow,
                      fx.request("link.create"),
                      info("link.create", "write", true),
                      [&results](const commands::CommandResponse&)
                      { ++results; });
    fx.drain();

    BOOST_TEST(results == 1);
    BOOST_REQUIRE(!fx.toasts.visible().empty());
    BOOST_TEST(fx.toasts.visible().back().title == "link.create ok");
}
