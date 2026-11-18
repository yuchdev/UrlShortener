#define BOOST_TEST_MODULE TuiFormScreenTest

#include <memory>
#include <string>
#include <vector>

#include <boost/test/unit_test.hpp>
#include <ftxui/component/event.hpp>
#include <url_shortener/commands/envelope.hpp>
#include <url_shortener/commands/error_codes.hpp>
#include <url_shortener/tui/async_caller.hpp>
#include <url_shortener/tui/command_client.hpp>
#include <url_shortener/tui/equivalent_cmd.hpp>
#include <url_shortener/tui/form_screen.hpp>
#include <url_shortener/tui/router.hpp>
#include <url_shortener/tui/screen.hpp>
#include <url_shortener/tui/screen_id.hpp>
#include <url_shortener/tui/theme.hpp>
#include <url_shortener/tui/tui_options.hpp>

#include "golden.hpp"
#include "hostile_strings.hpp"
#include "inline_poster.hpp"
#include "mock_command_client.hpp"
#include "render_helpers.hpp"

/**
 * @file 08_form_screen_test.cpp
 * @brief Behaviour + T-110 tests for the generic D5 form screen (06.0/08).
 *
 * Type: unit (component + mock seam, links url_shortener_tui + FTXUI).
 * Scenario: typing then Ctrl-S sends exactly one link.create with the expected
 *           args; a server invalid_url keeps the form open with data preserved
 *           and shows the error toast; a success calls on_success once; a
 * double Ctrl-S while busy sends one request; Esc prompts when touched and
 *           cancels immediately when untouched; a read-only session sends
 *           nothing; T-110 - a hostile edit-prefill renders sanitized.
 * Boundaries: confirm skipped via --yes for the submit cases; touched vs
 *           untouched Esc; read-only write command.
 * On failure first check: the submit path in form_view.cpp (try_submit ->
 *           run_mutation) and that the screen routes events to the form body.
 */

namespace tui = url_shortener::tui;
namespace commands = url_shortener::commands;

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
                 tui::ArgKind kind,
                 bool required = false,
                 bool nullable = false)
{
    tui::ArgInfo info;
    info.name = name;
    info.kind = kind;
    info.required = required;
    info.nullable = nullable;
    return info;
}

tui::CommandInfo link_create_info()
{
    tui::CommandInfo info;
    info.name = "link.create";
    info.permission = "write";
    info.mutating = true;
    info.supports_dry_run = true;
    tui::ArgInfo redirect = arg("redirect_type", tui::ArgKind::enumeration);
    redirect.enum_values = {"permanent", "temporary"};
    info.args = {arg("target_url", tui::ArgKind::string, true),
                 arg("slug", tui::ArgKind::string),
                 redirect,
                 arg("tags", tui::ArgKind::string_list)};
    return info;
}

tui::CommandInfo link_update_info()
{
    tui::CommandInfo info;
    info.name = "link.update";
    info.permission = "write";
    info.mutating = true;
    info.args = {arg("slug", tui::ArgKind::string, true),
                 arg("enabled", tui::ArgKind::boolean),
                 arg("tags", tui::ArgKind::string_list)};
    return info;
}

/// Minimal JSON string literal for @p s so a hostile value survives into the
/// prefilled buffer (control bytes become \uXXXX, so e.g. ESC round-trips).
std::string json_quote(const std::string& s)
{
    static const char* hex = "0123456789abcdef";
    std::string out = "\"";
    for (unsigned char c : s) {
        if (c == '"') {
            out += "\\\"";
        }
        else if (c == '\\') {
            out += "\\\\";
        }
        else if (c < 0x20) {
            out += "\\u00";
            out += hex[(c >> 4) & 0xF];
            out += hex[c & 0xF];
        }
        else {
            out += static_cast<char>(c);
        }
    }
    out += "\"";
    return out;
}

std::vector<ftxui::Event> type(const std::string& text)
{
    std::vector<ftxui::Event> events;
    for (char ch : text) {
        events.push_back(ftxui::Event::Character(std::string(1, ch)));
    }
    return events;
}

/// Builds the options a fixture starts from (set before @ref ScreenContext
/// copies them, which @ref ScreenContext does by value).
tui::TuiOptions opts_for(bool assume_yes, bool read_only)
{
    tui::TuiOptions o;
    o.assume_yes = assume_yes;
    o.read_only = read_only;
    return o;
}

/// Owns the fixtures a screen needs; builds a screen with the options a test
/// wants. `assume_yes` skips the mutation confirm so the submit cases assert
/// the request directly (the confirm path is covered by 08_mutation_flow_test).
struct ScreenFixture
{
    MockCommandClient client;
    InlinePoster poster;
    tui::AsyncCaller caller {client, poster, 1};
    tui::Router router;
    tui::TuiOptions options;
    tui::SessionInfo session;
    tui::ScreenContext context {caller, session, router, options};

    explicit ScreenFixture(bool assume_yes = true, bool read_only = false)
        // options must be set before `context` (declared after it) copies them.
        : options(opts_for(assume_yes, read_only))
        , context {caller, session, router, options}
    {
    }

    std::unique_ptr<tui::IScreen> make(tui::FormScreenConfig config)
    {
        return tui::make_form_screen(context, std::move(config));
    }
};

}  // namespace

// ---------------------------------------------------------------------------
// Typing a target URL then Ctrl-S sends exactly one link.create with the
// expected args.
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(type_then_submit_sends_one_request)
{
    ScreenFixture fx;
    fx.client.enqueue("link.create",
                      commands::make_success("tui-1", "{\"slug\":\"e\"}"));

    tui::FormScreenConfig config;
    config.command = link_create_info();
    config.id = tui::ScreenId::link_form;
    config.title = "New link";
    std::unique_ptr<tui::IScreen> screen = fx.make(std::move(config));

    render_to_text(screen->component(), 80, 24);  // learn the box
    send_events(screen->component(), type("https://e.com"));
    send_events(screen->component(), {ftxui::Event::CtrlS});

    fx.poster.wait_for_posted(1);
    fx.poster.run_pending();

    const std::vector<commands::CommandRequest> requests =
        fx.client.recorded_requests();
    BOOST_REQUIRE_EQUAL(requests.size(), 1u);
    BOOST_TEST(requests[0].command == "link.create");
    BOOST_TEST(requests[0].args_json == "{\"target_url\":\"https://e.com\"}");
    // Typing a text field marks the global hotkeys quiet.
    BOOST_TEST(fx.context.text_input_active);
}

// ---------------------------------------------------------------------------
// A server invalid_url keeps the form open with data preserved and shows the
// error toast.
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(server_error_keeps_form_open)
{
    ScreenFixture fx;
    fx.client.enqueue(
        "link.create",
        commands::make_failure(
            "tui-1",
            commands::make_error(commands::CommandErrorCode::invalid_url,
                                 "target_url must be http(s)")));

    tui::FormScreenConfig config;
    config.command = link_create_info();
    config.title = "New link";
    std::unique_ptr<tui::IScreen> screen = fx.make(std::move(config));

    render_to_text(screen->component(), 80, 24);
    send_events(screen->component(), type("ftp://bad"));
    send_events(screen->component(), {ftxui::Event::CtrlS});
    fx.poster.wait_for_posted(1);
    fx.poster.run_pending();

    const std::string grid = render_to_text(screen->component(), 80, 24);
    BOOST_TEST(grid.find("! invalid_url") != std::string::npos);
    BOOST_TEST(grid.find("command: link.create") != std::string::npos);
    BOOST_TEST(grid.find("ftp://bad") != std::string::npos);  // data preserved
    BOOST_TEST(fx.client.recorded_requests().size() == 1u);
}

// ---------------------------------------------------------------------------
// A success calls on_success exactly once.
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(success_calls_on_success_once)
{
    ScreenFixture fx;
    fx.client.enqueue("link.create",
                      commands::make_success("tui-1", "{\"slug\":\"e\"}"));

    int successes = 0;
    tui::FormScreenConfig config;
    config.command = link_create_info();
    config.on_success = [&successes](const commands::CommandResponse&)
    { ++successes; };
    std::unique_ptr<tui::IScreen> screen = fx.make(std::move(config));

    render_to_text(screen->component(), 80, 24);
    send_events(screen->component(), type("https://e.com"));
    send_events(screen->component(), {ftxui::Event::CtrlS});
    fx.poster.wait_for_posted(1);
    fx.poster.run_pending();

    BOOST_TEST(successes == 1);
}

// ---------------------------------------------------------------------------
// A double Ctrl-S while a call is in flight sends exactly one request.
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(double_submit_while_busy_sends_one)
{
    ScreenFixture fx;
    fx.client.enqueue("link.create",
                      commands::make_success("tui-1", "{\"slug\":\"e\"}"));

    tui::FormScreenConfig config;
    config.command = link_create_info();
    std::unique_ptr<tui::IScreen> screen = fx.make(std::move(config));

    render_to_text(screen->component(), 80, 24);
    send_events(screen->component(), type("https://e.com"));
    // Two Ctrl-S before draining: the second sees the caller busy.
    send_events(screen->component(),
                {ftxui::Event::CtrlS, ftxui::Event::CtrlS});
    fx.poster.wait_for_posted(1);
    fx.poster.run_pending();

    BOOST_TEST(fx.client.recorded_requests().size() == 1u);
}

// ---------------------------------------------------------------------------
// Esc cancels immediately when nothing is touched.
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(esc_untouched_cancels_immediately)
{
    ScreenFixture fx;
    int cancels = 0;
    tui::FormScreenConfig config;
    config.command = link_create_info();
    config.on_cancel = [&cancels] { ++cancels; };
    std::unique_ptr<tui::IScreen> screen = fx.make(std::move(config));

    render_to_text(screen->component(), 80, 24);
    send_events(screen->component(), {ftxui::Event::Escape});
    BOOST_TEST(cancels == 1);
}

// ---------------------------------------------------------------------------
// Esc with a touched field opens the discard prompt and only cancels once
// confirmed.
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(esc_touched_prompts_then_confirms)
{
    ScreenFixture fx;
    int cancels = 0;
    tui::FormScreenConfig config;
    config.command = link_create_info();
    config.on_cancel = [&cancels] { ++cancels; };
    std::unique_ptr<tui::IScreen> screen = fx.make(std::move(config));

    render_to_text(screen->component(), 80, 24);
    send_events(screen->component(), type("x"));  // touch a field
    send_events(screen->component(), {ftxui::Event::Escape});

    const std::string grid = render_to_text(screen->component(), 80, 24);
    BOOST_TEST(grid.find("Discard") != std::string::npos);
    BOOST_TEST(cancels == 0);  // not cancelled yet

    send_events(screen->component(),
                {ftxui::Event::Return});  // confirm discard
    BOOST_TEST(cancels == 1);
}

// ---------------------------------------------------------------------------
// A read-only session disables the submit and sends nothing for a write
// command.
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(read_only_sends_nothing)
{
    ScreenFixture fx(/*assume_yes=*/true, /*read_only=*/true);
    tui::FormScreenConfig config;
    config.command = link_create_info();
    std::unique_ptr<tui::IScreen> screen = fx.make(std::move(config));

    render_to_text(screen->component(), 80, 24);
    send_events(screen->component(), type("https://e.com"));
    send_events(screen->component(), {ftxui::Event::CtrlS});
    fx.poster.run_pending();

    BOOST_TEST(fx.client.recorded_requests().empty());
}

// ---------------------------------------------------------------------------
// Success criterion 3: the rendered Equivalent footer equals
// equivalent_cli(recorded request) byte-for-byte.
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(footer_matches_recorded_request)
{
    ScreenFixture fx;
    fx.client.enqueue("link.create", commands::make_success("tui-1", "{}"));

    tui::FormScreenConfig config;
    config.command = link_create_info();
    std::unique_ptr<tui::IScreen> screen = fx.make(std::move(config));

    render_to_text(screen->component(), 80, 24);
    // A short value so the whole CLI line fits the footer's column budget and
    // appears un-truncated in the grid (the footer source is byte-identical to
    // the request either way; truncation is only a display concern).
    send_events(screen->component(), type("https://e"));
    const std::string grid = render_to_text(screen->component(), 80, 24);

    send_events(screen->component(), {ftxui::Event::CtrlS});
    fx.poster.wait_for_posted(1);
    fx.poster.run_pending();

    const std::vector<commands::CommandRequest> requests =
        fx.client.recorded_requests();
    BOOST_REQUIRE_EQUAL(requests.size(), 1u);
    const std::string cli = tui::equivalent_cli(requests[0]);
    BOOST_TEST(cli
               == "url_shortener cmd link.create --json "
                  "'{\"target_url\":\"https://e\"}'");
    // The footer rendered the equivalent of the exact request sent; the line is
    // benign (sanitize is identity) and fits the 80-col budget, so it appears
    // verbatim in the grid.
    BOOST_TEST(grid.find(cli) != std::string::npos);
}

// ---------------------------------------------------------------------------
// The dry-run toggle (offered for a supports_dry_run command) sets
// options.dry_run on the submitted request.
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(dry_run_toggle_sets_option)
{
    ScreenFixture fx;
    fx.client.enqueue("link.create", commands::make_success("tui-1", "{}"));

    tui::FormScreenConfig config;
    config.command = link_create_info();  // supports_dry_run = true
    std::unique_ptr<tui::IScreen> screen = fx.make(std::move(config));

    render_to_text(screen->component(), 80, 24);
    send_events(screen->component(), type("https://e.com"));
    // Tab past the four fields to the dry-run toggle, then Space to enable it.
    send_events(screen->component(),
                {ftxui::Event::Tab,
                 ftxui::Event::Tab,
                 ftxui::Event::Tab,
                 ftxui::Event::Tab,
                 ftxui::Event::Character(" ")});
    const std::string grid = render_to_text(screen->component(), 80, 24);
    BOOST_TEST(grid.find("[x] dry-run") != std::string::npos);

    send_events(screen->component(), {ftxui::Event::CtrlS});
    fx.poster.wait_for_posted(1);
    fx.poster.run_pending();

    const std::vector<commands::CommandRequest> requests =
        fx.client.recorded_requests();
    BOOST_REQUIRE_EQUAL(requests.size(), 1u);
    BOOST_TEST(requests[0].options.dry_run);
}

// ---------------------------------------------------------------------------
// A benign edit-prefill renders the D5 edit form (golden).
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(edit_prefill_matches_golden)
{
    ScreenFixture fx;
    tui::FormScreenConfig config;
    config.command = link_update_info();
    config.id = tui::ScreenId::link_form;
    config.title = "Edit link";
    config.prefill_result =
        "{\"slug\":\"docs\",\"enabled\":true,"
        "\"tags\":[\"docs\",\"public\"]}";
    std::unique_ptr<tui::IScreen> screen = fx.make(std::move(config));

    render_to_text(screen->component(), 80, 24);  // learn the box
    const std::string grid = render_to_text(screen->component(), 80, 24);
    BOOST_TEST(grid.find("command: link.update") != std::string::npos);
    tui::testing::assert_matches_golden(grid, "d05_edit_link_80x24.txt");
}

// ---------------------------------------------------------------------------
// T-110: editing a link whose target_url / tags hold ESC[2J / U+202E prefills
// and renders the fields sanitized - no control / bidi byte reaches the grid.
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(hostile_edit_prefill_is_sanitized)
{
    for (const auto& sample : hostile_samples()) {
        ScreenFixture fx;
        tui::FormScreenConfig config;
        config.command = link_update_info();
        config.title = "Edit link";
        config.prefill_result =
            "{\"slug\":\"docs\",\"tags\":[" + json_quote(sample.raw) + "]}";
        std::unique_ptr<tui::IScreen> screen = fx.make(std::move(config));

        render_to_text(screen->component(), 80, 24);
        const std::string grid = render_to_text(screen->component(), 80, 24);
        BOOST_TEST(grid.find('\x1b') == std::string::npos,
                   "ESC reached the grid for sample: " << sample.name);
        for (const std::string& line : split_lines(grid)) {
            BOOST_TEST(!has_display_hazard(line),
                       "display hazard for sample: " << sample.name);
        }
    }
}
