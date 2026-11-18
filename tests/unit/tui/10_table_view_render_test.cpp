#define BOOST_TEST_MODULE TuiTableViewRenderTest

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include <boost/test/unit_test.hpp>

#include <ftxui/component/component_base.hpp>
#include <ftxui/component/event.hpp>

#include <url_shortener/tui/table_model.hpp>
#include <url_shortener/tui/table_view.hpp>
#include <url_shortener/tui/text_fit.hpp>
#include <url_shortener/tui/theme.hpp>

#include "golden.hpp"
#include "hostile_strings.hpp"
#include "render_helpers.hpp"

/**
 * @file 10_table_view_render_test.cpp
 * @brief Golden and T-110 render tests for the D3/D6/D8 table view (06.0/10).
 *
 * Type: unit (render/snapshot, links url_shortener_tui + FTXUI).
 * Scenario: the D3 body (5 columns, 4 rows incl. a truncated long URL, footer
 *           `page 1/61  (20 per page)`) matches its golden; the loading / empty
 *           / error states, the horizontal-scroll `< more` / `more >` markers,
 *           the mono (ASCII) theme and the column chooser each match a golden;
 *           a zero-visible-column layout is impossible; T-110 - a hostile cell
 *           never delivers a display hazard and the layout never exceeds the
 *           terminal width.
 * Boundaries: 80x12, 80x24 and 120x30 widths; every hostile sample as a cell.
 * On failure first check: whether fit_cell still sanitizes before truncating,
 *           and whether the goldens were regenerated with UPDATE_GOLDEN=1.
 */

using url_shortener::tui::ColumnDef;
using url_shortener::tui::display_width;
using url_shortener::tui::make_table_view;
using url_shortener::tui::PageResult;
using url_shortener::tui::TableModel;
using url_shortener::tui::TableViewOptions;
using url_shortener::tui::Theme;
using url_shortener::tui::theme_for;
using url_shortener::tui::ThemeName;
using url_shortener::tui::testing::assert_matches_golden;
using url_shortener::tui::testing::has_display_hazard;
using url_shortener::tui::testing::hostile_samples;
using url_shortener::tui::testing::render_to_text;
using url_shortener::tui::testing::send_events;

namespace
{

// The measured-width approach needs one frame to learn the box; the goldens use
// the second (stable) frame, mirroring the subtask's "render twice" note.
std::string render_twice(ftxui::Component view, int width, int height)
{
    render_to_text(view, width, height);
    return render_to_text(view, width, height);
}

std::vector<std::string> split(const std::string& text)
{
    std::vector<std::string> lines;
    std::string line;
    for (char ch : text)
    {
        if (ch == '\n')
        {
            lines.push_back(line);
            line.clear();
        }
        else
        {
            line += ch;
        }
    }
    lines.push_back(line);
    return lines;
}

std::vector<ColumnDef> d3_columns()
{
    return {
        ColumnDef {"slug", "SLUG", 4, 12, true, true, false},
        ColumnDef {"target", "TARGET URL", 8, 40, false, true, false},
        ColumnDef {"status", "STATUS", 6, 10, false, true, false},
        ColumnDef {"clicks", "CLICKS", 4, 8, true, true, true},
        ColumnDef {"expires", "EXPIRES", 7, 10, false, true, false},
    };
}

PageResult d3_page()
{
    PageResult page;
    page.total = 1204;
    page.rows = {
        {"docs", "https://example.com/docs/getting-started", "active", "12044",
         "-"},
        {"launch",
         "https://example.com/launch?utm_source=twitter&utm_campaign=autumn",
         "active", "8730", "2026-12"},
        {"promo", "https://shop.example.com/promo", "disabled", "4112", "-"},
        {"old-blog", "https://example.com/blog/2019/hello", "deleted", "7", "-"},
    };
    for (std::size_t i = 0; i < page.rows.size(); ++i)
    {
        page.raw_rows.push_back("{}");
    }
    return page;
}

// Builds a model primed with @p page and its generation applied.
TableModel primed(std::vector<ColumnDef> cols, PageResult page)
{
    TableModel model(std::move(cols), 20);
    std::uint64_t gen = 0;
    model.set_query_handler(
        [&gen](const auto&, std::uint64_t g) { gen = g; });
    model.refresh();
    model.apply_result(gen, std::move(page));
    return model;
}

// Columns c00..c11 of fixed 8 width, to force horizontal overflow at width 80.
std::vector<ColumnDef> many_columns()
{
    std::vector<ColumnDef> cols;
    for (int i = 0; i < 12; ++i)
    {
        const std::string n =
            (i < 10 ? "c0" : "c1") + std::to_string(i % 10);
        cols.push_back(ColumnDef {n, n, 8, 8, false, true, false});
    }
    return cols;
}

PageResult many_page()
{
    PageResult page;
    page.total = 3;
    for (int r = 0; r < 3; ++r)
    {
        std::vector<std::string> row;
        for (int c = 0; c < 12; ++c)
        {
            row.push_back("r" + std::to_string(r) + "c" + std::to_string(c));
        }
        page.rows.push_back(row);
        page.raw_rows.push_back("{}");
    }
    return page;
}

TableViewOptions dark_options(std::string title)
{
    TableViewOptions options;
    options.title = std::move(title);
    options.theme = &theme_for(ThemeName::dark);
    return options;
}

}  // namespace

// ---------------------------------------------------------------------------
// The D3 body (5 columns, 4 rows, truncated URL, footer page 1/61) golden.
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(d3_body_matches_golden)
{
    TableModel model = primed(d3_columns(), d3_page());
    ftxui::Component view = make_table_view(model, dark_options("Links"));
    const std::string actual = render_twice(view, 80, 24);

    BOOST_TEST(actual.find("page 1/61  (20 per page)") != std::string::npos);
    BOOST_TEST(actual.find(">docs") != std::string::npos);  // selection marker
    assert_matches_golden(actual, "d03_table_20x_80x24.txt");
}

// ---------------------------------------------------------------------------
// page_footer is a pure model-derived string.
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(page_footer_string)
{
    TableModel model = primed(d3_columns(), d3_page());
    BOOST_TEST(url_shortener::tui::page_footer(model) ==
               "page 1/61  (20 per page)");
}

// ---------------------------------------------------------------------------
// Loading / empty / error placeholder states.
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(loading_state_golden)
{
    TableModel model(d3_columns(), 20);
    std::uint64_t gen = 0;
    model.set_query_handler([&gen](const auto&, std::uint64_t g) { gen = g; });
    model.refresh();  // -> loading, no rows
    ftxui::Component view = make_table_view(model, dark_options("Links"));
    const std::string actual = render_twice(view, 80, 12);
    BOOST_TEST(actual.find("loading...") != std::string::npos);
    assert_matches_golden(actual, "table_loading_80x12.txt");
}

BOOST_AUTO_TEST_CASE(empty_state_golden)
{
    TableModel model(d3_columns(), 20);
    std::uint64_t gen = 0;
    model.set_query_handler([&gen](const auto&, std::uint64_t g) { gen = g; });
    model.refresh();
    model.apply_result(gen, PageResult {});  // total 0 -> empty
    ftxui::Component view = make_table_view(model, dark_options("Links"));
    const std::string actual = render_twice(view, 80, 12);
    BOOST_TEST(actual.find("no rows") != std::string::npos);
    assert_matches_golden(actual, "table_empty_80x12.txt");
}

BOOST_AUTO_TEST_CASE(error_state_golden)
{
    TableModel model(d3_columns(), 20);
    std::uint64_t gen = 0;
    model.set_query_handler([&gen](const auto&, std::uint64_t g) { gen = g; });
    model.refresh();
    model.apply_error(gen, "backend unavailable");
    ftxui::Component view = make_table_view(model, dark_options("Links"));
    const std::string actual = render_twice(view, 80, 12);
    BOOST_TEST(actual.find("! backend unavailable") != std::string::npos);
    BOOST_TEST(actual.find("r retry") != std::string::npos);
    assert_matches_golden(actual, "table_error_80x12.txt");
}

// ---------------------------------------------------------------------------
// Horizontal scroll: left edge shows `more >`, far right shows `< more`.
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(hscroll_left_golden)
{
    TableModel model = primed(many_columns(), many_page());
    ftxui::Component view = make_table_view(model, dark_options("Tables"));
    const std::string actual = render_twice(view, 80, 12);
    BOOST_TEST(actual.find("more >") != std::string::npos);
    assert_matches_golden(actual, "table_hscroll_left_80x12.txt");
}

BOOST_AUTO_TEST_CASE(hscroll_right_golden)
{
    TableModel model = primed(many_columns(), many_page());
    ftxui::Component view = make_table_view(model, dark_options("Tables"));
    // Scroll to the far right so columns are hidden on the left.
    for (int i = 0; i < 12; ++i)
    {
        send_events(view, {ftxui::Event::Character(">")});
    }
    const std::string actual = render_twice(view, 80, 12);
    BOOST_TEST(actual.find("< more") != std::string::npos);
    assert_matches_golden(actual, "table_hscroll_right_80x12.txt");
}

// ---------------------------------------------------------------------------
// Mono theme body is ASCII-only and matches its golden.
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(mono_theme_golden)
{
    TableModel model = primed(d3_columns(), d3_page());
    TableViewOptions options;
    options.title = "Links";
    options.theme = &theme_for(ThemeName::mono);
    ftxui::Component view = make_table_view(model, std::move(options));
    const std::string actual = render_twice(view, 80, 12);

    for (unsigned char byte : actual)
    {
        BOOST_TEST(byte <= 0x7Eu,
                   "non-ASCII byte in the mono table: "
                       << static_cast<int>(byte));
    }
    assert_matches_golden(actual, "table_mono_80x12.txt");
}

// ---------------------------------------------------------------------------
// Column chooser modal golden.
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(column_chooser_golden)
{
    TableModel model = primed(d3_columns(), d3_page());
    ftxui::Component view = make_table_view(model, dark_options("Links"));
    send_events(view, {ftxui::Event::Character("c")});
    const std::string actual = render_twice(view, 80, 24);
    BOOST_TEST(actual.find("Columns") != std::string::npos);
    assert_matches_golden(actual, "table_column_chooser_80x24.txt");
}

// ---------------------------------------------------------------------------
// Horizontal scroll reveals every column of the 12-column fixture at width 80.
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(hscroll_reveals_all_columns)
{
    TableModel model = primed(many_columns(), many_page());
    ftxui::Component view = make_table_view(model, dark_options("Tables"));

    bool c00_seen = false;
    bool c11_seen = false;
    for (int step = 0; step < 12; ++step)
    {
        const std::string frame = render_twice(view, 80, 12);
        if (frame.find("c00") != std::string::npos)
        {
            c00_seen = true;
        }
        if (frame.find("c11") != std::string::npos)
        {
            c11_seen = true;
        }
        send_events(view, {ftxui::Event::Character(">")});
    }
    BOOST_TEST(c00_seen);
    BOOST_TEST(c11_seen);
}

// ---------------------------------------------------------------------------
// At least one column always remains visible (zero columns is impossible).
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(at_least_one_column_stays_visible)
{
    TableModel model = primed(d3_columns(), d3_page());
    for (std::size_t i = 0; i < d3_columns().size(); ++i)
    {
        model.toggle_column(i);  // the last visible column refuses to hide
    }
    BOOST_TEST(model.shown_columns(79).size() >= 1u);

    ftxui::Component view = make_table_view(model, dark_options("Links"));
    const std::string actual = render_twice(view, 80, 12);
    BOOST_TEST(!actual.empty());
}

// ---------------------------------------------------------------------------
// 120x30 width: wider layout, no line exceeds the terminal width, header shown.
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(wide_layout_within_width)
{
    TableModel model = primed(d3_columns(), d3_page());
    ftxui::Component view = make_table_view(model, dark_options("Links"));
    const std::string actual = render_twice(view, 120, 30);

    BOOST_TEST(actual.find("SLUG") != std::string::npos);
    for (const std::string& line : split(actual))
    {
        BOOST_TEST(display_width(line) <= 120u,
                   "line exceeds terminal width: " << line);
    }
}

// ---------------------------------------------------------------------------
// T-110: every hostile sample placed in a cell renders with no display hazard
// and never widens the layout past the terminal.
// On failure first check: fit_cell sanitizes before truncation in table_view.
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(hostile_cell_never_reaches_grid)
{
    for (const auto& sample : hostile_samples())
    {
        PageResult page;
        page.total = 1;
        page.rows = {{sample.raw, "https://example.com/x", "active", "1", "-"}};
        page.raw_rows = {"{}"};

        TableModel model = primed(d3_columns(), std::move(page));
        ftxui::Component view = make_table_view(model, dark_options("Links"));
        const std::string grid = render_twice(view, 80, 12);

        BOOST_TEST(grid.find('\x1b') == std::string::npos,
                   "ESC reached the grid for sample: " << sample.name);
        for (const std::string& line : split(grid))
        {
            BOOST_TEST(!has_display_hazard(line),
                       "display hazard for sample: " << sample.name);
            BOOST_TEST(display_width(line) <= 80u,
                       "line exceeds width for sample: " << sample.name);
        }
    }
}
