#define BOOST_TEST_MODULE TuiDashboardModelTest

#include <cctype>
#include <cstdint>
#include <functional>
#include <limits>
#include <string>
#include <vector>

#include <boost/test/unit_test.hpp>
#include <url_shortener/commands/envelope.hpp>
#include <url_shortener/commands/error_codes.hpp>
#include <url_shortener/tui/dashboard_model.hpp>
#include <url_shortener/tui/panel_state.hpp>

/**
 * @file 17_dashboard_model_test.cpp
 * @brief Model tests for the Dashboard view-model (D2, subtask 06.0/17).
 *
 * Type: unit (pure view-model, links url_shortener_tui_model, no FTXUI).
 * Scenario: four counts responses fill the counters and mark the panel ready;
 *        the sample is ranked by redirects_24h and bars scale to the max (ties,
 *        zero max, huge counts); a single failing request degrades only its
 *        panel; the jobs line summarises 0/1/2 running jobs; has_stats_summary
 *        switches the request set and the exact-metrics apply drops the sample
 *        label; every refresh request is a read command.
 * Boundaries: ready vs. unavailable per panel; sampled vs. stats.summary; empty
 *        store; 0/1/2 running jobs.
 * On failure first check: DashboardModel::apply_* routing and refresh_requests
 *        tagging in dashboard_model.cpp.
 */

namespace tui = url_shortener::tui;
namespace commands = url_shortener::commands;

namespace
{

/// A monotonic `tui-N` id generator for refresh_requests.
std::function<std::string()> id_gen()
{
    auto counter = std::make_shared<int>(0);
    return [counter] { return "tui-" + std::to_string(++*counter); };
}

commands::CommandResponse ok(const std::string& json)
{
    return commands::make_success("id", json);
}

commands::CommandResponse fail(const std::string& message)
{
    return commands::make_failure(
        "id",
        commands::make_error(commands::CommandErrorCode::internal, message));
}

/// A one-item `link.list` page whose `total` is @p total.
std::string counts_page(std::uint64_t total)
{
    return R"({"items":[],"total":)" + std::to_string(total) + "}";
}

/// Finds the command of the request tagged @p panel in @p requests.
std::string command_for_panel(
    const std::vector<commands::CommandRequest>& requests,
    const std::string& panel)
{
    for (const commands::CommandRequest& request : requests) {
        if (tui::DashboardModel::panel_of(request.id) == panel) {
            return request.command;
        }
    }
    return std::string();
}

}  // namespace

// ---------------------------------------------------------------------------
// Four successful counts responses fill the counters and mark counts ready.
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(counts_from_four_responses)
{
    tui::DashboardModel model(false);
    model.start_refresh();
    model.apply("counts.total", ok(counts_page(1204)));
    model.apply("counts.active", ok(counts_page(1180)));
    model.apply("counts.disabled", ok(counts_page(9)));
    model.apply("counts.deleted", ok(counts_page(15)));

    BOOST_TEST((model.counts().state == tui::PanelState::ready));
    BOOST_TEST(model.counts().total == 1204u);
    BOOST_TEST(model.counts().active == 1180u);
    BOOST_TEST(model.counts().disabled == 9u);
    BOOST_TEST(model.counts().deleted == 15u);
}

// ---------------------------------------------------------------------------
// The sample is ranked by redirects_24h (top 3) and the 24h figure is the sum.
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(sample_ranking_and_sum)
{
    tui::DashboardModel model(false);
    model.apply("redirects",
                ok(R"({"items":[)"
                   R"({"slug":"promo","stats":{"redirects_24h":4112}},)"
                   R"({"slug":"docs","stats":{"redirects_24h":12044}},)"
                   R"({"slug":"zero","stats":{"redirects_24h":0}},)"
                   R"({"slug":"launch","stats":{"redirects_24h":8730}})"
                   R"(],"total":4})"));

    BOOST_TEST((model.redirects().state == tui::PanelState::ready));
    BOOST_TEST(model.redirects().sampled == true);
    BOOST_REQUIRE(model.redirects().redirects_24h.has_value());
    BOOST_TEST(*model.redirects().redirects_24h
               == 24886u);  // 4112+12044+0+8730
    BOOST_TEST(!model.redirects().success_pct.has_value());
    BOOST_TEST(!model.redirects().p50_ms.has_value());

    const std::vector<tui::TopSlug>& top = model.top_slugs();
    BOOST_REQUIRE_EQUAL(top.size(), 3u);
    BOOST_TEST(top[0].slug == "docs");
    BOOST_TEST(top[0].count == 12044u);
    BOOST_TEST(top[1].slug == "launch");
    BOOST_TEST(top[2].slug == "promo");
}

// ---------------------------------------------------------------------------
// Bar scaling: full bar at the max, proportional below, empty on zero max,
// ties render equal, and a huge count cannot overflow.
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(bar_scaling)
{
    tui::DashboardModel model(false);
    // ASCII glyphs are one byte each, so the string length is the glyph count.
    BOOST_TEST(model.bars(12044, 12044, 8, true).size() == 8u);  // full
    BOOST_TEST(model.bars(8730, 12044, 8, true).size() == 6u);  // round(5.8)
    BOOST_TEST(model.bars(4112, 12044, 8, true).size() == 3u);  // round(2.7)
    BOOST_TEST(model.bars(0, 12044, 8, true).empty());  // zero value
    BOOST_TEST(model.bars(5, 0, 8, true).empty());  // zero max

    // Ties render equal.
    BOOST_TEST(model.bars(100, 200, 8, true) == model.bars(100, 200, 8, true));
    BOOST_TEST(model.bars(100, 200, 8, true).size() == 4u);

    // A count near 2^63 does not overflow (long double scaling).
    const std::uint64_t huge = 9000000000000000000ull;
    BOOST_TEST(model.bars(huge, huge, 8, true).size() == 8u);
}

// ---------------------------------------------------------------------------
// A single failing request degrades only its panel (success criterion 2).
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(one_failing_panel_is_isolated)
{
    tui::DashboardModel model(false);
    model.start_refresh();
    model.apply("counts.total", ok(counts_page(1204)));
    model.apply("counts.active", ok(counts_page(1180)));
    model.apply("counts.disabled", ok(counts_page(9)));
    model.apply("counts.deleted", ok(counts_page(15)));
    model.apply("recent",
                ok(R"({"items":[{"slug":"a","status":"active",)"
                   R"("updated_at":"2026-11-19T10:42:00Z"}],"total":1})"));
    model.apply("backend", fail("backend boom"));  // only this one fails

    BOOST_TEST((model.counts().state == tui::PanelState::ready));
    BOOST_TEST((model.recent_state() == tui::PanelState::ready));
    BOOST_TEST(model.backend_line() == "Backend: n/a");
}

// ---------------------------------------------------------------------------
// A failing counts sub-request marks the whole counts panel unavailable.
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(counts_unavailable_when_one_sub_fails)
{
    tui::DashboardModel model(false);
    model.start_refresh();
    model.apply("counts.total", ok(counts_page(1204)));
    model.apply("counts.active", fail("active boom"));
    model.apply("counts.disabled", ok(counts_page(9)));
    model.apply("counts.deleted", ok(counts_page(15)));

    BOOST_TEST((model.counts().state == tui::PanelState::unavailable));
    BOOST_TEST(model.counts_error() == "active boom");
}

// ---------------------------------------------------------------------------
// An empty store: all counters zero, no top slugs, no recent rows.
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(empty_store)
{
    tui::DashboardModel model(false);
    model.start_refresh();
    model.apply("counts.total", ok(counts_page(0)));
    model.apply("counts.active", ok(counts_page(0)));
    model.apply("counts.disabled", ok(counts_page(0)));
    model.apply("counts.deleted", ok(counts_page(0)));
    model.apply("redirects", ok(R"({"items":[],"total":0})"));
    model.apply("recent", ok(R"({"items":[],"total":0})"));

    BOOST_TEST((model.counts().state == tui::PanelState::ready));
    BOOST_TEST(model.counts().total == 0u);
    BOOST_TEST(model.top_slugs().empty());
    BOOST_TEST(model.recent().empty());
    BOOST_REQUIRE(model.redirects().redirects_24h.has_value());
    BOOST_TEST(*model.redirects().redirects_24h == 0u);
}

// ---------------------------------------------------------------------------
// The jobs line summarises 0, 1 and 2 running jobs.
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(jobs_line_for_counts)
{
    tui::DashboardModel none(false);
    none.apply("jobs", ok(R"({"items":[],"total":0})"));
    BOOST_TEST(none.jobs_line() == "Jobs: none running");

    tui::DashboardModel one(false);
    one.apply("jobs",
              ok(R"({"items":[{"command":"link.import","state":"running",)"
                 R"("progress":{"done":62,"total":100}}],"total":1})"));
    BOOST_TEST(one.jobs_line() == "Jobs: 1 running (link.import 62%)");

    tui::DashboardModel two(false);
    two.apply(
        "jobs",
        ok(R"({"items":[)"
           R"({"command":"link.import","state":"running","progress":{"done":62,"total":100}},)"
           R"({"command":"link.export","state":"running","progress":{"done":1,"total":10}})"
           R"(],"total":2})"));
    BOOST_TEST(two.jobs_line() == "Jobs: 2 running (link.import 62%)");
}

// ---------------------------------------------------------------------------
// has_stats_summary switches the redirects request from a sampled link.list to
// stats.summary, and the exact-metrics apply drops the sample label.
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(stats_summary_switches_request_set)
{
    tui::DashboardModel sampled(false);
    const std::vector<commands::CommandRequest> sampled_reqs =
        sampled.refresh_requests(id_gen());
    BOOST_TEST(command_for_panel(sampled_reqs, "redirects") == "link.list");
    for (const commands::CommandRequest& request : sampled_reqs) {
        BOOST_TEST(request.command != "stats.summary");
    }

    tui::DashboardModel exact(true);
    const std::vector<commands::CommandRequest> exact_reqs =
        exact.refresh_requests(id_gen());
    BOOST_TEST(command_for_panel(exact_reqs, "redirects") == "stats.summary");

    exact.apply("redirects",
                ok(R"({"redirects_24h":48311,"success_pct":99.2,"p50_ms":0.4,)"
                   R"("top_slugs":[{"slug":"docs","count":12044}]})"));
    BOOST_TEST(exact.redirects().sampled == false);
    BOOST_REQUIRE(exact.redirects().redirects_24h.has_value());
    BOOST_TEST(*exact.redirects().redirects_24h == 48311u);
    BOOST_REQUIRE(exact.redirects().success_pct.has_value());
    BOOST_TEST(*exact.redirects().success_pct == 99.2,
               boost::test_tools::tolerance(0.01));
    BOOST_REQUIRE(exact.redirects().p50_ms.has_value());
    BOOST_TEST(*exact.redirects().p50_ms == 0.4,
               boost::test_tools::tolerance(0.01));
    BOOST_REQUIRE_EQUAL(exact.top_slugs().size(), 1u);
    BOOST_TEST(exact.top_slugs()[0].slug == "docs");
}

// ---------------------------------------------------------------------------
// Every refresh request is a read command (success criterion 3): the request
// list contains no mutating command. Permission strings are asserted against
// the known read commands the dashboard uses.
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(all_requests_are_read_commands)
{
    // The dashboard only ever issues these; each is a `read`-permission command
    // in the catalog (link.list / job.list / system.info / db.tables / the
    // proposed read-only stats.summary).
    const auto permission_of = [](const std::string& command) -> std::string
    {
        if (command == "link.list" || command == "job.list"
            || command == "system.info" || command == "db.tables"
            || command == "stats.summary")
        {
            return "read";
        }
        return "write";  // fail closed for anything unexpected
    };

    for (const bool has_summary : {false, true}) {
        tui::DashboardModel model(has_summary);
        for (const commands::CommandRequest& request :
             model.refresh_requests(id_gen()))
        {
            BOOST_TEST(permission_of(request.command) == "read",
                       "non-read command in batch: " << request.command);
        }
    }
}

// ---------------------------------------------------------------------------
// Regression (LOW): a run of INT64_MAX-valued sample counts saturates the 24h
// sum at UINT64_MAX instead of wrapping (threat model T-121).
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(sample_sum_saturates_without_wrapping)
{
    // Each item's redirects_24h is clamped to INT64_MAX; three of them exceed
    // UINT64_MAX, so a wrapping accumulator would yield a small number.
    const std::string big = std::to_string(
        static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()));
    tui::DashboardModel model(false);
    model.apply("redirects",
                ok(R"({"items":[)"
                   R"({"slug":"a","stats":{"redirects_24h":)"
                   + big
                   + R"(}},)"
                     R"({"slug":"b","stats":{"redirects_24h":)"
                   + big
                   + R"(}},)"
                     R"({"slug":"c","stats":{"redirects_24h":)"
                   + big
                   + R"(}})"
                     R"(],"total":3})"));

    BOOST_REQUIRE(model.redirects().redirects_24h.has_value());
    BOOST_TEST(*model.redirects().redirects_24h
               == std::numeric_limits<std::uint64_t>::max());
}

// ---------------------------------------------------------------------------
// Regression (LOW): a running job whose `done` is huge (but below `total`)
// yields a sane 0..100 percentage rather than a wrapped value.
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(jobs_line_percentage_never_wraps)
{
    // done is above UINT64_MAX/100 (the overflow threshold) but below total.
    tui::DashboardModel model(false);
    model.apply("jobs",
                ok(R"({"items":[{"command":"link.import","state":"running",)"
                   R"("progress":{"done":9000000000000000000,)"
                   R"("total":9223372036854775807}}],"total":1})"));

    const std::string line = model.jobs_line();
    // Extract the "<pct>%)" number and assert it is a sane percentage.
    const std::string::size_type pct_end = line.find("%)");
    BOOST_REQUIRE(pct_end != std::string::npos);
    std::string::size_type start = pct_end;
    while (start > 0
           && std::isdigit(static_cast<unsigned char>(line[start - 1])))
    {
        --start;
    }
    const int pct = std::stoi(line.substr(start, pct_end - start));
    BOOST_TEST(pct >= 0);
    BOOST_TEST(pct <= 100);
}

// ---------------------------------------------------------------------------
// Regression (INFO): unsorted stats.summary top_slugs are ranked descending so
// the first entry (the bar max) is the largest and scales a full-width bar.
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(stats_summary_top_slugs_are_ranked)
{
    tui::DashboardModel model(true);
    model.apply("redirects",
                ok(R"({"redirects_24h":100,"top_slugs":[)"
                   R"({"slug":"promo","count":4112},)"
                   R"({"slug":"docs","count":12044},)"
                   R"({"slug":"launch","count":8730})"
                   R"(]})"));

    const std::vector<tui::TopSlug>& top = model.top_slugs();
    BOOST_REQUIRE_EQUAL(top.size(), 3u);
    BOOST_TEST(top[0].slug == "docs");
    BOOST_TEST(top[0].count == 12044u);
    BOOST_TEST(top[1].slug == "launch");
    BOOST_TEST(top[2].slug == "promo");
    // The largest entry (the bar max) fills the full width.
    BOOST_TEST(model.bars(top[0].count, top[0].count, 8, true).size() == 8u);
}
