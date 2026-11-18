#define BOOST_TEST_MODULE TuiTablesModelTest

#include <cstdint>
#include <string>
#include <vector>

#include <boost/test/unit_test.hpp>
#include <url_shortener/commands/envelope.hpp>
#include <url_shortener/core/utils.hpp>
#include <url_shortener/tui/table_model.hpp>
#include <url_shortener/tui/tables_model.hpp>
#include <url_shortener/tui/text_fit.hpp>

#include "hostile_strings.hpp"

/**
 * @file 14_tables_model_test.cpp
 * @brief Tables browser view-model tests (D6, 06.0/14): db.tables parsing,
 *        db.rows request building, column derivation and the secret-name
 * filter.
 *
 * Type: unit (pure model, links url_shortener_tui_model; no FTXUI).
 * Scenario: apply_tables parses a real-shaped three-table db.tables result;
 *   rows_request encodes the default / filtered / sorted page bytes; can_insert
 *   tracks writable; thousands grouping and width derivation match D6; a
 *   secret-named column (password_hash / api_token / salt) is dropped while
 *   client_id_hash survives; empty and malformed input yield an empty list.
 * Boundaries: writable vs read-only; sort set vs unset; q empty vs set; secret
 *   vs client_id_hash; selection present vs absent.
 * On failure first check: TablesModel::apply_tables parsing,
 *   TablesModel::rows_request arg assembly, and columns_for_selected filtering.
 */

namespace tui = url_shortener::tui;
namespace commands = url_shortener::commands;

using tui::testing::has_display_hazard;
using tui::testing::hostile_samples;

namespace
{

/// A real-shaped three-table db.tables result: links (writable), click_events
/// and jobs (both read-only). Mirrors catalog 3.2.
std::string three_tables()
{
    return R"({"tables":[)"
           R"({"name":"links","rows":1204,"writable":true,"columns":[)"
           R"({"name":"id","type":"string","sortable":false,"searchable":false},)"
           R"({"name":"slug","type":"string","sortable":true,"searchable":true},)"
           R"({"name":"url","type":"string","sortable":false,"searchable":true},)"
           R"({"name":"created_at","type":"timestamp","sortable":true,"searchable":false})"
           R"(]},)"
           R"({"name":"click_events","rows":912550,"writable":false,"columns":[)"
           R"({"name":"occurred_at","type":"timestamp","sortable":true,"searchable":false},)"
           R"({"name":"status_code","type":"int","sortable":true,"searchable":false},)"
           R"({"name":"client_id_hash","type":"string","sortable":false,"searchable":false})"
           R"(]},)"
           R"({"name":"jobs","rows":3,"writable":false,"columns":[)"
           R"({"name":"id","type":"string","sortable":false,"searchable":false},)"
           R"({"name":"state","type":"string","sortable":false,"searchable":false})"
           R"(]})"
           R"(]})";
}

}  // namespace

// ---------------------------------------------------------------------------
// apply_tables parses the three-table shape with counts, writability, columns.
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(apply_tables_parses_three)
{
    tui::TablesModel model;
    model.apply_tables(three_tables());

    BOOST_REQUIRE_EQUAL(model.tables().size(), 3u);
    BOOST_TEST(model.tables()[0].name == "links");
    BOOST_TEST(model.tables()[0].rows == 1204u);
    BOOST_TEST(model.tables()[0].writable);
    BOOST_TEST(model.tables()[1].name == "click_events");
    BOOST_TEST(model.tables()[1].rows == 912550u);
    BOOST_TEST(!model.tables()[1].writable);
    BOOST_TEST(model.tables()[2].name == "jobs");
    BOOST_TEST(model.tables()[2].rows == 3u);

    // First table selected by default.
    BOOST_REQUIRE(model.selected().has_value());
    BOOST_TEST(*model.selected() == 0u);
    // Columns carry sortable/searchable from db.tables.
    BOOST_REQUIRE_EQUAL(model.tables()[0].columns.size(), 4u);
    BOOST_TEST(model.tables()[0].columns[1].name == "slug");
    BOOST_TEST(model.tables()[0].columns[1].sortable);
    BOOST_TEST(model.tables()[0].columns[1].searchable);
}

// ---------------------------------------------------------------------------
// can_insert is true only for the writable (links) table.
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(can_insert_tracks_writable)
{
    tui::TablesModel model;
    model.apply_tables(three_tables());

    model.select(0);  // links (writable)
    BOOST_TEST(model.can_insert());
    model.select(1);  // click_events (read-only)
    BOOST_TEST(!model.can_insert());
    model.select(2);  // jobs (read-only)
    BOOST_TEST(!model.can_insert());
}

// ---------------------------------------------------------------------------
// rows_request: default, with q, with sort desc, and a paged offset.
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(rows_request_bytes)
{
    tui::TablesModel model;
    model.apply_tables(three_tables());  // links selected

    tui::PageQuery query;  // offset 0, limit 20, no sort, no filter
    BOOST_TEST(model.rows_request(query, "tui-1").command == "db.rows");
    BOOST_TEST(model.rows_request(query, "tui-1").args_json
               == R"({"table":"links","offset":0,"limit":20})");

    query.filter = "foo";
    BOOST_TEST(model.rows_request(query, "tui-1").args_json
               == R"({"table":"links","q":"foo","offset":0,"limit":20})");

    tui::PageQuery sorted;
    sorted.sort_key = "slug";
    sorted.descending = true;
    BOOST_TEST(
        model.rows_request(sorted, "tui-1").args_json
        == R"({"table":"links","sort":"slug","order":"desc","offset":0,"limit":20})");

    tui::PageQuery asc;
    asc.sort_key = "created_at";
    asc.descending = false;
    asc.offset = 40;
    BOOST_TEST(
        model.rows_request(asc, "tui-1").args_json
        == R"({"table":"links","sort":"created_at","order":"asc","offset":40,"limit":20})");
}

// ---------------------------------------------------------------------------
// Columns are derived with sensible widths and carry the sortable flag.
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(columns_derived_with_widths)
{
    tui::TablesModel model;
    model.apply_tables(three_tables());
    model.select(0);  // links

    const std::vector<tui::ColumnDef> cols = model.columns_for_selected();
    BOOST_REQUIRE_EQUAL(cols.size(), 4u);
    BOOST_TEST(cols[0].key == "id");
    BOOST_TEST(cols[0].max_width == 12);  // id-like
    BOOST_TEST(cols[1].key == "slug");
    BOOST_TEST(cols[1].sortable);
    BOOST_TEST(cols[2].key == "url");
    BOOST_TEST(cols[2].max_width == 40);  // url
    BOOST_TEST(cols[3].key == "created_at");
    BOOST_TEST(cols[3].max_width == 20);  // timestamp
    BOOST_TEST(cols[3].sortable);

    // click_events: status_code is an int column (right-aligned, width 12).
    model.select(1);
    const std::vector<tui::ColumnDef> ev = model.columns_for_selected();
    bool found_int = false;
    for (const tui::ColumnDef& c : ev) {
        if (c.key == "status_code") {
            found_int = true;
            BOOST_TEST(c.right_align);
            BOOST_TEST(c.max_width == 12);
        }
    }
    BOOST_TEST(found_int);
}

// ---------------------------------------------------------------------------
// Thousands grouping in the left-pane rows.
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(left_pane_row_thousands)
{
    tui::TablesModel model;
    model.apply_tables(three_tables());

    // links is row 0 and selected, so the marker is `>`.
    BOOST_TEST(model.left_pane_row(0)
               == std::string("> links") + std::string(11, ' ') + "1,204");
    // click_events (unselected) carries a leading space marker and a grouped
    // count.
    BOOST_TEST(model.left_pane_row(1).find("912,550") != std::string::npos);
    BOOST_TEST(model.left_pane_row(1).substr(0, 2) == "  ");
    // jobs: small count, no separator.
    BOOST_TEST(model.left_pane_row(2).find("3") != std::string::npos);
}

// ---------------------------------------------------------------------------
// Secret-named columns are filtered; client_id_hash survives (defence in
// depth, success criterion 4).
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(secret_columns_filtered)
{
    BOOST_TEST(tui::TablesModel::is_secret_column_name("password_hash"));
    BOOST_TEST(tui::TablesModel::is_secret_column_name("api_token"));
    BOOST_TEST(tui::TablesModel::is_secret_column_name("salt"));
    BOOST_TEST(tui::TablesModel::is_secret_column_name("db_dsn"));
    BOOST_TEST(!tui::TablesModel::is_secret_column_name("client_id_hash"));
    BOOST_TEST(!tui::TablesModel::is_secret_column_name("slug"));

    const std::string hostile =
        R"({"tables":[{"name":"secrets","rows":1,"writable":false,"columns":[)"
        R"({"name":"slug","type":"string","sortable":false,"searchable":false},)"
        R"({"name":"password_hash","type":"string","sortable":false,"searchable":false},)"
        R"({"name":"api_token","type":"string","sortable":false,"searchable":false},)"
        R"({"name":"salt","type":"string","sortable":false,"searchable":false},)"
        R"({"name":"client_id_hash","type":"string","sortable":false,"searchable":false})"
        R"(]}]})";

    tui::TablesModel model;
    model.apply_tables(hostile);
    model.select(0);

    const std::vector<tui::ColumnDef> cols = model.columns_for_selected();
    BOOST_REQUIRE_EQUAL(cols.size(), 2u);  // slug + client_id_hash only
    BOOST_TEST(cols[0].key == "slug");
    BOOST_TEST(cols[1].key == "client_id_hash");
    BOOST_TEST(model.hidden_secret_columns() == 3u);
    for (const tui::ColumnDef& c : cols) {
        BOOST_TEST(c.key != "password_hash");
        BOOST_TEST(c.key != "api_token");
        BOOST_TEST(c.key != "salt");
    }
}

// ---------------------------------------------------------------------------
// redact_secret_fields projects secret-named keys out of a row's JSON, keeping
// the row-JSON view symmetric with the column filter (security review LOW).
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(redact_secret_fields_projects_out_secrets)
{
    std::size_t hidden = 0;
    const std::string row =
        R"({"slug":"home","password_hash":"abc","api_token":"xyz",)"
        R"("client_id_hash":"h1","url":"https://x/"})";
    const std::string out =
        tui::TablesModel::redact_secret_fields(row, &hidden);
    BOOST_TEST(hidden == 2u);
    BOOST_TEST(out.find("password_hash") == std::string::npos);
    BOOST_TEST(out.find("abc") == std::string::npos);
    BOOST_TEST(out.find("api_token") == std::string::npos);
    BOOST_TEST(out.find("xyz") == std::string::npos);
    BOOST_TEST(out.find("client_id_hash") != std::string::npos);
    BOOST_TEST(out.find("h1") != std::string::npos);
    BOOST_TEST(out.find("slug") != std::string::npos);

    // No secret field -> byte-identical to the compact source (criterion 2).
    std::size_t none = 9;
    const std::string clean = R"({"slug":"home","url":"https://x/"})";
    BOOST_TEST(tui::TablesModel::redact_secret_fields(clean, &none) == clean);
    BOOST_TEST(none == 0u);

    // A non-object row is returned unchanged.
    std::size_t z = 9;
    BOOST_TEST(tui::TablesModel::redact_secret_fields("not json", &z)
               == "not json");
    BOOST_TEST(z == 0u);
}

// ---------------------------------------------------------------------------
// Empty and malformed db.tables leave an empty, selection-less model.
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(empty_and_malformed)
{
    tui::TablesModel model;
    model.apply_tables(R"({"tables":[]})");
    BOOST_TEST(model.tables().empty());
    BOOST_TEST(!model.selected().has_value());
    BOOST_TEST(!model.can_insert());
    BOOST_TEST(model.columns_for_selected().empty());
    BOOST_TEST(model.rows_request(tui::PageQuery {}, "tui-1").args_json
               == R"({"table":"","offset":0,"limit":20})");

    model.apply_tables("not json");
    BOOST_TEST(model.tables().empty());
}

// ---------------------------------------------------------------------------
// T-110: a hostile table name sanitizes clean once passed through the sink.
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(hostile_table_name_sanitizes_clean)
{
    for (const auto& sample : hostile_samples()) {
        BOOST_TEST_CONTEXT("sample=" << sample.name)
        {
            const std::string json = R"({"tables":[{"name":)"
                + url_shortener::jsonString(sample.raw)
                + R"(,"rows":5,"writable":false,"columns":[)"
                  R"({"name":)"
                + url_shortener::jsonString(sample.raw)
                + R"(,"type":"string","sortable":false,"searchable":false}]}]})";
            tui::TablesModel model;
            model.apply_tables(json);
            model.select(0);
            // The model does not sanitize; the render sink does. Prove the
            // sanitized left-pane row and column title carry no hazard (T-110).
            BOOST_TEST(!has_display_hazard(
                tui::sanitize_for_terminal(model.left_pane_row(0))));
            for (const tui::ColumnDef& c : model.columns_for_selected()) {
                BOOST_TEST(
                    !has_display_hazard(tui::sanitize_for_terminal(c.title)));
            }
        }
    }
}
