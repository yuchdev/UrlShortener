/** @file 07_expiry_logic_test.cpp @brief Unit test: expiry evaluation logic. */
#define BOOST_TEST_MODULE ExpiryLogicTest
#include <boost/test/unit_test.hpp>
#include "../../../src/url_shortener.cpp"

/**
 * [Unit][Core] Expiry helper distinguishes null vs past timestamps.
 *
 * Scenario:
 *   Given no expiry and a known past expiry timestamp.
 *   When isExpired() evaluates each value.
 *   Then null expiry is false and past expiry is true.
 *
 * API/Feature covered:
 *   - Core expiry evaluation utility.
 *
 * If this breaks:
 *   - Active links can be treated as expired, or expired links may redirect.
 *   - First check timestamp parsing and now-comparison logic.
 */
BOOST_AUTO_TEST_CASE(expiry_evaluation_logic)
{
    BOOST_TEST(!isExpired(std::nullopt));
    BOOST_TEST(isExpired(std::optional<std::string>{"2000-01-01T00:00:00Z"}));
}
