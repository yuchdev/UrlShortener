/** @file 21_query_params.cpp
 *  @brief Unit tests for the query-string parser added for GET /api/v1/links
 *  (milestone 0006, Task 04.0 subtask 07): percent-decoding, `+`-as-space,
 *  repeated keys, and the hostile-input rejections required by threat model
 *  T-73 (malformed %XX, decoded NUL, empty names).
 *
 *  Numbering note: the spec assigned 20, but tests/unit/http/20 is taken
 *  (20_rest_lifecycle_via_registry), so the parser test is 21 and the route
 *  test is 22.
 */
#define BOOST_TEST_MODULE QueryParams
#include <boost/test/unit_test.hpp>

#include <optional>
#include <string>
#include <vector>

#include <url_shortener/http/query_params.hpp>

using url_shortener::http::parse_query_string;
using url_shortener::http::percent_decode;
using url_shortener::http::query_param;

namespace
{

// Parses and requires success, returning the decoded params.
std::vector<query_param> ok_parse(const std::string& query)
{
    std::vector<query_param> out;
    std::string error;
    BOOST_REQUIRE_MESSAGE(parse_query_string(query, &out, &error),
                          "expected success for [" << query << "]");
    return out;
}

// Parses and requires failure with the fixed malformed_query token.
void bad_parse(const std::string& query)
{
    std::vector<query_param> out;
    std::string error;
    BOOST_TEST(!parse_query_string(query, &out, &error),
               "expected failure for [" << query << "]");
    BOOST_TEST(error == "malformed_query", "[" << query << "]");
}

}  // namespace

// Type: unit (empty input).
// Scenario: an empty query string yields zero parameters and success.
// Boundaries: the empty string is the no-parameters case, not an error.
// On failure first check: did the early-return for empty input change?
BOOST_AUTO_TEST_CASE(empty_string_yields_no_params)
{
    const auto params = ok_parse("");
    BOOST_TEST(params.empty());
}

// Type: unit (two simple pairs).
// Scenario: a=1&b=2 decodes to two ordered pairs.
// Boundaries: the common multi-parameter path.
// On failure first check: did splitting on & or = break?
BOOST_AUTO_TEST_CASE(two_pairs_decode_in_order)
{
    const auto params = ok_parse("a=1&b=2");
    BOOST_REQUIRE(params.size() == 2);
    BOOST_TEST(params[0].name == "a");
    BOOST_TEST(params[0].value == "1");
    BOOST_TEST(params[1].name == "b");
    BOOST_TEST(params[1].value == "2");
}

// Type: unit (name without '=' and empty value).
// Scenario: `a` (no '=') and `a=` both yield an empty value.
// Boundaries: a bare name is valid with an empty value; a trailing '=' is an
//   explicit empty value.
// On failure first check: did the missing-'=' branch stop producing an empty
//   value?
BOOST_AUTO_TEST_CASE(name_without_equals_and_empty_value)
{
    const auto bare = ok_parse("a");
    BOOST_REQUIRE(bare.size() == 1);
    BOOST_TEST(bare[0].name == "a");
    BOOST_TEST(bare[0].value.empty());

    const auto empty_value = ok_parse("a=");
    BOOST_REQUIRE(empty_value.size() == 1);
    BOOST_TEST(empty_value[0].name == "a");
    BOOST_TEST(empty_value[0].value.empty());
}

// Type: unit (percent and plus decoding).
// Scenario: %20 and + decode to space, %2B to '+', and the two-byte UTF-8
//   sequence %C3%A9 to the bytes of 'é'.
// Boundaries: '+' is space but %2B is a literal '+'; multi-byte UTF-8 round
//   trips byte-for-byte.
// On failure first check: did +/%XX handling or the hex decode change?
BOOST_AUTO_TEST_CASE(percent_and_plus_decoding)
{
    const auto params = ok_parse("a=%20&b=+&c=%2B&d=%C3%A9");
    BOOST_REQUIRE(params.size() == 4);
    BOOST_TEST(params[0].value == " ");
    BOOST_TEST(params[1].value == " ");
    BOOST_TEST(params[2].value == "+");
    BOOST_TEST(params[3].value == "\xC3\xA9");

    BOOST_TEST(percent_decode("ex%20ample").value_or("") == "ex ample");
    BOOST_TEST(percent_decode("a+b").value_or("") == "a b");
}

// Type: unit (malformed percent escapes rejected).
// Scenario: %zz, a truncated %2, and a trailing % each fail the parser.
// Boundaries: a '%' needs two following hex digits; anything else is malformed.
// On failure first check: did percent_decode start accepting a short/bad
//   escape?
BOOST_AUTO_TEST_CASE(malformed_percent_rejected)
{
    bad_parse("a=%zz");
    bad_parse("a=%2");
    bad_parse("a=%");
    BOOST_TEST(!percent_decode("%zz").has_value());
    BOOST_TEST(!percent_decode("%2").has_value());
    BOOST_TEST(!percent_decode("%").has_value());
}

// Type: unit (decoded NUL rejected, threat model T-73).
// Scenario: %00 decodes to a NUL byte and is rejected so it can never truncate
//   downstream C-string handling.
// Boundaries: 0x00 is the one byte value that must not survive decoding.
// On failure first check: did the NUL guard disappear from percent_decode?
BOOST_AUTO_TEST_CASE(decoded_nul_rejected)
{
    bad_parse("a=%00");
    BOOST_TEST(!percent_decode("%00").has_value());
}

// Type: unit (empty names rejected).
// Scenario: =x, && and a trailing & each produce an empty name and fail.
// Boundaries: a name must decode to a non-empty string; the endpoint is strict.
// On failure first check: did the empty-name guard move or disappear?
BOOST_AUTO_TEST_CASE(empty_name_rejected)
{
    bad_parse("=x");
    bad_parse("&&");
    bad_parse("a=1&");
    bad_parse("&a=1");
}

// Type: unit (duplicates preserved in order).
// Scenario: repeated names are returned in order; detecting them is the caller's
//   job, not the parser's.
// Boundaries: the parser never collapses or rejects duplicates.
// On failure first check: did the parser start de-duplicating?
BOOST_AUTO_TEST_CASE(duplicate_names_returned_in_order)
{
    const auto params = ok_parse("limit=1&limit=2");
    BOOST_REQUIRE(params.size() == 2);
    BOOST_TEST(params[0].value == "1");
    BOOST_TEST(params[1].value == "2");
}

// Type: unit (large input does not crash, bounded allocation).
// Scenario: a 2 KiB query and a very long single value parse without crashing.
// Boundaries: the caller bounds input to the request-target limit (2048); the
//   parser allocates at most the input size.
// On failure first check: did an index or allocation assumption break on large
//   input?
BOOST_AUTO_TEST_CASE(large_input_does_not_crash)
{
    std::string big_query = "a=";
    big_query += std::string(2048, 'x');
    const auto params = ok_parse(big_query);
    BOOST_REQUIRE(params.size() == 1);
    BOOST_TEST(params[0].value.size() == 2048u);

    const std::string long_value(100000, 'y');
    const auto decoded = percent_decode(long_value);
    BOOST_REQUIRE(decoded.has_value());
    BOOST_TEST(decoded->size() == 100000u);
}
