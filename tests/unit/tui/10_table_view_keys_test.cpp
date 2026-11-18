#define BOOST_TEST_MODULE TuiTableViewKeysTest

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include <boost/test/unit_test.hpp>

#include <ftxui/component/component_base.hpp>
#include <ftxui/component/event.hpp>

#include <url_shortener/tui/table_model.hpp>
#include <url_shortener/tui/table_view.hpp>

#include "render_helpers.hpp"

/**
 * @file 10_table_view_keys_test.cpp
 * @brief Key-routing tests for the D3/D6/D8 table view (subtask 06.0/10).
 *
 * Type: unit (component, links url_shortener_tui + FTXUI).
 * Scenario: j/k/arrows/g/G move the selection without a query; PgDn/PgUp/r emit
 *           exactly one query each; `/`+text+Enter sets the filter with one
 *           query and Esc clears it; `s` sorts the sorted column; `c` opens the
 *           chooser and a toggle hides the column; the extra-key callback
 *           receives the selected row; keys are text (not nav) while the filter
 *           is focused.
 * Boundaries: query count is asserted exactly via the model's query handler.
 * On failure first check: the OnEvent routing order (chooser -> filter ->
 *           on_extra_key -> built-ins) in table_view.cpp.
 */

using url_shortener::tui::ColumnDef;
using url_shortener::tui::make_table_view;
using url_shortener::tui::PageQuery;
using url_shortener::tui::PageResult;
using url_shortener::tui::TableModel;
using url_shortener::tui::TableViewOptions;
using url_shortener::tui::testing::render_to_text;
using url_shortener::tui::testing::send_events;

namespace
{

// Counts and records every query the model emits so a test can assert the
// exact number of queries a key produces.
struct QuerySpy
{
    std::vector<PageQuery> queries;
    std::uint64_t last_gen = 0;

    void attach(TableModel& model)
    {
        model.set_query_handler(
            [this](const PageQuery& q, std::uint64_t gen)
            {
                queries.push_back(q);
                last_gen = gen;
            });
    }

    std::size_t count() const { return queries.size(); }
    const PageQuery& last() const { return queries.back(); }
};

std::vector<ColumnDef> three_columns()
{
    return {
        ColumnDef {"slug", "SLUG", 4, 20, true, true, false},
        ColumnDef {"target", "TARGET", 8, 40, false, true, false},
        ColumnDef {"clicks", "CLICKS", 4, 10, true, true, true},
    };
}

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

// Loads a fresh model with one page of @p rows of @p total already applied.
void prime(TableModel& model, QuerySpy& spy, std::size_t rows, std::size_t total)
{
    spy.attach(model);
    model.refresh();
    model.apply_result(spy.last_gen, make_page(rows, total));
}

}  // namespace

// ---------------------------------------------------------------------------
// Selection keys move the cursor and emit no query; PgDn/PgUp/r each emit one.
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(navigation_keys_map_to_model_calls)
{
    TableModel model(three_columns(), 20);
    QuerySpy spy;
    prime(model, spy, 20, 100);
    const std::size_t base = spy.count();  // 1 (the initial refresh)

    ftxui::Component view = make_table_view(model, TableViewOptions {});

    send_events(view, {ftxui::Event::Character("j")});
    BOOST_TEST(model.selected().value() == 1u);
    send_events(view, {ftxui::Event::ArrowDown});
    BOOST_TEST(model.selected().value() == 2u);
    send_events(view, {ftxui::Event::Character("k"), ftxui::Event::ArrowUp});
    BOOST_TEST(model.selected().value() == 0u);
    send_events(view, {ftxui::Event::Character("G")});
    BOOST_TEST(model.selected().value() == 19u);
    send_events(view, {ftxui::Event::Character("g")});
    BOOST_TEST(model.selected().value() == 0u);
    // None of the selection keys emitted a query.
    BOOST_TEST(spy.count() == base);

    send_events(view, {ftxui::Event::PageDown});
    BOOST_TEST(spy.count() == base + 1);
    BOOST_TEST(spy.last().offset == 20u);
    model.apply_result(spy.last_gen, make_page(20, 100));

    send_events(view, {ftxui::Event::PageUp});
    BOOST_TEST(spy.count() == base + 2);
    BOOST_TEST(spy.last().offset == 0u);
}

// ---------------------------------------------------------------------------
// `r` refresh emits exactly one query.
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(refresh_key_emits_one_query)
{
    TableModel model(three_columns(), 20);
    QuerySpy spy;
    prime(model, spy, 20, 100);
    const std::size_t base = spy.count();

    ftxui::Component view = make_table_view(model, TableViewOptions {});
    send_events(view, {ftxui::Event::Character("r")});
    BOOST_TEST(spy.count() == base + 1);
}

// ---------------------------------------------------------------------------
// `/` + text + Enter sets the filter and emits exactly one query; Esc clears.
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(filter_sets_one_query_on_enter_and_esc_clears)
{
    TableModel model(three_columns(), 20);
    QuerySpy spy;
    prime(model, spy, 20, 100);
    const std::size_t base = spy.count();

    ftxui::Component view = make_table_view(model, TableViewOptions {});

    send_events(view, {ftxui::Event::Character("/"),
                       ftxui::Event::Character("a"),
                       ftxui::Event::Character("b")});
    // Typing emits nothing (no per-keystroke query flood).
    BOOST_TEST(spy.count() == base);

    send_events(view, {ftxui::Event::Return});
    BOOST_TEST(spy.count() == base + 1);
    BOOST_TEST(model.query().filter == "ab");

    // Focus again and clear with Esc: exactly one more query, empty filter.
    send_events(view, {ftxui::Event::Character("/"), ftxui::Event::Escape});
    BOOST_TEST(spy.count() == base + 2);
    BOOST_TEST(model.query().filter.empty());
}

// ---------------------------------------------------------------------------
// `s` toggles the sort on the sorted (or first sortable) column.
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(sort_key_toggles_sorted_column)
{
    TableModel model(three_columns(), 20);
    QuerySpy spy;
    prime(model, spy, 20, 100);

    TableViewOptions options;
    options.sort_key_enabled = true;
    ftxui::Component view = make_table_view(model, std::move(options));

    send_events(view, {ftxui::Event::Character("s")});
    BOOST_TEST(model.query().sort_key == "slug");
    BOOST_TEST(model.query().descending == false);

    // A second `s` flips direction on the same column.
    send_events(view, {ftxui::Event::Character("s")});
    BOOST_TEST(model.query().sort_key == "slug");
    BOOST_TEST(model.query().descending == true);
}

// ---------------------------------------------------------------------------
// `c` opens the chooser; toggling a checkbox hides that column next render.
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(column_chooser_hides_a_column)
{
    TableModel model(three_columns(), 20);
    QuerySpy spy;
    prime(model, spy, 20, 100);

    ftxui::Component view = make_table_view(model, TableViewOptions {});

    BOOST_TEST(render_to_text(view, 80, 12).find("SLUG") != std::string::npos);

    // Open chooser, toggle the first checkbox (SLUG), close.
    send_events(view, {ftxui::Event::Character("c"),
                       ftxui::Event::Character(" "), ftxui::Event::Return});

    const std::string after = render_to_text(view, 80, 12);
    BOOST_TEST(after.find("SLUG") == std::string::npos);
    BOOST_TEST(after.find("TARGET") != std::string::npos);
}

// ---------------------------------------------------------------------------
// The extra-key callback is consulted before built-ins and receives the row.
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(extra_key_callback_receives_selected_row)
{
    TableModel model(three_columns(), 20);
    QuerySpy spy;
    prime(model, spy, 20, 100);

    std::optional<std::size_t> seen;
    TableViewOptions options;
    options.on_extra_key =
        [&seen](const ftxui::Event& event, std::optional<std::size_t> row)
    {
        if (event == ftxui::Event::Character("x"))
        {
            seen = row;
            return true;
        }
        return false;
    };
    ftxui::Component view = make_table_view(model, std::move(options));

    send_events(view, {ftxui::Event::Character("j"),
                       ftxui::Event::Character("j")});
    BOOST_TEST(model.selected().value() == 2u);
    send_events(view, {ftxui::Event::Character("x")});
    BOOST_REQUIRE(seen.has_value());
    BOOST_TEST(seen.value() == 2u);
}

// ---------------------------------------------------------------------------
// While the filter input is focused, navigation keys are text, not movement.
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(keys_ignored_while_filter_focused)
{
    TableModel model(three_columns(), 20);
    QuerySpy spy;
    prime(model, spy, 20, 100);

    ftxui::Component view = make_table_view(model, TableViewOptions {});

    send_events(view, {ftxui::Event::Character("/"),
                       ftxui::Event::Character("j")});
    // 'j' became text; the selection did not advance.
    BOOST_TEST(model.selected().value() == 0u);
    BOOST_TEST(render_to_text(view, 80, 12).find("[j]") != std::string::npos);
}
