#define BOOST_TEST_MODULE TuiJsonViewTest

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include <boost/test/unit_test.hpp>
#include <url_shortener/commands/json_scan.hpp>
#include <url_shortener/tui/json_view.hpp>

#include "golden.hpp"

using url_shortener::commands::decode_json_string;
using url_shortener::tui::get_bool;
using url_shortener::tui::get_int;
using url_shortener::tui::get_string;
using url_shortener::tui::pretty_print;
using url_shortener::tui::raw_field;
using url_shortener::tui::split_array;
using url_shortener::tui::testing::assert_matches_golden;

// ---------------------------------------------------------------------------
// raw_field returns the exact source text of nested containers.
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(raw_field_reads_nested_object_and_array)
{
    const std::string json = R"({"obj":{"x":1},"arr":[1,2,3]})";

    const std::optional<std::string> obj = raw_field(json, "obj");
    BOOST_REQUIRE(obj.has_value());
    BOOST_TEST(*obj == R"({"x":1})");

    const std::optional<std::string> arr = raw_field(json, "arr");
    BOOST_REQUIRE(arr.has_value());
    BOOST_TEST(*arr == "[1,2,3]");

    const std::vector<std::string> elements = split_array(*arr);
    BOOST_REQUIRE(elements.size() == 3u);
    BOOST_TEST(elements[0] == "1");
    BOOST_TEST(elements[2] == "3");
}

// ---------------------------------------------------------------------------
// split_array delegates to scan_array: braces/brackets/escaped quotes and
// commas inside string values never mis-split.
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(split_array_is_not_confused_by_string_contents)
{
    const std::string array = R"(["a{b}","c]d","e\"f","g,h"])";
    const std::vector<std::string> elements = split_array(array);
    BOOST_REQUIRE(elements.size() == 4u);
    BOOST_TEST(elements[0] == R"("a{b}")");
    BOOST_TEST(elements[1] == R"("c]d")");
    BOOST_TEST(elements[3] == R"("g,h")");

    const std::optional<std::string> decoded = decode_json_string(elements[2]);
    BOOST_REQUIRE(decoded.has_value());
    BOOST_TEST(*decoded == "e\"f");
}

// ---------------------------------------------------------------------------
// Empty and non-array inputs yield an empty vector.
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(split_array_empty_and_invalid)
{
    BOOST_TEST(split_array("[]").empty());
    BOOST_TEST(split_array("not an array").empty());
    BOOST_TEST(split_array("[1,2").empty());  // truncated, no UB
}

// ---------------------------------------------------------------------------
// Missing fields and truncated input report a miss, never UB.
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(missing_and_truncated_report_nullopt)
{
    const std::string json = R"({"a":1})";
    BOOST_TEST(!raw_field(json, "b").has_value());
    BOOST_TEST(!get_int(json, "b").has_value());
    BOOST_TEST(!get_string(json, "b").has_value());

    const std::string truncated = R"({"a":)";
    BOOST_TEST(!raw_field(truncated, "a").has_value());
    BOOST_TEST(!get_int(truncated, "a").has_value());
}

// ---------------------------------------------------------------------------
// get_int accepts integers only; floats, strings and missing are nullopt.
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(get_int_typed_reads)
{
    const std::string json = R"({"n":42,"neg":-7,"f":1.5,"s":"x"})";
    BOOST_REQUIRE(get_int(json, "n").has_value());
    BOOST_TEST(*get_int(json, "n") == static_cast<std::int64_t>(42));
    BOOST_TEST(*get_int(json, "neg") == static_cast<std::int64_t>(-7));
    BOOST_TEST(!get_int(json, "f").has_value());
    BOOST_TEST(!get_int(json, "s").has_value());
}

// ---------------------------------------------------------------------------
// get_bool accepts booleans only.
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(get_bool_typed_reads)
{
    const std::string json = R"({"b":true,"c":false,"n":1})";
    BOOST_REQUIRE(get_bool(json, "b").has_value());
    BOOST_TEST(*get_bool(json, "b"));
    BOOST_REQUIRE(get_bool(json, "c").has_value());
    BOOST_TEST(!*get_bool(json, "c"));
    BOOST_TEST(!get_bool(json, "n").has_value());
}

// ---------------------------------------------------------------------------
// get_string decodes Unicode and \u escapes.
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(get_string_decodes_unicode_escapes)
{
    const std::string json = R"({"u":"Aé","plain":"café"})";
    const std::optional<std::string> u = get_string(json, "u");
    BOOST_REQUIRE(u.has_value());
    BOOST_TEST(*u == std::string("A\xC3\xA9"));  // "Aé"

    const std::optional<std::string> plain = get_string(json, "plain");
    BOOST_REQUIRE(plain.has_value());
    BOOST_TEST(*plain == std::string("caf\xC3\xA9"));  // "café"
}

// ---------------------------------------------------------------------------
// pretty_print golden: re-indented link.list result, keys and values intact.
// On failure first check: the pretty_print algorithm (spacing only) changed;
// rerun with UPDATE_GOLDEN=1 and review the diff if the change is intended.
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(pretty_print_link_list_golden)
{
    const std::string link_list =
        R"({"items":[{"slug":"docs","target_url":)"
        R"("https://example.com/docs","status":"active","clicks":12044,)"
        R"("expires_at":null},{"slug":"promo","target_url":)"
        R"("https://shop.example.com/promo","status":"disabled",)"
        R"("clicks":4112}],"total":2,"page":1})";

    assert_matches_golden(pretty_print(link_list, 2),
                          "02_pretty_link_list.txt");
}

// ---------------------------------------------------------------------------
// Invalid or trailing-data JSON is returned verbatim, never throws.
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(pretty_print_invalid_returned_verbatim)
{
    BOOST_TEST(pretty_print("not json") == "not json");
    BOOST_TEST(pretty_print(R"({"a":1} trailing)") == R"({"a":1} trailing)");
    BOOST_TEST(pretty_print("{") == "{");
    // A bare leaf value is valid and re-emitted (trimmed) unchanged.
    BOOST_TEST(pretty_print("  42  ") == "42");
}

// ---------------------------------------------------------------------------
// Success criterion 4: split_array round-trips 1,000 generated items with no
// mis-splitting (property-style loop).
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(split_array_round_trips_thousand_items)
{
    std::string array = "[";
    for (int i = 0; i < 1000; ++i) {
        if (i != 0) {
            array += ",";
        }
        array += R"({"slug":"s)" + std::to_string(i)
            + R"(","target":"https://ex/)" + std::to_string(i) + R"("})";
    }
    array += "]";

    const std::vector<std::string> items = split_array(array);
    BOOST_REQUIRE(items.size() == 1000u);
    for (int i = 0; i < 1000; ++i) {
        const std::optional<std::string> slug =
            get_string(items[static_cast<std::size_t>(i)], "slug");
        BOOST_REQUIRE(slug.has_value());
        BOOST_TEST(*slug == "s" + std::to_string(i));
    }
}

// ---------------------------------------------------------------------------
// The 8 MiB byte cap accepts a large page but rejects hostile oversize input.
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(byte_cap_accepts_large_rejects_oversize)
{
    const std::string under_value(1u * 1024u * 1024u, 'a');  // ~1 MiB
    const std::string under = R"({"big":")" + under_value + R"("})";
    BOOST_TEST(raw_field(under, "big").has_value());

    const std::string over_value(9u * 1024u * 1024u, 'a');  // ~9 MiB > cap
    const std::string over = R"({"big":")" + over_value + R"("})";
    BOOST_TEST(!raw_field(over, "big").has_value());
}
