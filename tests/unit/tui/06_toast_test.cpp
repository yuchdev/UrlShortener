#define BOOST_TEST_MODULE TuiToastTest

#include <chrono>
#include <string>
#include <vector>

#include <boost/test/unit_test.hpp>

#include <url_shortener/commands/envelope.hpp>
#include <url_shortener/commands/error_codes.hpp>
#include <url_shortener/core/clock.hpp>
#include <url_shortener/tui/text_fit.hpp>
#include <url_shortener/tui/toast.hpp>

#include "hostile_strings.hpp"

/**
 * @file 06_toast_test.cpp
 * @brief Unit tests for the D10 toast model (FTXUI-free).
 *
 * Type: unit (model library, links url_shortener_tui_model).
 * Scenario: an error envelope becomes a `! <code>` toast with the message body
 *           and the raw envelope kept intact; a success auto-expires after the
 *           TTL under a ManualClock while an error persists until dismissed; the
 *           queue caps at three (the oldest is evicted); an ambiguous envelope
 *           degrades to a generic `! internal` toast; and every string a toast
 *           hands to its render sink is terminal-safe after sanitization (T-110).
 * Boundaries: expiry exactly at the deadline; the fourth push; a 300-char body
 *             truncated to 200 code points + "...".
 * On failure first check: `toast_from_response` classification in
 *           `src/tui/toast.cpp`, or the push/tick bookkeeping in `ToastQueue`.
 */

using url_shortener::tui::Toast;
using url_shortener::tui::toast_from_response;
using url_shortener::tui::ToastKind;
using url_shortener::tui::ToastQueue;
using url_shortener::tui::sanitize_for_terminal;
using url_shortener::tui::testing::has_display_hazard;
using url_shortener::tui::testing::hostile_samples;

namespace cmd = url_shortener::commands;

namespace
{

cmd::CommandResponse error_response(cmd::CommandErrorCode code,
                                    const std::string& message,
                                    const std::string& id = "c-42")
{
    return cmd::make_failure(id, cmd::make_error(code, message));
}

std::vector<std::string> split_lines(const std::string& text)
{
    std::vector<std::string> out;
    std::string line;
    for (char c : text)
    {
        if (c == '\n')
        {
            out.push_back(line);
            line.clear();
        }
        else
        {
            line += c;
        }
    }
    out.push_back(line);
    return out;
}

}  // namespace

// ---------------------------------------------------------------------------
// An error envelope maps to a "! <code>" title, the message body and an intact
// raw envelope.
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(error_envelope_becomes_coded_toast)
{
    const cmd::CommandResponse r = error_response(
        cmd::CommandErrorCode::slug_conflict, "slug \"docs\" already exists.");
    const Toast t = toast_from_response("link.create", "c-42", r);

    BOOST_TEST((t.kind == ToastKind::error));
    BOOST_TEST(t.title == "! slug_conflict");
    BOOST_TEST(t.body == "slug \"docs\" already exists.");
    BOOST_TEST(t.source_command == "link.create");
    BOOST_TEST(t.request_id == "c-42");
    // The raw envelope kept for [d]etails carries the code verbatim.
    BOOST_TEST(t.raw_envelope.find("slug_conflict") != std::string::npos);
}

// ---------------------------------------------------------------------------
// A success envelope becomes a "<command> ok" success toast with no body.
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(success_envelope_becomes_ok_toast)
{
    const cmd::CommandResponse r =
        cmd::make_success("c-1", "{\"slug\":\"docs\"}");
    const Toast t = toast_from_response("link.create", "c-1", r);

    BOOST_TEST((t.kind == ToastKind::success));
    BOOST_TEST(t.title == "link.create ok");
    BOOST_TEST(t.body.empty());
    // pretty_print re-indents, so the envelope reads `"ok": true`.
    BOOST_TEST(t.raw_envelope.find("\"ok\": true") != std::string::npos);
}

// ---------------------------------------------------------------------------
// A long error message is truncated to 200 code points with an ellipsis for the
// display body; the raw envelope is unaffected.
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(long_body_truncated_for_display)
{
    const std::string message(300, 'a');
    const cmd::CommandResponse r =
        error_response(cmd::CommandErrorCode::invalid_field, message);
    const Toast t = toast_from_response("link.create", "c-7", r);

    BOOST_REQUIRE_EQUAL(t.body.size(), 203u);  // 200 + "..."
    BOOST_TEST(t.body.substr(t.body.size() - 3) == "...");
}

// ---------------------------------------------------------------------------
// A success toast auto-expires once the clock passes its deadline; an error
// toast never expires on tick and only a dismiss removes it.
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(success_expires_error_persists)
{
    ManualClock clock(std::chrono::system_clock::time_point {});
    ToastQueue queue(clock, std::chrono::seconds(6));

    queue.push(toast_from_response(
        "link.create", "c-1", cmd::make_success("c-1", "{}")));
    BOOST_REQUIRE_EQUAL(queue.visible().size(), 1u);

    clock.advance(std::chrono::seconds(5));
    queue.tick();
    BOOST_TEST(queue.visible().size() == 1u);  // not yet due

    clock.advance(std::chrono::seconds(1));  // now at the 6 s deadline
    queue.tick();
    BOOST_TEST(queue.visible().empty());

    // An error toast survives an arbitrary amount of ticking.
    queue.push(toast_from_response(
        "link.delete", "c-2",
        error_response(cmd::CommandErrorCode::not_found, "no such link",
                       "c-2")));
    clock.advance(std::chrono::seconds(3600));
    queue.tick();
    BOOST_REQUIRE_EQUAL(queue.visible().size(), 1u);

    queue.dismiss_front();
    BOOST_TEST(queue.visible().empty());
}

// ---------------------------------------------------------------------------
// Pushing a fourth toast evicts the oldest, keeping at most three visible.
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(fourth_toast_evicts_oldest)
{
    ManualClock clock(std::chrono::system_clock::time_point {});
    ToastQueue queue(clock, std::chrono::seconds(6));

    for (int i = 1; i <= 4; ++i)
    {
        Toast t;
        t.kind = ToastKind::error;  // errors never auto-expire
        t.title = "! t" + std::to_string(i);
        queue.push(std::move(t));
    }

    BOOST_REQUIRE_EQUAL(queue.visible().size(), 3u);
    BOOST_TEST(queue.visible().front().title == "! t2");  // t1 evicted
    BOOST_TEST(queue.visible().back().title == "! t4");
}

// ---------------------------------------------------------------------------
// An ambiguous failure (ok == false with error.code none) degrades to a generic
// "! internal" toast without crashing (threat model T-113).
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(ambiguous_envelope_becomes_internal_toast)
{
    cmd::CommandResponse r;
    r.ok = false;  // but error.code stays `none`
    r.id = "c-9";

    const Toast t = toast_from_response("link.create", "c-9", r);
    BOOST_TEST((t.kind == ToastKind::error));
    BOOST_TEST(t.title == "! internal");
    BOOST_TEST(!t.raw_envelope.empty());
}

// ---------------------------------------------------------------------------
// T-110: a hostile error message / raw envelope is terminal-safe at the render
// sink - the title, body and every raw-envelope line, once sanitized, carry no
// control / bidi / zero-width / combining-flood hazard.
// On failure first check: whether a sink stopped calling sanitize_for_terminal.
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(hostile_strings_are_terminal_safe_at_the_sink)
{
    for (const auto& sample : hostile_samples())
    {
        const cmd::CommandResponse r = error_response(
            cmd::CommandErrorCode::invalid_url, sample.raw, "c-1");
        const Toast t = toast_from_response("link.create", "c-1", r);

        BOOST_TEST(!has_display_hazard(sanitize_for_terminal(t.title)),
                   "title hazard for sample: " << sample.name);
        BOOST_TEST(!has_display_hazard(sanitize_for_terminal(t.body)),
                   "body hazard for sample: " << sample.name);
        for (const std::string& line : split_lines(t.raw_envelope))
        {
            BOOST_TEST(!has_display_hazard(sanitize_for_terminal(line)),
                       "raw-envelope hazard for sample: " << sample.name);
        }
    }
}
