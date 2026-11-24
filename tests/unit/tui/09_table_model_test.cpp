#define BOOST_TEST_MODULE TuiTableModelTest

#include <cstddef>
#include <cstdint>
#include <numeric>
#include <optional>
#include <ostream>
#include <string>
#include <vector>

#include <boost/test/unit_test.hpp>

#include <url_shortener/tui/table_model.hpp>

namespace url_shortener::tui
{
// Lets Boost.Test print a LoadState on a failed comparison (ADL-found).
inline std::ostream& operator<<(std::ostream& os, LoadState state)
{
    return os << static_cast<int>(state);
}
}  // namespace url_shortener::tui

using url_shortener::tui::ColumnDef;
using url_shortener::tui::LoadState;
using url_shortener::tui::PageQuery;
using url_shortener::tui::PageResult;
using url_shortener::tui::TableModel;

namespace
{

// Records every query the model emits so tests can assert count and content.
struct QuerySpy
{
    std::vector<PageQuery> queries;
    std::vector<std::uint64_t> generations;

    void attach(TableModel& model)
    {
        model.set_query_handler(
            [this](const PageQuery& q, std::uint64_t gen)
            {
                queries.push_back(q);
                generations.push_back(gen);
            });
    }

    std::size_t count() const { return queries.size(); }
    const PageQuery& last() const { return queries.back(); }
    std::uint64_t last_gen() const { return generations.back(); }
};

std::vector<ColumnDef> three_columns()
{
    return {
        ColumnDef{"slug", "SLUG", 4, 20, true, true, false},
        ColumnDef{"target", "TARGET", 8, 40, false, true, false},
        ColumnDef{"clicks", "CLICKS", 4, 10, true, true, true},
    };
}

// Builds a page of @p n rows with a given total.
PageResult make_page(std::size_t n, std::size_t total)
{
    PageResult page;
    page.total = total;
    for (std::size_t i = 0; i < n; ++i)
    {
        const std::string tag = std::to_string(i);
        page.rows.push_back({"slug" + tag, "https://x/" + tag, tag});
        page.raw_rows.push_back("{\"slug\":\"slug" + tag + "\"}");
    }
    return page;
}

}  // namespace

// ---------------------------------------------------------------------------
// Page arithmetic
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(page_count_for_various_totals)
{
    TableModel model(three_columns(), 20);
    QuerySpy spy;
    spy.attach(model);

    const std::size_t totals[] = {0u, 1u, 20u, 21u, 1204u};
    const std::size_t expected[] = {1u, 1u, 1u, 2u, 61u};
    for (std::size_t i = 0; i < 5; ++i)
    {
        model.refresh();
        BOOST_REQUIRE(model.apply_result(spy.last_gen(),
                                         make_page(0, totals[i])));
        BOOST_TEST_INFO("total=" << totals[i]);
        BOOST_TEST(model.page_count() == expected[i]);
    }
}

BOOST_AUTO_TEST_CASE(d3_scenario_1204_rows_is_page_1_of_61)
{
    TableModel model(three_columns(), 20);
    QuerySpy spy;
    spy.attach(model);
    model.refresh();
    BOOST_REQUIRE(model.apply_result(spy.last_gen(), make_page(20, 1204)));

    BOOST_TEST(model.page_index() == 0u);
    BOOST_TEST(model.page_count() == 61u);
    BOOST_TEST(model.total() == 1204u);
}

BOOST_AUTO_TEST_CASE(last_page_offset_is_last_full_window)
{
    TableModel model(three_columns(), 20);
    QuerySpy spy;
    spy.attach(model);
    model.refresh();
    BOOST_REQUIRE(model.apply_result(spy.last_gen(), make_page(20, 1204)));

    model.last_page();
    BOOST_TEST(model.query().offset == 60u * 20u);
    BOOST_TEST(model.page_index() == 60u);
}

BOOST_AUTO_TEST_CASE(next_page_at_last_emits_no_query)
{
    TableModel model(three_columns(), 20);
    QuerySpy spy;
    spy.attach(model);
    model.refresh();  // query 1
    BOOST_REQUIRE(model.apply_result(spy.last_gen(), make_page(20, 40)));

    model.next_page();  // query 2 (page 2 of 2)
    BOOST_REQUIRE(model.apply_result(spy.last_gen(), make_page(20, 40)));
    const std::size_t before = spy.count();

    model.next_page();  // already last -> no-op
    BOOST_TEST(spy.count() == before);
}

BOOST_AUTO_TEST_CASE(first_and_prev_page_at_start_emit_no_query)
{
    TableModel model(three_columns(), 20);
    QuerySpy spy;
    spy.attach(model);
    model.refresh();
    BOOST_REQUIRE(model.apply_result(spy.last_gen(), make_page(20, 100)));
    const std::size_t before = spy.count();

    model.prev_page();   // page 1 already -> no-op
    model.first_page();  // page 1 already -> no-op
    BOOST_TEST(spy.count() == before);
}

// ---------------------------------------------------------------------------
// Sorting
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(toggle_sort_cycles_asc_then_desc)
{
    TableModel model(three_columns(), 20);
    QuerySpy spy;
    spy.attach(model);

    model.toggle_sort(0);  // sortable slug: none -> asc
    BOOST_TEST(model.query().sort_key == "slug");
    BOOST_TEST(model.query().descending == false);

    model.toggle_sort(0);  // asc -> desc
    BOOST_TEST(model.query().descending == true);

    model.toggle_sort(0);  // desc -> asc
    BOOST_TEST(model.query().descending == false);
    BOOST_TEST(spy.count() == 3u);
}

BOOST_AUTO_TEST_CASE(toggle_sort_non_sortable_is_ignored)
{
    TableModel model(three_columns(), 20);
    QuerySpy spy;
    spy.attach(model);

    model.toggle_sort(1);  // "target" is not sortable
    BOOST_TEST(spy.count() == 0u);
    BOOST_TEST(model.query().sort_key.empty());
}

BOOST_AUTO_TEST_CASE(toggle_sort_resets_offset)
{
    TableModel model(three_columns(), 20);
    QuerySpy spy;
    spy.attach(model);
    model.refresh();
    BOOST_REQUIRE(model.apply_result(spy.last_gen(), make_page(20, 1204)));
    model.last_page();
    BOOST_TEST(model.query().offset > 0u);

    model.toggle_sort(0);
    BOOST_TEST(model.query().offset == 0u);
}

// ---------------------------------------------------------------------------
// Filtering
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(set_filter_resets_offset_and_emits)
{
    TableModel model(three_columns(), 20);
    QuerySpy spy;
    spy.attach(model);
    model.refresh();
    BOOST_REQUIRE(model.apply_result(spy.last_gen(), make_page(20, 1204)));
    model.next_page();
    BOOST_REQUIRE(model.apply_result(spy.last_gen(), make_page(20, 1204)));
    const std::size_t before = spy.count();

    model.set_filter("camp:spring");
    BOOST_TEST(spy.count() == before + 1u);
    BOOST_TEST(model.query().offset == 0u);
    BOOST_TEST(model.query().filter == "camp:spring");
}

// ---------------------------------------------------------------------------
// Stale-response protection
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(stale_result_is_ignored)
{
    TableModel model(three_columns(), 20);
    QuerySpy spy;
    spy.attach(model);

    model.refresh();  // gen 1
    const std::uint64_t gen1 = spy.last_gen();
    model.set_filter("x");  // gen 2 supersedes gen 1

    // The slow gen-1 result arrives after gen 2 was emitted: ignored.
    BOOST_TEST(model.apply_result(gen1, make_page(5, 5)) == false);
    BOOST_TEST(model.rows().empty());

    // The gen-2 result is accepted.
    BOOST_TEST(model.apply_result(spy.last_gen(), make_page(3, 3)) == true);
    BOOST_TEST(model.rows().size() == 3u);
}

BOOST_AUTO_TEST_CASE(error_then_refresh_recovers)
{
    TableModel model(three_columns(), 20);
    QuerySpy spy;
    spy.attach(model);
    model.refresh();
    BOOST_REQUIRE(model.apply_result(spy.last_gen(), make_page(4, 4)));

    model.refresh();
    model.apply_error(spy.last_gen(), "backend down");
    BOOST_TEST(model.state() == LoadState::error);
    BOOST_TEST(model.error_message() == "backend down");
    BOOST_TEST(model.rows().size() == 4u);  // previous rows kept

    model.refresh();
    BOOST_REQUIRE(model.apply_result(spy.last_gen(), make_page(2, 2)));
    BOOST_TEST(model.state() == LoadState::loaded);
    BOOST_TEST(model.error_message().empty());
}

BOOST_AUTO_TEST_CASE(stale_error_is_ignored)
{
    TableModel model(three_columns(), 20);
    QuerySpy spy;
    spy.attach(model);
    model.refresh();
    const std::uint64_t gen1 = spy.last_gen();
    model.refresh();  // gen 2

    model.apply_error(gen1, "stale");
    BOOST_TEST(model.state() != LoadState::error);
}

// ---------------------------------------------------------------------------
// Load state transitions
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(empty_result_sets_empty_state)
{
    TableModel model(three_columns(), 20);
    QuerySpy spy;
    spy.attach(model);
    model.refresh();
    BOOST_REQUIRE(model.apply_result(spy.last_gen(), make_page(0, 0)));
    BOOST_TEST(model.state() == LoadState::empty);
    BOOST_TEST(!model.selected().has_value());
}

// ---------------------------------------------------------------------------
// Selection
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(selection_moves_and_clamps_within_page)
{
    TableModel model(three_columns(), 20);
    QuerySpy spy;
    spy.attach(model);
    model.refresh();
    BOOST_REQUIRE(model.apply_result(spy.last_gen(), make_page(3, 3)));

    BOOST_TEST(model.selected().value() == 0u);
    model.select_next();
    model.select_next();
    model.select_next();  // clamps at last
    BOOST_TEST(model.selected().value() == 2u);
    model.select_prev();
    BOOST_TEST(model.selected().value() == 1u);
    model.select_last();
    BOOST_TEST(model.selected().value() == 2u);
    model.select_first();
    BOOST_TEST(model.selected().value() == 0u);
}

BOOST_AUTO_TEST_CASE(selection_clamps_when_shorter_page_arrives)
{
    TableModel model(three_columns(), 20);
    QuerySpy spy;
    spy.attach(model);
    model.refresh();
    BOOST_REQUIRE(model.apply_result(spy.last_gen(), make_page(10, 100)));
    model.select_last();
    BOOST_TEST(model.selected().value() == 9u);

    // A refresh returns a shorter page; selection must clamp, not dangle.
    model.refresh();
    BOOST_REQUIRE(model.apply_result(spy.last_gen(), make_page(4, 4)));
    BOOST_TEST(model.selected().value() == 3u);
}

BOOST_AUTO_TEST_CASE(selected_raw_tracks_selection)
{
    TableModel model(three_columns(), 20);
    QuerySpy spy;
    spy.attach(model);
    model.refresh();
    BOOST_REQUIRE(model.apply_result(spy.last_gen(), make_page(3, 3)));
    model.select_next();
    BOOST_REQUIRE(model.selected_raw() != nullptr);
    BOOST_TEST(*model.selected_raw() == "{\"slug\":\"slug1\"}");
}

// ---------------------------------------------------------------------------
// Columns and horizontal scroll
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(hiding_the_last_visible_column_is_refused)
{
    TableModel model(three_columns(), 20);
    model.toggle_column(0);  // hide slug
    model.toggle_column(1);  // hide target
    // Only clicks remains; hiding it must be refused.
    model.toggle_column(2);
    BOOST_TEST(model.shown_columns(80).size() == 1u);
}

BOOST_AUTO_TEST_CASE(scroll_columns_clamps_both_ends)
{
    // A width of 10 is too narrow for all three columns, so a window exists.
    TableModel model(three_columns(), 20);
    model.scroll_columns(-5);  // clamp at 0
    BOOST_TEST(!model.overflow_left(10));
    BOOST_TEST(model.overflow_right(10));  // more columns to the right

    model.scroll_columns(100);  // clamp at last visible
    BOOST_TEST(model.overflow_left(10));    // columns to the left now
    BOOST_TEST(!model.overflow_right(10));  // nothing further right
    BOOST_TEST(model.shown_columns(10).size() >= 1u);
}

// ---------------------------------------------------------------------------
// Layout
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(layout_never_exceeds_available_width)
{
    TableModel model(three_columns(), 20);
    QuerySpy spy;
    spy.attach(model);
    model.refresh();
    BOOST_REQUIRE(model.apply_result(spy.last_gen(), make_page(20, 100)));

    for (int width = 20; width <= 200; ++width)
    {
        const std::vector<int> widths = model.layout(width);
        BOOST_REQUIRE(!widths.empty());
        const int separators = static_cast<int>(widths.size()) - 1;
        const int used =
            std::accumulate(widths.begin(), widths.end(), 0) + separators;
        BOOST_TEST_INFO("width=" << width);
        BOOST_TEST(used <= width);
    }
}

BOOST_AUTO_TEST_CASE(layout_shows_at_least_one_column_when_narrow)
{
    TableModel model(three_columns(), 20);
    BOOST_TEST(model.layout(3).size() == 1u);
    BOOST_TEST(model.layout(3).front() <= 3);
}

// ---------------------------------------------------------------------------
// Query content
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(page_size_is_clamped_to_catalog_limits)
{
    TableModel big(three_columns(), 100000);
    BOOST_TEST(big.query().limit == 500u);
    TableModel zero(three_columns(), 0);
    BOOST_TEST(zero.query().limit == 1u);
}

BOOST_AUTO_TEST_CASE(no_query_before_handler_is_set)
{
    // Constructing the model must not require a handler and must not crash.
    TableModel model(three_columns(), 20);
    BOOST_TEST(model.state() == LoadState::idle);
    model.refresh();  // handler empty: updates state, emits nothing
    BOOST_TEST(model.state() == LoadState::loading);
}
