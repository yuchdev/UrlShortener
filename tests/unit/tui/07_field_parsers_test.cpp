#define BOOST_TEST_MODULE TuiFieldParsersTest

#include <limits>
#include <string>
#include <utility>
#include <vector>

#include <boost/test/unit_test.hpp>
#include <url_shortener/tui/field_parsers.hpp>

/**
 * @file 07_field_parsers_test.cpp
 * @brief Unit tests for the comma/equals list and map parsers and their inverse
 * formatters (FTXUI-free model library).
 *
 * Type: unit (model library, links url_shortener_tui_model).
 * Scenario: `parse_string_list` trims items and drops empties;
 * `parse_string_map` splits `k=v` pairs, rejects a missing `=` or empty key and
 * keeps the last value for a duplicate key; `parse_integer` is strict and
 *           overflow-safe; and `format_*` round-trips representable values
 * while flagging any value that embeds a `,`/`=`. Boundaries: empty string,
 * single item, surrounding spaces, trailing comma, multibyte UTF-8, missing
 * `=`, empty key, duplicate key, INT64 min/max and overflow, and a 200-input
 * generated round-trip. On failure first check: the split/trim loops in
 * `src/tui/field_parsers.cpp`.
 */

using url_shortener::tui::format_string_list;
using url_shortener::tui::format_string_map;
using url_shortener::tui::parse_integer;
using url_shortener::tui::parse_string_list;
using url_shortener::tui::parse_string_map;

using string_pairs = std::vector<std::pair<std::string, std::string>>;

// ---------------------------------------------------------------------------
// parse_string_list: trimming, empties and unicode.
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(list_empty_string_yields_no_items)
{
    BOOST_CHECK(parse_string_list("").empty());
    BOOST_CHECK(parse_string_list("   ").empty());
}

BOOST_AUTO_TEST_CASE(list_single_item)
{
    const std::vector<std::string> items = parse_string_list("solo");
    BOOST_REQUIRE_EQUAL(items.size(), 1u);
    BOOST_CHECK_EQUAL(items[0], "solo");
}

BOOST_AUTO_TEST_CASE(list_trims_spaces_and_drops_empties)
{
    const std::vector<std::string> items = parse_string_list("a, b ,c");
    BOOST_REQUIRE_EQUAL(items.size(), 3u);
    BOOST_CHECK_EQUAL(items[0], "a");
    BOOST_CHECK_EQUAL(items[1], "b");
    BOOST_CHECK_EQUAL(items[2], "c");

    const std::vector<std::string> gapped = parse_string_list("a,,b,");
    BOOST_REQUIRE_EQUAL(gapped.size(), 2u);
    BOOST_CHECK_EQUAL(gapped[0], "a");
    BOOST_CHECK_EQUAL(gapped[1], "b");
}

BOOST_AUTO_TEST_CASE(list_trailing_comma_is_ignored)
{
    const std::vector<std::string> items = parse_string_list("x,y,");
    BOOST_REQUIRE_EQUAL(items.size(), 2u);
    BOOST_CHECK_EQUAL(items[1], "y");
}

BOOST_AUTO_TEST_CASE(list_preserves_multibyte_utf8)
{
    // "café, 世界" - accented and CJK items survive byte-for-byte.
    const std::vector<std::string> items =
        parse_string_list("caf\xc3\xa9, \xe4\xb8\x96\xe7\x95\x8c");
    BOOST_REQUIRE_EQUAL(items.size(), 2u);
    BOOST_CHECK_EQUAL(items[0], "caf\xc3\xa9");
    BOOST_CHECK_EQUAL(items[1], "\xe4\xb8\x96\xe7\x95\x8c");
}

// ---------------------------------------------------------------------------
// parse_string_map: pairs, rejections and duplicate handling.
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(map_basic_pairs)
{
    const auto parsed = parse_string_map("a=1, b=2");
    BOOST_REQUIRE(parsed.has_value());
    BOOST_REQUIRE_EQUAL(parsed->size(), 2u);
    BOOST_CHECK_EQUAL((*parsed)[0].first, "a");
    BOOST_CHECK_EQUAL((*parsed)[0].second, "1");
    BOOST_CHECK_EQUAL((*parsed)[1].first, "b");
    BOOST_CHECK_EQUAL((*parsed)[1].second, "2");
}

BOOST_AUTO_TEST_CASE(map_empty_string_is_empty_map)
{
    const auto parsed = parse_string_map("");
    BOOST_REQUIRE(parsed.has_value());
    BOOST_CHECK(parsed->empty());
}

BOOST_AUTO_TEST_CASE(map_allows_empty_value)
{
    const auto parsed = parse_string_map("k=");
    BOOST_REQUIRE(parsed.has_value());
    BOOST_REQUIRE_EQUAL(parsed->size(), 1u);
    BOOST_CHECK_EQUAL((*parsed)[0].first, "k");
    BOOST_CHECK_EQUAL((*parsed)[0].second, "");
}

BOOST_AUTO_TEST_CASE(map_missing_equals_is_rejected)
{
    BOOST_CHECK(!parse_string_map("a=1, bad").has_value());
}

BOOST_AUTO_TEST_CASE(map_empty_key_is_rejected)
{
    BOOST_CHECK(!parse_string_map("=v").has_value());
    BOOST_CHECK(!parse_string_map("a=1, =v").has_value());
}

BOOST_AUTO_TEST_CASE(map_duplicate_key_last_wins_position_kept)
{
    std::vector<std::string> duplicates;
    const auto parsed = parse_string_map("a=1, b=2, a=3", &duplicates);
    BOOST_REQUIRE(parsed.has_value());
    BOOST_REQUIRE_EQUAL(parsed->size(), 2u);
    BOOST_CHECK_EQUAL((*parsed)[0].first, "a");
    BOOST_CHECK_EQUAL((*parsed)[0].second, "3");  // last value wins
    BOOST_CHECK_EQUAL((*parsed)[1].first, "b");

    // The duplicate key is reported on the warning channel ("last wins").
    BOOST_REQUIRE_EQUAL(duplicates.size(), 1u);
    BOOST_CHECK_EQUAL(duplicates[0], "a");

    // No duplicates -> the channel is left untouched.
    std::vector<std::string> none;
    parse_string_map("a=1, b=2", &none);
    BOOST_CHECK(none.empty());
}

BOOST_AUTO_TEST_CASE(map_splits_on_first_equals_only)
{
    const auto parsed = parse_string_map("url=http://x?a=b");
    BOOST_REQUIRE(parsed.has_value());
    BOOST_REQUIRE_EQUAL(parsed->size(), 1u);
    BOOST_CHECK_EQUAL((*parsed)[0].first, "url");
    BOOST_CHECK_EQUAL((*parsed)[0].second, "http://x?a=b");
}

// ---------------------------------------------------------------------------
// parse_integer: strictness and overflow.
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(integer_basic_and_sign)
{
    BOOST_CHECK_EQUAL(parse_integer("0").value(), 0);
    BOOST_CHECK_EQUAL(parse_integer("42").value(), 42);
    BOOST_CHECK_EQUAL(parse_integer(" -7 ").value(), -7);
    BOOST_CHECK_EQUAL(parse_integer("+5").value(), 5);
    BOOST_CHECK_EQUAL(parse_integer("007").value(), 7);
}

BOOST_AUTO_TEST_CASE(integer_rejects_non_numeric)
{
    BOOST_CHECK(!parse_integer("").has_value());
    BOOST_CHECK(!parse_integer("abc").has_value());
    BOOST_CHECK(!parse_integer("12a").has_value());
    BOOST_CHECK(!parse_integer("-").has_value());
    BOOST_CHECK(!parse_integer("1.5").has_value());
}

BOOST_AUTO_TEST_CASE(integer_range_limits)
{
    BOOST_CHECK_EQUAL(parse_integer("9223372036854775807").value(),
                      std::numeric_limits<long long>::max());
    BOOST_CHECK_EQUAL(parse_integer("-9223372036854775808").value(),
                      std::numeric_limits<long long>::min());
    BOOST_CHECK(!parse_integer("9223372036854775808").has_value());
    BOOST_CHECK(!parse_integer("-9223372036854775809").has_value());
    BOOST_CHECK(!parse_integer("99999999999999999999").has_value());
}

// ---------------------------------------------------------------------------
// format_* flags values that cannot round-trip through the comma/equals form.
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(format_list_flags_embedded_comma)
{
    bool representable = true;
    const std::string text = format_string_list({"ok", "a,b"}, representable);
    BOOST_CHECK(!representable);
    BOOST_CHECK_EQUAL(text, "ok, a,b");
}

BOOST_AUTO_TEST_CASE(format_map_flags_embedded_separators)
{
    bool representable = true;
    format_string_map({{"k", "a=b"}}, representable);
    BOOST_CHECK(!representable);

    representable = true;
    format_string_map({{"k,2", "v"}}, representable);
    BOOST_CHECK(!representable);
}

// ---------------------------------------------------------------------------
// Round-trip: parse(format(x)) == x over 200 generated inputs that avoid the
// non-representable characters (',' '=' and surrounding whitespace).
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(round_trip_list_and_map_200_inputs)
{
    // A small deterministic LCG keeps the test dependency-free and stable.
    unsigned long long state = 0x9e3779b97f4a7c15ULL;
    const auto next = [&state]() -> unsigned
    {
        state = state * 6364136223846793005ULL + 1442695040888963407ULL;
        return static_cast<unsigned>(state >> 33);
    };
    const char alphabet[] = "abcXYZ0189_.-/:";  // no ',', '=', or space

    const auto gen_token = [&]() -> std::string
    {
        const unsigned len = 1u + (next() % 6u);  // 1..6, never empty
        std::string token;
        for (unsigned k = 0; k < len; ++k) {
            token += alphabet[next() % (sizeof(alphabet) - 1u)];
        }
        return token;
    };

    for (int iter = 0; iter < 200; ++iter) {
        // List round-trip.
        std::vector<std::string> items;
        const unsigned count = 1u + (next() % 5u);
        for (unsigned k = 0; k < count; ++k) {
            items.push_back(gen_token());
        }
        bool representable = true;
        const std::string list_text = format_string_list(items, representable);
        BOOST_REQUIRE(representable);
        BOOST_CHECK(parse_string_list(list_text) == items);

        // Map round-trip with unique keys (duplicate keys would collapse).
        string_pairs pairs;
        const unsigned pair_count = 1u + (next() % 5u);
        for (unsigned k = 0; k < pair_count; ++k) {
            std::string key = gen_token() + std::to_string(k);  // unique
            pairs.emplace_back(std::move(key), gen_token());
        }
        representable = true;
        const std::string map_text = format_string_map(pairs, representable);
        BOOST_REQUIRE(representable);
        const auto parsed = parse_string_map(map_text);
        BOOST_REQUIRE(parsed.has_value());
        BOOST_CHECK(*parsed == pairs);
    }
}
