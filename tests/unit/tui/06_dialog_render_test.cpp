#define BOOST_TEST_MODULE TuiDialogRenderTest

#include <chrono>
#include <string>
#include <vector>

#include <boost/test/unit_test.hpp>

#include <ftxui/component/component_base.hpp>
#include <ftxui/component/event.hpp>
#include <ftxui/dom/elements.hpp>

#include <url_shortener/commands/envelope.hpp>
#include <url_shortener/commands/error_codes.hpp>
#include <url_shortener/core/clock.hpp>
#include <url_shortener/tui/confirm.hpp>
#include <url_shortener/tui/confirm_view.hpp>
#include <url_shortener/tui/theme.hpp>
#include <url_shortener/tui/toast.hpp>
#include <url_shortener/tui/toast_view.hpp>

#include "golden.hpp"
#include "hostile_strings.hpp"
#include "render_helpers.hpp"

/**
 * @file 06_dialog_render_test.cpp
 * @brief Render tests for the D10 confirm dialog and error toast (FTXUI).
 *
 * Type: unit (render/snapshot, links url_shortener_tui + FTXUI).
 * Scenario: the confirm delete box (Delete disabled) and a yes_no box match
 *           their goldens; an error toast and its [d]etails view match theirs;
 *           the dialog component ignores Enter until the typed slug matches,
 *           submits once it does, and cancels on Esc; and a hostile raw envelope
 *           never reaches the toast grid (T-110).
 * Boundaries: Delete disabled vs enabled; Enter before vs after a token match.
 * On failure first check: whether a render sink stopped calling
 *           `sanitize_for_terminal`, or the submit guard in `confirm_dialog`.
 */

using url_shortener::tui::ConfirmKind;
using url_shortener::tui::ConfirmModel;
using url_shortener::tui::ConfirmRequest;
using url_shortener::tui::confirm_dialog;
using url_shortener::tui::render_confirm;
using url_shortener::tui::render_toasts;
using url_shortener::tui::theme_for;
using url_shortener::tui::ThemeName;
using url_shortener::tui::Toast;
using url_shortener::tui::toast_from_response;
using url_shortener::tui::ToastKind;
using url_shortener::tui::ToastQueue;
using url_shortener::tui::testing::assert_matches_golden;
using url_shortener::tui::testing::has_display_hazard;
using url_shortener::tui::testing::hostile_samples;
using url_shortener::tui::testing::render_to_text;
using url_shortener::tui::testing::send_events;

namespace cmd = url_shortener::commands;

namespace
{

ConfirmRequest delete_request()
{
    ConfirmRequest req;
    req.kind = ConfirmKind::type_token;
    req.title = "Delete link";
    req.prompt = "Soft-delete \"promo\"? Type the slug to confirm:";
    req.token = "promo";
    return req;
}

ConfirmRequest yes_no_request()
{
    ConfirmRequest req;
    req.kind = ConfirmKind::yes_no;
    req.title = "Confirm";
    req.prompt = "Run \"link.create\"? This changes data.";
    return req;
}

Toast error_toast()
{
    const cmd::CommandResponse r = cmd::make_failure(
        "c-42", cmd::make_error(cmd::CommandErrorCode::slug_conflict,
                                "slug \"docs\" already exists."));
    return toast_from_response("link.create", "c-42", r);
}

std::vector<std::string> split(const std::string& text)
{
    std::vector<std::string> lines;
    std::string line;
    for (char c : text)
    {
        if (c == '\n')
        {
            lines.push_back(line);
            line.clear();
        }
        else
        {
            line += c;
        }
    }
    lines.push_back(line);
    return lines;
}

}  // namespace

// ---------------------------------------------------------------------------
// The confirm delete box (Delete disabled, empty input) matches its golden.
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(confirm_delete_matches_golden)
{
    ConfirmModel model(delete_request());
    const std::string actual =
        render_to_text(render_confirm(model, theme_for(ThemeName::dark)), 80,
                       24);
    assert_matches_golden(actual, "d10_confirm_delete_80x24.txt");
}

// ---------------------------------------------------------------------------
// The yes_no confirm box matches its golden.
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(confirm_yes_no_matches_golden)
{
    ConfirmModel model(yes_no_request());
    const std::string actual =
        render_to_text(render_confirm(model, theme_for(ThemeName::dark)), 80,
                       24);
    assert_matches_golden(actual, "d10_confirm_yesno_80x24.txt");
}

// ---------------------------------------------------------------------------
// The error toast (collapsed) matches its golden.
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(toast_error_matches_golden)
{
    ManualClock clock(std::chrono::system_clock::time_point {});
    ToastQueue queue(clock);
    queue.push(error_toast());

    const std::string actual = render_to_text(
        render_toasts(queue, false, theme_for(ThemeName::dark)), 80, 24);
    assert_matches_golden(actual, "d10_toast_error_80x24.txt");
}

// ---------------------------------------------------------------------------
// The error toast with [d]etails expanded matches its golden.
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(toast_details_matches_golden)
{
    ManualClock clock(std::chrono::system_clock::time_point {});
    ToastQueue queue(clock);
    queue.push(error_toast());

    const std::string actual = render_to_text(
        render_toasts(queue, true, theme_for(ThemeName::dark)), 80, 24);
    assert_matches_golden(actual, "d10_toast_details_80x24.txt");
}

// ---------------------------------------------------------------------------
// The dialog ignores Enter until the typed slug matches, then submits; Esc
// cancels. The action button is disabled until can_submit().
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(dialog_enter_ignored_until_match_then_submits)
{
    ConfirmModel model(delete_request());
    ftxui::Component dialog = confirm_dialog(model, theme_for(ThemeName::dark));

    BOOST_TEST(!model.can_submit());  // Delete disabled initially

    // Enter before any typing is ignored.
    send_events(dialog, {ftxui::Event::Return});
    BOOST_TEST(!model.result().has_value());

    // Type the slug; the guard then accepts a submit.
    send_events(dialog, {ftxui::Event::Character("p"),
                         ftxui::Event::Character("r"),
                         ftxui::Event::Character("o"),
                         ftxui::Event::Character("m"),
                         ftxui::Event::Character("o")});
    BOOST_TEST(model.can_submit());

    send_events(dialog, {ftxui::Event::Return});
    BOOST_REQUIRE(model.result().has_value());
    BOOST_TEST(model.result().value());
}

// ---------------------------------------------------------------------------
// Esc cancels the dialog.
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(dialog_esc_cancels)
{
    ConfirmModel model(delete_request());
    ftxui::Component dialog = confirm_dialog(model, theme_for(ThemeName::dark));

    send_events(dialog, {ftxui::Event::Escape});
    BOOST_REQUIRE(model.result().has_value());
    BOOST_TEST(!model.result().value());
}

// ---------------------------------------------------------------------------
// Backspace deletes a whole typed code point (so an exact match is reachable).
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(dialog_backspace_deletes_codepoint)
{
    ConfirmModel model(delete_request());
    ftxui::Component dialog = confirm_dialog(model, theme_for(ThemeName::dark));

    send_events(dialog, {ftxui::Event::Character("p"),
                         ftxui::Event::Character("r"),
                         ftxui::Event::Character("x")});
    BOOST_TEST(!model.can_submit());
    send_events(dialog, {ftxui::Event::Backspace,
                         ftxui::Event::Character("o"),
                         ftxui::Event::Character("m"),
                         ftxui::Event::Character("o")});
    BOOST_TEST(model.input() == "promo");
    BOOST_TEST(model.can_submit());
}

// ---------------------------------------------------------------------------
// T-110: a hostile raw envelope shown via [d]etails never reaches the grid -
// no ESC / C1 / bidi / zero-width byte survives any rendered line.
// On failure first check: whether render_toast stopped sanitizing its lines.
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(hostile_details_never_reaches_grid)
{
    for (const auto& sample : hostile_samples())
    {
        Toast t;
        t.kind = ToastKind::error;
        t.title = "! " + sample.raw;
        t.body = sample.raw;
        t.source_command = "link.create";
        t.request_id = "c-1";
        t.raw_envelope =
            "{\n  \"error\": {\n    \"message\": \"" + sample.raw + "\"\n  }\n}";

        ManualClock clock(std::chrono::system_clock::time_point {});
        ToastQueue queue(clock);
        queue.push(std::move(t));

        const std::string grid = render_to_text(
            render_toasts(queue, true, theme_for(ThemeName::dark)), 80, 24);

        BOOST_TEST(grid.find('\x1b') == std::string::npos,
                   "ESC reached the toast grid for sample: " << sample.name);
        for (const std::string& line : split(grid))
        {
            BOOST_TEST(!has_display_hazard(line),
                       "display hazard in a toast cell for sample: "
                           << sample.name);
        }
    }
}
