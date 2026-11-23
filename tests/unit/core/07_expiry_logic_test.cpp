/** @file 07_expiry_logic_test.cpp @brief Unit test: expiry evaluation logic. */
#define BOOST_TEST_MODULE ExpiryLogicTest
#include <boost/test/unit_test.hpp>
#include <url_shortener/url_shortener.hpp>

using namespace url_shortener;

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

/**
 * [Unit][Core] parseRfc3339Zulu enforces a strict, calendar-aware grammar.
 *
 * Scenario:
 *   Given a range of RFC3339-Zulu candidate strings, some well-formed and
 *   some malformed (bad calendar dates, missing/lowercase zone designator,
 *   numeric offsets, date-only, fractional seconds, trailing garbage).
 *   When parseRfc3339Zulu() evaluates each one.
 *   Then only strictly-conforming UTC timestamps yield a value and every
 *   invalid form yields std::nullopt.
 *
 * API/Feature covered:
 *   - Hardened RFC3339 Zulu timestamp parsing (CWE-20 input validation on
 *     expires_at and other persisted timestamps).
 *
 * Boundaries:
 *   - Leap day accepted only in an actual leap year.
 *   - Day validated against per-month length.
 *   - Fractional seconds 1..9 digits accepted; zone designator mandatory.
 *
 * If this breaks:
 *   - Invalid calendar dates may be silently normalised and stored, or valid
 *     timestamps rejected.
 *   - First check the field-by-field grammar and the days_in_month table.
 */
BOOST_AUTO_TEST_CASE(parse_rfc3339_zulu_strict_grammar)
{
    // Valid canonical form.
    BOOST_TEST(parseRfc3339Zulu("2026-01-02T03:04:05Z").has_value());

    // Leap day: valid in a leap year, invalid in a common year.
    BOOST_TEST(parseRfc3339Zulu("2024-02-29T00:00:00Z").has_value());
    BOOST_TEST(!parseRfc3339Zulu("2026-02-29T00:00:00Z").has_value());

    // Day-of-month overflow (April has 30 days).
    BOOST_TEST(!parseRfc3339Zulu("2026-04-31T00:00:00Z").has_value());

    // Date-only value (no time component).
    BOOST_TEST(!parseRfc3339Zulu("2026-01-02").has_value());

    // Missing mandatory trailing 'Z'.
    BOOST_TEST(!parseRfc3339Zulu("2026-01-02T03:04:05").has_value());

    // Numeric UTC offsets are not accepted (Zulu only).
    BOOST_TEST(!parseRfc3339Zulu("2026-01-02T03:04:05+00:00").has_value());

    // Fractional seconds: valid with 1..9 digits.
    BOOST_TEST(parseRfc3339Zulu("2026-01-02T03:04:05.5Z").has_value());
    BOOST_TEST(parseRfc3339Zulu("2026-01-02T03:04:05.123456789Z").has_value());
    // Too many fractional digits, or a lone dot, are rejected.
    BOOST_TEST(!parseRfc3339Zulu("2026-01-02T03:04:05.1234567890Z").has_value());
    BOOST_TEST(!parseRfc3339Zulu("2026-01-02T03:04:05.Z").has_value());

    // Lowercase separators are rejected.
    BOOST_TEST(!parseRfc3339Zulu("2026-01-02t03:04:05Z").has_value());
    BOOST_TEST(!parseRfc3339Zulu("2026-01-02T03:04:05z").has_value());

    // Out-of-range time fields; leap second (:60) is rejected.
    BOOST_TEST(!parseRfc3339Zulu("2026-01-02T24:00:00Z").has_value());
    BOOST_TEST(!parseRfc3339Zulu("2026-01-02T03:60:05Z").has_value());
    BOOST_TEST(!parseRfc3339Zulu("2026-12-31T23:59:60Z").has_value());
    BOOST_TEST(!parseRfc3339Zulu("2026-00-10T00:00:00Z").has_value());

    // Trailing garbage after a valid timestamp.
    BOOST_TEST(!parseRfc3339Zulu("2026-01-02T03:04:05Z ").has_value());
    BOOST_TEST(!parseRfc3339Zulu("2026-01-02T03:04:05Zx").has_value());

    // Non-numeric / empty / whitespace input.
    BOOST_TEST(!parseRfc3339Zulu("").has_value());
    BOOST_TEST(!parseRfc3339Zulu("not-a-timestamp").has_value());
    BOOST_TEST(!parseRfc3339Zulu("2026-1-2T3:4:5Z").has_value());
}
