#define BOOST_TEST_MODULE TuiImportExportScreenTest

#include <cstddef>
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
#include <url_shortener/tui/screen_id.hpp>
#include <url_shortener/tui/screens/import_export_screen.hpp>
#include <url_shortener/tui/theme.hpp>
#include <url_shortener/tui/tui_options.hpp>

#include "golden.hpp"
#include "hostile_strings.hpp"
#include "inline_poster.hpp"
#include "mock_command_client.hpp"
#include "render_helpers.hpp"

/**
 * @file 15_import_export_screen_test.cpp
 * @brief Import / Export screen tests (D7/D11, 06.0/15): the import/export
 *        goldens, the one-request submit paths, the job hand-off to Jobs, the
 *        read-only gates (import Start, export path), the oversize guard, the
 *        path conflict / capability toasts and T-110 sanitization.
 *
 * Type: unit (component + mock seam, links url_shortener_tui + FTXUI).
 * Scenario: typing into the paste area then Ctrl-S confirms and sends one
 *        link.import; a job response routes to Jobs with the job focused; an
 *        invalid_field response keeps the typed data; --yes skips the confirm;
 *        read-only disables import Start (sends nothing) and the export path
 *        field (inline export still works); an oversize paste never reaches the
 *        wire; a path conflict / capability denial surfaces as a value-free
 *        toast; hostile paste bytes render sanitized.
 * Boundaries: confirm vs --yes; write (import) vs read (export); inline vs path
 *        export; in-limit vs oversize paste.
 * On failure first check: ImportExportComponent::submit_import / submit_export
 * / make_on_result in import_export_screen.cpp and ImportModel/ExportModel.
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

tui::CommandInfo import_info()
{
    tui::CommandInfo info;
    info.name = "link.import";
    info.summary = "Bulk-import pairs";
    info.permission = "write";
    info.mutating = true;
    info.supports_dry_run = true;
    info.is_job = true;
    return info;
}

tui::CommandInfo export_info()
{
    tui::CommandInfo info;
    info.name = "link.export";
    info.summary = "Export pairs";
    info.permission = "read";  // base read; the server raises `path` to write
    info.is_job = true;
    return info;
}

std::vector<tui::CommandInfo> catalog()
{
    return {import_info(), export_info()};
}

commands::CommandResponse job_response(const std::string& id,
                                       const std::string& job_id)
{
    commands::JobInfo job;
    job.id = job_id;
    job.state = commands::JobState::queued;
    return commands::make_job_accepted(id, job);
}

/// Counts non-overlapping occurrences of @p needle in @p haystack.
std::size_t count_occurrences(const std::string& haystack,
                              const std::string& needle)
{
    std::size_t n = 0;
    std::size_t pos = 0;
    while ((pos = haystack.find(needle, pos)) != std::string::npos) {
        ++n;
        pos += needle.size();
    }
    return n;
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

/// Owns the fixtures an Import/Export-screen test needs.
struct Fixture
{
    MockCommandClient client;
    InlinePoster poster;
    tui::AsyncCaller caller {client, poster, 1};
    tui::Router router;
    tui::TuiOptions options;
    tui::SessionInfo session;
    tui::ScreenContext context {caller, session, router, options};

    explicit Fixture(bool read_only = false,
                     bool assume_yes = false,
                     std::size_t max_inline = 65536)
        : options(make_options(read_only, assume_yes, max_inline))
        , context {caller, session, router, options}
    {
    }

    static tui::TuiOptions make_options(bool read_only,
                                        bool assume_yes,
                                        std::size_t max_inline)
    {
        tui::TuiOptions o;
        o.read_only = read_only;
        o.assume_yes = assume_yes;
        o.max_inline_bytes = max_inline;
        return o;
    }

    std::unique_ptr<tui::IScreen> screen()
    {
        return tui::make_import_export_screen(context, catalog());
    }

    void drain(std::size_t new_posts)
    {
        m_expected_ += new_posts;
        poster.wait_for_posted(m_expected_);
        poster.run_pending();
    }

    std::size_t m_expected_ = 0;
};

/// Types @p text into the focused control as one character event.
void type(const ftxui::Component& component, const std::string& text)
{
    send_events(component, {ftxui::Event::Character(text)});
}

/// Sends @p n Tab events.
void tabs(const ftxui::Component& component, int n)
{
    for (int i = 0; i < n; ++i) {
        send_events(component, {ftxui::Event::Tab});
    }
}

}  // namespace

// ---------------------------------------------------------------------------
// The Import sub-tab matches its D7 golden.
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(import_matches_golden)
{
    Fixture fx;
    std::unique_ptr<tui::IScreen> screen = fx.screen();
    screen->on_enter();
    const std::string grid = render_to_text(screen->component(), 80, 24);
    assert_matches_golden(grid, "d07_import_80x24.txt");
    for (const std::string& line : split_lines(grid)) {
        BOOST_TEST(!has_display_hazard(line));
    }
}

// ---------------------------------------------------------------------------
// The Import sub-tab with a file-path source matches its golden.
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(import_file_matches_golden)
{
    Fixture fx;
    std::unique_ptr<tui::IScreen> screen = fx.screen();
    screen->on_enter();
    render_to_text(screen->component(), 80, 24);  // measure

    // Default focus is the paste area; Tab 4 times wraps to the Source radio.
    tabs(screen->component(), 4);
    send_events(screen->component(),
                {ftxui::Event::Character(" ")});  // -> file
    tabs(screen->component(), 1);  // -> file-path field
    type(screen->component(), "/tmp/pairs.csv");

    const std::string grid = render_to_text(screen->component(), 80, 24);
    assert_matches_golden(grid, "d07_import_file_80x24.txt");
}

// ---------------------------------------------------------------------------
// Typing a paste then Ctrl-S confirms and sends exactly one link.import.
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(type_then_start_sends_one_import)
{
    Fixture fx(/*read_only=*/false, /*assume_yes=*/false);
    fx.client.enqueue("link.import", job_response("tui-1", "j-9"));

    std::unique_ptr<tui::IScreen> screen = fx.screen();
    screen->on_enter();
    render_to_text(screen->component(), 80, 24);

    type(screen->component(), "https://example.com/a,a");
    send_events(screen->component(), {ftxui::Event::CtrlS});
    BOOST_TEST(count_command(fx.client.recorded_requests(), "link.import")
               == 0u);  // confirm gates it

    send_events(screen->component(), {ftxui::Event::Return});  // confirm
    fx.drain(1);

    const std::vector<commands::CommandRequest> reqs =
        fx.client.recorded_requests();
    BOOST_TEST(count_command(reqs, "link.import") == 1u);
    bool found_data = false;
    for (const commands::CommandRequest& r : reqs) {
        if (r.command == "link.import"
            && r.args_json.find("https://example.com/a,a") != std::string::npos)
        {
            found_data = true;
        }
    }
    BOOST_TEST(found_data);
}

// ---------------------------------------------------------------------------
// A job response routes to the Jobs tab with the job focused (D7 "after
// Start").
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(job_response_routes_to_jobs_with_focus)
{
    Fixture fx(/*read_only=*/false, /*assume_yes=*/true);
    fx.client.enqueue("link.import", job_response("tui-1", "j-9"));

    std::unique_ptr<tui::IScreen> screen = fx.screen();
    screen->on_enter();
    render_to_text(screen->component(), 80, 24);

    type(screen->component(), "https://x,a");
    send_events(screen->component(),
                {ftxui::Event::CtrlS});  // --yes: no confirm
    fx.drain(1);

    BOOST_CHECK(fx.router.active_tab() == tui::ScreenId::jobs);
    BOOST_CHECK_EQUAL(fx.context.focus_job_id, "j-9");
}

// ---------------------------------------------------------------------------
// An invalid_field response keeps the typed data (form not cleared).
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(invalid_field_keeps_data)
{
    Fixture fx(/*read_only=*/false, /*assume_yes=*/true);
    fx.client.enqueue(
        "link.import",
        commands::make_failure(
            "tui-1",
            commands::make_error(commands::CommandErrorCode::invalid_field,
                                 "row 2 is not a URL")));

    std::unique_ptr<tui::IScreen> screen = fx.screen();
    screen->on_enter();
    render_to_text(screen->component(), 80, 24);

    type(screen->component(), "notaurl,abc");
    send_events(screen->component(), {ftxui::Event::CtrlS});
    fx.drain(1);

    const std::string grid = render_to_text(screen->component(), 80, 24);
    BOOST_TEST(grid.find("invalid_field") != std::string::npos);  // toast
    BOOST_TEST(grid.find("notaurl,abc") != std::string::npos);  // data kept
}

// ---------------------------------------------------------------------------
// --yes skips the confirm: Ctrl-S submits immediately (no Return needed).
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(assume_yes_skips_confirm)
{
    Fixture fx(/*read_only=*/false, /*assume_yes=*/true);
    fx.client.enqueue("link.import", job_response("tui-1", "j-9"));

    std::unique_ptr<tui::IScreen> screen = fx.screen();
    screen->on_enter();
    render_to_text(screen->component(), 80, 24);

    type(screen->component(), "https://x,a");
    send_events(screen->component(), {ftxui::Event::CtrlS});
    fx.drain(1);

    BOOST_TEST(count_command(fx.client.recorded_requests(), "link.import")
               == 1u);
}

// ---------------------------------------------------------------------------
// Read-only disables import Start: Ctrl-S sends nothing and shows
// not_authorized.
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(read_only_disables_import_start)
{
    Fixture fx(/*read_only=*/true);
    std::unique_ptr<tui::IScreen> screen = fx.screen();
    screen->on_enter();
    render_to_text(screen->component(), 80, 24);

    type(screen->component(), "https://x,a");
    send_events(screen->component(), {ftxui::Event::CtrlS});
    fx.poster.run_pending();  // nothing submitted

    BOOST_TEST(count_command(fx.client.recorded_requests(), "link.import")
               == 0u);
    const std::string grid = render_to_text(screen->component(), 80, 24);
    BOOST_TEST(grid.find("not_authorized") != std::string::npos);
}

// ---------------------------------------------------------------------------
// An oversize paste never reaches the wire (blocking message, no request).
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(oversize_blocks_and_matches_golden)
{
    Fixture fx(/*read_only=*/false, /*assume_yes=*/true, /*max_inline=*/8);
    std::unique_ptr<tui::IScreen> screen = fx.screen();
    screen->on_enter();
    render_to_text(screen->component(), 80, 24);

    type(screen->component(), "aaaaaaaaaaaa");  // 12 > cap (9)
    send_events(screen->component(), {ftxui::Event::CtrlS});
    fx.poster.run_pending();

    BOOST_TEST(count_command(fx.client.recorded_requests(), "link.import")
               == 0u);
    const std::string grid = render_to_text(screen->component(), 80, 24);
    BOOST_TEST(grid.find("exceeds the inline limit") != std::string::npos);
    assert_matches_golden(grid, "d07_import_toolarge_80x24.txt");
}

// ---------------------------------------------------------------------------
// A per-keystroke paste far past the cap stays bounded: once the buffer is
// full, each further character is an O(1) no-op (append_within_cap returns
// false) so the screen does no preview/sync work, and the form stays blocked
// (CWE-407 regression).
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(per_char_paste_past_cap_stays_blocked)
{
    Fixture fx(/*read_only=*/false, /*assume_yes=*/true, /*max_inline=*/8);
    std::unique_ptr<tui::IScreen> screen = fx.screen();
    screen->on_enter();
    render_to_text(screen->component(), 80, 24);

    // Deliver 4 x (cap+1) characters one event at a time, as a real terminal
    // delivers a paste; only the first 9 (max_inline + 1) are retained.
    std::vector<ftxui::Event> keys(36, ftxui::Event::Character("a"));
    send_events(screen->component(), keys);

    send_events(screen->component(), {ftxui::Event::CtrlS});
    fx.poster.run_pending();

    BOOST_TEST(count_command(fx.client.recorded_requests(), "link.import")
               == 0u);
    const std::string grid = render_to_text(screen->component(), 80, 24);
    BOOST_TEST(grid.find("exceeds the inline limit") != std::string::npos);
}

// ---------------------------------------------------------------------------
// The Export sub-tab matches its golden.
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(export_matches_golden)
{
    Fixture fx;
    std::unique_ptr<tui::IScreen> screen = fx.screen();
    screen->on_enter();
    render_to_text(screen->component(), 80, 24);

    send_events(screen->component(), {ftxui::Event::F2});  // -> Export
    const std::string grid = render_to_text(screen->component(), 80, 24);
    assert_matches_golden(grid, "d07_export_80x24.txt");
    for (const std::string& line : split_lines(grid)) {
        BOOST_TEST(!has_display_hazard(line));
    }
}

// ---------------------------------------------------------------------------
// T-118: read-only disables the export path field (reason shown); inline export
// still works and reaches the wire with no `path` argument.
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(read_only_export_path_disabled_inline_works)
{
    Fixture fx(/*read_only=*/true);
    fx.client.enqueue("link.export", job_response("tui-1", "j-7"));

    std::unique_ptr<tui::IScreen> screen = fx.screen();
    screen->on_enter();
    render_to_text(screen->component(), 80, 24);

    send_events(screen->component(), {ftxui::Event::F2});  // -> Export
    const std::string grid = render_to_text(screen->component(), 80, 24);
    BOOST_TEST(grid.find("not_authorized") != std::string::npos);
    BOOST_TEST(grid.find("disabled") != std::string::npos);

    send_events(screen->component(), {ftxui::Event::CtrlS});  // inline export
    fx.drain(1);

    const std::vector<commands::CommandRequest> reqs =
        fx.client.recorded_requests();
    BOOST_TEST(count_command(reqs, "link.export") == 1u);
    for (const commands::CommandRequest& r : reqs) {
        if (r.command == "link.export") {
            BOOST_TEST(r.args_json.find("\"path\"") == std::string::npos);
        }
    }
}

// ---------------------------------------------------------------------------
// T-118: an export to an existing path returns a conflict shown as a value-free
// toast (the server's code + message, never the path bytes).
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(export_path_conflict_value_free_toast)
{
    Fixture fx(/*read_only=*/false, /*assume_yes=*/true);
    fx.client.enqueue(
        "link.export",
        commands::make_failure(
            "tui-1",
            commands::make_error(commands::CommandErrorCode::invalid_field,
                                 "invalid export path")));

    std::unique_ptr<tui::IScreen> screen = fx.screen();
    screen->on_enter();
    render_to_text(screen->component(), 80, 24);

    send_events(screen->component(), {ftxui::Event::F2});  // -> Export
    tabs(screen->component(), 5);  // default fmt -> path field
    type(screen->component(), "/secret/out.csv");
    send_events(screen->component(), {ftxui::Event::CtrlS});
    fx.drain(1);

    BOOST_TEST(count_command(fx.client.recorded_requests(), "link.export")
               == 1u);
    const std::string grid = render_to_text(screen->component(), 80, 24);
    // The value-free toast carries the code and the server message only.
    BOOST_TEST(grid.find("invalid_field") != std::string::npos);
    BOOST_TEST(grid.find("invalid export path") != std::string::npos);
    // T-118: the path must not leak into the toast/error surface. It
    // legitimately echoes once in the input field the operator typed (and must
    // be preserved), so a single occurrence means zero leaked into the toast;
    // two would mean a leak.
    BOOST_TEST(count_occurrences(grid, "/secret/out.csv") == 1u);
}

// ---------------------------------------------------------------------------
// A --path-root / capability denial renders a value-free toast.
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(export_capability_denial_value_free_toast)
{
    Fixture fx(/*read_only=*/false, /*assume_yes=*/true);
    fx.client.enqueue(
        "link.export",
        commands::make_failure(
            "tui-1",
            commands::make_error(commands::CommandErrorCode::unsupported,
                                 "file paths disabled")));

    std::unique_ptr<tui::IScreen> screen = fx.screen();
    screen->on_enter();
    render_to_text(screen->component(), 80, 24);

    send_events(screen->component(), {ftxui::Event::F2});  // -> Export
    tabs(screen->component(), 5);  // -> path field
    type(screen->component(), "/etc/passwd");
    send_events(screen->component(), {ftxui::Event::CtrlS});
    fx.drain(1);

    const std::string grid = render_to_text(screen->component(), 80, 24);
    BOOST_TEST(grid.find("unsupported") != std::string::npos);
    BOOST_TEST(grid.find("file paths disabled") != std::string::npos);
    // T-118: the path echoes only in the input field (once), never in the
    // toast.
    BOOST_TEST(count_occurrences(grid, "/etc/passwd") == 1u);
}

// ---------------------------------------------------------------------------
// T-110: hostile paste bytes render sanitized (no display hazard on any row).
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(t110_hostile_paste_renders_sanitized)
{
    for (const tui::testing::HostileSample& sample : hostile_samples()) {
        BOOST_TEST_CONTEXT("sample=" << sample.name)
        {
            Fixture fx;
            std::unique_ptr<tui::IScreen> screen = fx.screen();
            screen->on_enter();
            render_to_text(screen->component(), 120, 24);
            type(screen->component(), sample.raw);

            const std::string grid =
                render_to_text(screen->component(), 120, 24);
            for (const std::string& line : split_lines(grid)) {
                BOOST_TEST(!has_display_hazard(line));
            }
        }
    }
}
