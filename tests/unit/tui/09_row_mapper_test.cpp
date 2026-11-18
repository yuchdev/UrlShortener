#define BOOST_TEST_MODULE TuiRowMapperTest

#include <optional>
#include <string>
#include <vector>

#include <boost/test/unit_test.hpp>

#include <url_shortener/tui/row_mapper.hpp>
#include <url_shortener/tui/table_model.hpp>

using url_shortener::tui::PageResult;
using url_shortener::tui::rows_from_result;

namespace
{

const std::vector<std::string> link_columns = {"slug", "target_url",
                                               "stats.total_redirects"};

}  // namespace

// ---------------------------------------------------------------------------
// link.list shape, nested stats.total_redirects, missing field -> "-"
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(maps_link_list_with_nested_and_missing_fields)
{
    const std::string result =
        R"({"items":[)"
        R"({"slug":"docs","target_url":"https://example.com/docs",)"
        R"("stats":{"total_redirects":12044}},)"
        R"({"slug":"launch","target_url":"https://example.com/launch",)"
        R"("stats":{"total_redirects":8730}},)"
        R"({"slug":"promo","target_url":"https://shop.example.com/promo"})"
        R"(],"total":1204})";

    std::optional<PageResult> page =
        rows_from_result(result, link_columns, "items");
    BOOST_REQUIRE(page.has_value());
    BOOST_REQUIRE(page->rows.size() == 3u);
    BOOST_TEST(page->total == 1204u);

    BOOST_TEST(page->rows[0][0] == "docs");
    BOOST_TEST(page->rows[0][1] == "https://example.com/docs");
    BOOST_TEST(page->rows[0][2] == "12044");
    BOOST_TEST(page->rows[1][2] == "8730");
    BOOST_TEST(page->rows[2][2] == "-");  // no stats object at all

    BOOST_REQUIRE(page->raw_rows.size() == 3u);
    BOOST_TEST(page->raw_rows[2] ==
               R"({"slug":"promo","target_url":"https://shop.example.com/promo"})");
}

// ---------------------------------------------------------------------------
// Values containing ] } and escaped quotes are decoded, never mis-split.
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(handles_brackets_braces_and_escaped_quotes)
{
    const std::string result =
        R"({"items":[{"slug":"a]b}c","target_url":"x\"y","stats":{}}],)"
        R"("total":1})";

    std::optional<PageResult> page =
        rows_from_result(result, link_columns, "items");
    BOOST_REQUIRE(page.has_value());
    BOOST_REQUIRE(page->rows.size() == 1u);
    BOOST_TEST(page->rows[0][0] == "a]b}c");
    BOOST_TEST(page->rows[0][1] == "x\"y");
    BOOST_TEST(page->rows[0][2] == "-");  // stats present but key missing
}

// ---------------------------------------------------------------------------
// db.rows shape (rows field, integer and boolean cells)
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(maps_db_rows_shape)
{
    const std::string result =
        R"({"rows":[{"id":"01HZX","hits":7,"enabled":true}],"total":3})";
    const std::vector<std::string> cols = {"id", "hits", "enabled"};

    std::optional<PageResult> page = rows_from_result(result, cols, "rows");
    BOOST_REQUIRE(page.has_value());
    BOOST_REQUIRE(page->rows.size() == 1u);
    BOOST_TEST(page->total == 3u);
    BOOST_TEST(page->rows[0][0] == "01HZX");
    BOOST_TEST(page->rows[0][1] == "7");
    BOOST_TEST(page->rows[0][2] == "true");
}

// ---------------------------------------------------------------------------
// Empty items is a well-formed empty page, not an error.
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(empty_items_is_valid_empty_page)
{
    std::optional<PageResult> page =
        rows_from_result(R"({"items":[],"total":0})", link_columns, "items");
    BOOST_REQUIRE(page.has_value());
    BOOST_TEST(page->rows.empty());
    BOOST_TEST(page->total == 0u);
}

// ---------------------------------------------------------------------------
// total falls back to element count when absent.
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(total_defaults_to_element_count)
{
    std::optional<PageResult> page = rows_from_result(
        R"({"items":[{"slug":"a"},{"slug":"b"}]})", {"slug"}, "items");
    BOOST_REQUIRE(page.has_value());
    BOOST_TEST(page->total == 2u);
}

// ---------------------------------------------------------------------------
// Malformed JSON / missing array -> nullopt (caller shows an error), no crash.
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(malformed_json_returns_nullopt)
{
    BOOST_TEST(!rows_from_result(R"({"items":[)", link_columns, "items")
                    .has_value());
    BOOST_TEST(!rows_from_result("not json at all", link_columns, "items")
                    .has_value());
    BOOST_TEST(
        !rows_from_result(R"({"total":0})", link_columns, "items").has_value());
}
