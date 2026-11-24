#define BOOST_TEST_MODULE TuiLinkDetailModelTest

#include <chrono>
#include <cstdint>
#include <string>
#include <vector>

#include <boost/test/unit_test.hpp>
#include <url_shortener/commands/envelope.hpp>
#include <url_shortener/core/clock.hpp>
#include <url_shortener/tui/link_detail_model.hpp>

/**
 * @file 13_link_detail_model_test.cpp
 * @brief Unit tests for the FTXUI-free Link detail / stats model (D4, subtask
 *        06.0/13).
 *
 * Type: unit (pure model, url_shortener_tui_model, no FTXUI).
 * Scenario: stats_request renders the 7-day window from a ManualClock as
 *           epoch-second string args with bucket "day"; sparkline scales a
 *           series to a ramp (empty / single / all-zero / 2^63 / width 1, and
 *           an ASCII variant); format_count groups thousands; rows() lays out
 *           the D4 grid for a full / sparse / deleted link; weekday_from_epoch
 *           maps month/year boundaries and a leap day.
 * Boundaries: pending vs loaded vs failed panes; present vs absent link fields;
 *           zero vs max sparkline values; pre-epoch (negative) weekday.
 * On failure first check: whether serializeLinkViewJson /
 *           serializeLinkStatsJson field names changed, or the ramp / window
 *           constants moved.
 */

namespace tui = url_shortener::tui;
namespace commands = url_shortener::commands;

namespace
{

/// A ManualClock frozen at @p epoch_seconds since the Unix epoch (UTC).
ManualClock clock_at(std::int64_t epoch_seconds)
{
    return ManualClock(std::chrono::system_clock::time_point {}
                       + std::chrono::seconds {epoch_seconds});
}

/// A full LinkView JSON (serializeLinkViewJson shape) for grid tests.
std::string full_link()
{
    return R"({"id":"l-1","slug":"docs",)"
           R"("url":"https://example.com/docs","short_url":"https://sho.rt/docs",)"
           R"("created_at":"2024-01-01T00:00:00Z",)"
           R"("updated_at":"2024-01-02T00:00:00Z","status":"active",)"
           R"("redirect_type":"permanent","tags":["a","b"],)"
           R"("metadata":{"k":"v"},)"
           R"("campaign":{"name":"autumn","source":"twitter","medium":"social"}})";
}

}  // namespace

// ---------------------------------------------------------------------------
// stats_request renders the window as epoch-second string args, bucket "day".
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(stats_request_window_bytes)
{
    ManualClock clock = clock_at(1700000000);
    tui::LinkDetailModel model(clock);

    const commands::CommandRequest request = model.stats_request("docs");
    BOOST_TEST(request.command == "link.stats");
    // from = 1700000000 - 7*24*3600 = 1699395200; both are JSON *strings*.
    BOOST_TEST(request.args_json
               == "{\"slug\":\"docs\",\"from\":\"1699395200\","
                  "\"to\":\"1700000000\",\"bucket\":\"day\"}");
}

// ---------------------------------------------------------------------------
// get_request carries the slug and the correlation id.
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(get_request_bytes)
{
    ManualClock clock = clock_at(0);
    tui::LinkDetailModel model(clock);

    const commands::CommandRequest request = model.get_request("docs", "tui-7");
    BOOST_TEST(request.command == "link.get");
    BOOST_TEST(request.id == "tui-7");
    BOOST_TEST(request.args_json == "{\"slug\":\"docs\"}");
}

// ---------------------------------------------------------------------------
// sparkline scale cases.
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(sparkline_scale_cases)
{
    // Empty series -> empty string, regardless of width.
    BOOST_TEST(tui::sparkline({}, 5, true) == "");
    // Width 0 -> empty string.
    BOOST_TEST(tui::sparkline({1, 2, 3}, 0, true) == "");
    // Single value -> top of the ramp (ascii '#').
    BOOST_TEST(tui::sparkline({5}, 10, true) == "#");
    // All zero -> flat level-0 baseline (ascii space).
    BOOST_TEST(tui::sparkline({0, 0, 0}, 3, true) == "   ");
    // Width 1 truncates to the first value; 1/3 scaled over " .:-=+*#" -> ':'.
    BOOST_TEST(tui::sparkline({1, 2, 3}, 1, true) == ":");
    // Huge values near 2^63 must not overflow the scaling.
    const std::uint64_t huge = 9223372036854775808ull;  // 2^63
    const std::string two = tui::sparkline({1, huge}, 2, true);
    BOOST_REQUIRE_EQUAL(two.size(), 2u);
    BOOST_TEST(two == " #");
    // Unicode ramp: all-zero baseline is the lowest block U+2581, not a space.
    BOOST_TEST(tui::sparkline({0}, 1, false) == "\xe2\x96\x81");
    // Unicode ramp: a max value is the full block U+2588.
    BOOST_TEST(tui::sparkline({7}, 1, false) == "\xe2\x96\x88");
}

// ---------------------------------------------------------------------------
// format_count groups thousands.
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(format_count_cases)
{
    BOOST_TEST(tui::format_count(0) == "0");
    BOOST_TEST(tui::format_count(999) == "999");
    BOOST_TEST(tui::format_count(1000) == "1,000");
    BOOST_TEST(tui::format_count(84210) == "84,210");
    BOOST_TEST(tui::format_count(1000000) == "1,000,000");
}

// ---------------------------------------------------------------------------
// weekday_from_epoch across month/year boundaries, a leap day and pre-epoch.
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(weekday_from_epoch_cases)
{
    BOOST_TEST(tui::weekday_from_epoch(0) == "Thu");  // 1970-01-01
    BOOST_TEST(tui::weekday_from_epoch(86400) == "Fri");  // 1970-01-02
    BOOST_TEST(tui::weekday_from_epoch(-86400) == "Wed");  // 1969-12-31
    BOOST_TEST(tui::weekday_from_epoch(1704067200) == "Mon");  // 2024-01-01
    BOOST_TEST(tui::weekday_from_epoch(951782400) == "Tue");  // 2000-02-29 leap
    BOOST_TEST(tui::weekday_from_epoch(1709164800)
               == "Thu");  // 2024-02-29 leap
}

// ---------------------------------------------------------------------------
// rows() lays out the D4 grid for a full link.
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(rows_full_link)
{
    ManualClock clock = clock_at(0);
    tui::LinkDetailModel model(clock);
    BOOST_TEST(model.rows().empty());  // nothing applied yet

    model.apply_link(full_link());
    const std::vector<tui::DetailRow> rows = model.rows();
    BOOST_REQUIRE_EQUAL(rows.size(), 6u);

    BOOST_TEST(rows[0].left_label == "id");
    BOOST_TEST(rows[0].left_value == "l-1");
    BOOST_TEST(rows[0].right_label == "short url");
    BOOST_TEST(rows[0].right_value == "https://sho.rt/docs");

    BOOST_TEST(rows[1].left_label == "target");
    BOOST_TEST(rows[1].left_value == "https://example.com/docs");
    BOOST_TEST(rows[1].right_label == "");

    BOOST_TEST(rows[2].left_value == "active");
    BOOST_TEST(rows[2].right_value == "permanent");

    BOOST_TEST(rows[3].left_value == "2024-01-01T00:00:00Z");
    BOOST_TEST(rows[3].right_value == "-");  // expires absent from LinkView

    BOOST_TEST(rows[4].left_value == "a, b");
    BOOST_TEST(rows[4].right_value == "autumn / twitter / social");

    BOOST_TEST(rows[5].left_label == "metadata");
    BOOST_TEST(rows[5].left_value == "k=v");

    BOOST_TEST(model.slug() == "docs");
    BOOST_TEST(!model.is_deleted());
}

// ---------------------------------------------------------------------------
// rows() shows "-" for an absent tags / campaign / metadata / expires.
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(rows_sparse_link)
{
    ManualClock clock = clock_at(0);
    tui::LinkDetailModel model(clock);
    model.apply_link(
        R"({"id":"l-2","slug":"bare","url":"https://e.com/x",)"
        R"("short_url":"https://s/x","created_at":"2024-01-01T00:00:00Z",)"
        R"("updated_at":"2024-01-01T00:00:00Z","status":"active",)"
        R"("redirect_type":"temporary","tags":[],"metadata":{},)"
        R"("campaign":null})");
    const std::vector<tui::DetailRow> rows = model.rows();
    BOOST_REQUIRE_EQUAL(rows.size(), 6u);
    BOOST_TEST(rows[3].right_value == "-");  // expires
    BOOST_TEST(rows[4].left_value == "-");  // tags
    BOOST_TEST(rows[4].right_value == "-");  // campaign
    BOOST_TEST(rows[5].left_value == "-");  // metadata
}

// ---------------------------------------------------------------------------
// A deleted link is detected from status.
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(rows_deleted_link)
{
    ManualClock clock = clock_at(0);
    tui::LinkDetailModel model(clock);
    model.apply_link(
        R"({"id":"l-3","slug":"gone","url":"https://e.com/g",)"
        R"("short_url":"https://s/g","created_at":"2024-01-01T00:00:00Z",)"
        R"("updated_at":"2024-01-01T00:00:00Z","status":"deleted",)"
        R"("redirect_type":"permanent","tags":[],"metadata":{},)"
        R"("campaign":null})");
    BOOST_TEST(model.is_deleted());
    BOOST_TEST(model.rows()[2].left_value == "deleted");
}

// ---------------------------------------------------------------------------
// stats_header and day_bars derive from the range report.
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(stats_header_and_day_bars)
{
    ManualClock clock = clock_at(0);
    tui::LinkDetailModel model(clock);
    BOOST_TEST(model.day_bars().empty());  // nothing applied

    model.apply_stats(
        R"({"slug":"docs","total_attempts":84210,)"
        R"("successful_redirects":83720,"attempts_by_status_code":{},)"
        R"("attempts_by_domain":{},"time_buckets":[)"
        R"({"bucket_start":0,"count":5},{"bucket_start":86400,"count":9}],)"
        R"("from":0,"to":604800,"bucket":"day"})");

    BOOST_TEST(model.has_stats());
    BOOST_TEST(model.stats_header()
               == "Stats (7d)  attempts 84,210  success 83,720  "
                  "[bucket: day]");

    const std::vector<std::pair<std::string, std::uint64_t>> bars =
        model.day_bars();
    BOOST_REQUIRE_EQUAL(bars.size(), 2u);
    BOOST_TEST(bars[0].first == "Thu");
    BOOST_TEST(bars[0].second == 5u);
    BOOST_TEST(bars[1].first == "Fri");
    BOOST_TEST(bars[1].second == 9u);
}

// ---------------------------------------------------------------------------
// Errors are recorded per pane without affecting the other.
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(independent_pane_errors)
{
    ManualClock clock = clock_at(0);
    tui::LinkDetailModel model(clock);
    model.apply_link(full_link());
    model.apply_stats_error("internal", "backend down");

    BOOST_TEST(model.has_link());  // link pane intact
    BOOST_TEST(model.stats_failed());
    BOOST_TEST(model.stats_error_code() == "internal");
    BOOST_TEST(!model.rows().empty());  // the grid still renders

    tui::LinkDetailModel model2(clock);
    model2.apply_link_error("not_found", "no such link");
    model2.apply_stats(
        R"({"slug":"x","total_attempts":0,"successful_redirects":0,)"
        R"("attempts_by_status_code":{},"attempts_by_domain":{},)"
        R"("time_buckets":[],"from":0,"to":0,"bucket":"day"})");
    BOOST_TEST(model2.link_failed());
    BOOST_TEST(model2.link_error_code() == "not_found");
    BOOST_TEST(model2.has_stats());  // stats pane intact
}

// ---------------------------------------------------------------------------
// set_bucket drives the stats_request bucket and ignores an empty value.
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(set_bucket_changes_request)
{
    ManualClock clock = clock_at(1700000000);
    tui::LinkDetailModel model(clock);
    model.set_bucket("hour");
    BOOST_TEST(model.bucket() == "hour");
    BOOST_TEST(model.stats_request("docs").args_json.find("\"bucket\":\"hour\"")
               != std::string::npos);
    model.set_bucket("");  // ignored
    BOOST_TEST(model.bucket() == "hour");
}
