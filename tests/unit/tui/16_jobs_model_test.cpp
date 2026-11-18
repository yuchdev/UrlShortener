#define BOOST_TEST_MODULE TuiJobsModelTest

#include <chrono>
#include <string>

#include <boost/test/unit_test.hpp>
#include <url_shortener/core/utils.hpp>
#include <url_shortener/tui/jobs_model.hpp>
#include <url_shortener/tui/text_fit.hpp>

#include "hostile_strings.hpp"

/**
 * @file 16_jobs_model_test.cpp
 * @brief Jobs view-model tests (D8, 06.0/16): progress bar, outcome text, poll
 *        schedule and JobsModel row/selection parsing.
 *
 * Type: unit (pure model, links url_shortener_tui_model; no FTXUI).
 * Scenario: progress_bar renders gauges and percent-only for narrow widths;
 *           outcome_text summarizes succeeded/failed/cancelled jobs;
 * PollSchedule backs off then resets; JobsModel parses a job.list page and
 * answers selection/cancellability/any-active and focus-by-id. Boundaries:
 * total 0 / done>total / width<4; result present vs absent (carry- forward:
 * list rows carry no result/error); terminal vs active state. On failure first
 * check: progress_bar/outcome_text in jobs_model.cpp and JobsModel::apply_list
 * cell/fact construction.
 */

namespace tui = url_shortener::tui;

using tui::testing::has_display_hazard;
using tui::testing::hostile_samples;

namespace
{

/// A three-job D8 sample list: a queued job, a running job with progress, and a
/// terminal (succeeded) job. Mirrors the catalog 3.4 job object shape.
std::string sample_list()
{
    return R"({"items":[)"
           R"({"id":"j-1","command":"link.import","state":"queued",)"
           R"("progress":{"done":0,"total":0}},)"
           R"({"id":"j-2","command":"link.import","state":"running",)"
           R"("progress":{"done":62,"total":100},"started_at":"2026-11-19T10:00:00Z"},)"
           R"({"id":"j-3","command":"db.rows","state":"succeeded",)"
           R"("progress":{"done":312,"total":312},)"
           R"("started_at":"2026-11-19T09:00:00Z",)"
           R"("finished_at":"2026-11-19T09:00:03Z"})"
           R"(],"total":3})";
}

}  // namespace

// ---------------------------------------------------------------------------
// progress_bar: filled count, percent, clamping, narrow width.
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(progress_bar_renders_gauge)
{
    // 62% of a width-10 ASCII bar is 6 filled cells, then "  62%".
    BOOST_TEST(tui::progress_bar(62, 100, 10, true) == "######----  62%");
    // 0% is an empty bar.
    BOOST_TEST(tui::progress_bar(0, 100, 10, true) == "----------   0%");
    // 100% fills the bar; the percent widens to 4.
    BOOST_TEST(tui::progress_bar(100, 100, 10, true) == "##########  100%");
    // done > total clamps to 100%.
    BOOST_TEST(tui::progress_bar(15, 10, 10, true) == "##########  100%");
}

BOOST_AUTO_TEST_CASE(progress_bar_unknown_total)
{
    // total == 0 is "unknown": all-empty bar, "?" percent.
    BOOST_TEST(tui::progress_bar(0, 0, 10, true) == "----------    ?");
}

BOOST_AUTO_TEST_CASE(progress_bar_widths)
{
    // width < 4 -> percent only (no bar).
    BOOST_TEST(tui::progress_bar(60, 100, 3, true) == "60%");
    BOOST_TEST(tui::progress_bar(0, 0, 3, true) == "?");

    // width 40: a 40-cell bar then the percent.
    const std::string w40 = tui::progress_bar(50, 100, 40, true);
    BOOST_TEST(w40.substr(0, 20) == std::string(20, '#'));
    BOOST_TEST(w40.find("50%") != std::string::npos);

    // Non-ASCII uses the block glyphs (U+2588 / U+2591), width 1 each.
    const std::string blocks = tui::progress_bar(50, 100, 10, false);
    BOOST_TEST(tui::display_width(blocks)
               == tui::display_width(tui::progress_bar(50, 100, 10, true)));
    BOOST_TEST(blocks.find('#') == std::string::npos);
}

// ---------------------------------------------------------------------------
// outcome_text: succeeded summary, failed code, cancelled.
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(outcome_text_succeeded)
{
    BOOST_TEST(
        tui::outcome_text(R"({"state":"succeeded","result":{"rows":312}})")
        == "312 rows");
    BOOST_TEST(
        tui::outcome_text(R"({"state":"succeeded","result":{"created":312}})")
        == "312 created");
    BOOST_TEST(
        tui::outcome_text(
            R"({"state":"succeeded","result":{"created":10,"total":12}})")
        == "10/12 created");
    // A list row (no result body) shows the terminal state.
    BOOST_TEST(tui::outcome_text(R"({"state":"succeeded"})") == "succeeded");
}

BOOST_AUTO_TEST_CASE(outcome_text_failed_and_cancelled)
{
    BOOST_TEST(tui::outcome_text(
                   R"({"state":"failed","error":{"code":"invalid_url"}})")
               == "invalid_url");
    BOOST_TEST(tui::outcome_text(R"({"state":"failed"})") == "failed");
    BOOST_TEST(tui::outcome_text(R"({"state":"cancelled"})")
               == "job_cancelled");
    BOOST_TEST(tui::outcome_text(R"({"state":"running"})") == "-");
    BOOST_TEST(tui::outcome_text(R"({"state":"queued"})") == "-");
}

BOOST_AUTO_TEST_CASE(is_terminal_state_truth)
{
    BOOST_TEST(tui::is_terminal_state("succeeded"));
    BOOST_TEST(tui::is_terminal_state("failed"));
    BOOST_TEST(tui::is_terminal_state("cancelled"));
    BOOST_TEST(!tui::is_terminal_state("queued"));
    BOOST_TEST(!tui::is_terminal_state("running"));
    BOOST_TEST(!tui::is_terminal_state(""));
}

// ---------------------------------------------------------------------------
// T-110: a hostile failure code is bounded and sanitizes clean.
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(outcome_text_hostile_sanitizes_clean)
{
    for (const auto& sample : hostile_samples()) {
        BOOST_TEST_CONTEXT("sample=" << sample.name)
        {
            const std::string job = R"({"state":"failed","error":{"code":)"
                + url_shortener::jsonString(sample.raw) + R"(}})";
            const std::string out = tui::outcome_text(job);
            // The model does not sanitize; the render sink does. Prove the
            // sanitized OUTCOME cell carries no display hazard (T-110).
            BOOST_TEST(!has_display_hazard(tui::sanitize_for_terminal(out)));
        }
    }
}

// ---------------------------------------------------------------------------
// PollSchedule: 1s base, backoff 2/4/5/5 capped, reset on success.
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(poll_schedule_backoff_and_reset)
{
    using ms = std::chrono::milliseconds;
    tui::PollSchedule sched;  // base 1s, cap 5s

    BOOST_TEST(sched.next_delay(true).count() == 1000);  // ok -> base
    BOOST_TEST(sched.next_delay(false).count() == 2000);  // fail -> x2
    BOOST_TEST(sched.next_delay(false).count() == 4000);
    BOOST_TEST(sched.next_delay(false).count() == 5000);  // capped
    BOOST_TEST(sched.next_delay(false).count() == 5000);  // stays capped
    BOOST_TEST(sched.next_delay(true).count() == 1000);  // success resets
}

BOOST_AUTO_TEST_CASE(poll_schedule_should_poll_truth_table)
{
    tui::PollSchedule sched;
    BOOST_TEST(sched.should_poll(true, true));
    BOOST_TEST(sched.should_poll(true, false));
    BOOST_TEST(sched.should_poll(false, true));
    BOOST_TEST(!sched.should_poll(false, false));
}

// ---------------------------------------------------------------------------
// JobsModel: a D8 sample list parses into three rows with correct facts.
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(jobs_model_parses_sample_list)
{
    tui::JobsModel model(/*ascii=*/true);
    model.apply_list(sample_list());

    BOOST_REQUIRE_EQUAL(model.job_count(), 3u);
    BOOST_TEST(model.any_active());  // j-1 queued, j-2 running

    // First row selected by default.
    BOOST_REQUIRE(model.selected_id().has_value());
    BOOST_TEST(*model.selected_id() == "j-1");
    BOOST_TEST(model.can_cancel_selected());  // queued is cancellable

    // The row cells reflect the parsed job (STATE and progress columns).
    const auto& rows = model.table().rows();
    BOOST_REQUIRE_EQUAL(rows.size(), 3u);
    BOOST_TEST(rows[1][0] == "j-2");
    BOOST_TEST(rows[1][2] == "running");
    BOOST_TEST(rows[1][3] == "######----  62%");  // 62% gauge
    // Carry-forward 05.0: a list row carries no result body, so a succeeded
    // row's OUTCOME is the terminal state, not the summary.
    BOOST_TEST(rows[2][6] == "succeeded");
    BOOST_TEST(rows[0][4] == "-");  // queued has no STARTED
}

BOOST_AUTO_TEST_CASE(jobs_model_can_cancel_per_state)
{
    tui::JobsModel model(true);
    model.apply_list(sample_list());

    model.focus_job("j-1");  // queued
    BOOST_TEST(model.can_cancel_selected());
    model.focus_job("j-2");  // running
    BOOST_TEST(model.can_cancel_selected());
    model.focus_job("j-3");  // succeeded -> terminal, not cancellable
    BOOST_TEST(!model.can_cancel_selected());
    BOOST_TEST(model.selected_state() == "succeeded");
}

BOOST_AUTO_TEST_CASE(jobs_model_focus_selects_row)
{
    tui::JobsModel model(true);
    model.apply_list(sample_list());

    model.focus_job("j-3");
    BOOST_REQUIRE(model.selected_id().has_value());
    BOOST_TEST(*model.selected_id() == "j-3");

    // Focusing an unknown id leaves the selection unchanged.
    model.focus_job("j-404");
    BOOST_TEST(*model.selected_id() == "j-3");
}

BOOST_AUTO_TEST_CASE(jobs_model_empty_and_malformed)
{
    tui::JobsModel model(true);
    model.apply_list(R"({"items":[],"total":0})");
    BOOST_TEST(model.job_count() == 0u);
    BOOST_TEST(!model.any_active());
    BOOST_TEST(!model.selected_id().has_value());
    BOOST_TEST(!model.can_cancel_selected());

    model.apply_list("not json");
    BOOST_TEST(model.job_count() == 0u);
}

BOOST_AUTO_TEST_CASE(jobs_model_caps_rows)
{
    std::string json = R"({"items":[)";
    for (std::size_t i = 0; i < tui::JobsModel::k_max_jobs + 25; ++i) {
        if (i != 0) {
            json += ",";
        }
        json += R"({"id":"j-)" + std::to_string(i)
            + R"(","command":"c","state":"queued"})";
    }
    json += R"(]})";

    tui::JobsModel model(true);
    model.apply_list(json);
    BOOST_TEST(model.job_count() == tui::JobsModel::k_max_jobs);
}
