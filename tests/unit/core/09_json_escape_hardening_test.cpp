/**
 * @file 09_json_escape_hardening_test.cpp
 * @brief Unit test: the shared `jsonEscape` always produces a valid JSON string
 * body for untrusted input (control bytes, DEL, U+2028/U+2029, and malformed
 * UTF-8), while copying already-valid text through byte-for-byte.
 */
#define BOOST_TEST_MODULE JsonEscapeHardeningTest
#include <string>

#include <boost/test/unit_test.hpp>
#include <url_shortener/core/utils.hpp>

using url_shortener::jsonEscape;

namespace
{

///< The UTF-8 encoding of U+FFFD, the replacement emitted for a malformed byte.
const std::string k_replacement = "\xEF\xBF\xBD";

}  // namespace

/**
 * [Unit][Core] The five historically-handled characters keep their short JSON
 * escape sequences.
 *
 * Scenario:
 *   Given a string containing backslash, quote, newline, carriage return and
 *   tab.
 *   When jsonEscape() is called.
 *   Then each becomes its two-character backslash sequence, not a \u escape.
 *
 * If this breaks:
 *   - Existing REST/command bodies change shape.
 *   - First check the ASCII fast-path switch in jsonEscape.
 */
BOOST_AUTO_TEST_CASE(short_escapes_unchanged)
{
    BOOST_TEST(jsonEscape("a\\b") == "a\\\\b");
    BOOST_TEST(jsonEscape("a\"b") == "a\\\"b");
    BOOST_TEST(jsonEscape("a\nb") == "a\\nb");
    BOOST_TEST(jsonEscape("a\rb") == "a\\rb");
    BOOST_TEST(jsonEscape("a\tb") == "a\\tb");
}

/**
 * [Unit][Core] Every C0 control byte and DEL is \u00XX-escaped.
 *
 * Scenario:
 *   Given NUL, SOH (0x01), the unit separator (0x1F) and DEL (0x7F).
 *   When jsonEscape() is called.
 *   Then each is replaced by its lowercase \u00XX escape; no raw control byte
 *   survives in the output.
 *
 * Boundaries: 0x00, 0x01 and 0x1F are the low/boundary C0 bytes; 0x7F is the
 *   lone control that is not below 0x20.
 *
 * If this breaks:
 *   - A raw control byte can break the JSON/log the value is embedded in.
 *   - First check the `c < 0x20 || c == 0x7F` branch and appendUnicodeEscape.
 */
BOOST_AUTO_TEST_CASE(control_bytes_and_del_are_u_escaped)
{
    BOOST_TEST(jsonEscape(std::string(1, '\0')) == "\\u0000");
    BOOST_TEST(jsonEscape("\x01") == "\\u0001");
    BOOST_TEST(jsonEscape("\x1F") == "\\u001f");
    BOOST_TEST(jsonEscape("\x7F") == "\\u007f");
    // Control byte embedded in otherwise-plain text.
    BOOST_TEST(jsonEscape("x\x02y") == "x\\u0002y");
}

/**
 * [Unit][Core] The JavaScript line/paragraph separators U+2028 and U+2029 are
 * \u-escaped even though they are valid JSON.
 *
 * Scenario:
 *   Given the UTF-8 encodings of U+2028 and U+2029.
 *   When jsonEscape() is called.
 *   Then each becomes   /  .
 *
 * If this breaks:
 *   - Output embedded in a <script> or JS string literal can break parsing.
 *   - First check the explicit U+2028/U+2029 branch after UTF-8 validation.
 */
BOOST_AUTO_TEST_CASE(line_and_paragraph_separators_escaped)
{
    BOOST_TEST(jsonEscape("\xE2\x80\xA8") == "\\u2028");
    BOOST_TEST(jsonEscape("\xE2\x80\xA9") == "\\u2029");
}

/**
 * [Unit][Core] Valid multi-byte UTF-8 passes through byte-for-byte.
 *
 * Scenario:
 *   Given valid 2-, 3- and 4-byte UTF-8 sequences (é, €, U+1F600).
 *   When jsonEscape() is called.
 *   Then the bytes are copied through unchanged.
 *
 * If this breaks:
 *   - Non-ASCII text would be mangled or needlessly escaped, breaking parity
 *     with the historical escaper.
 *   - First check the "valid scalar value" copy branch.
 */
BOOST_AUTO_TEST_CASE(valid_multibyte_passes_unchanged)
{
    const std::string e_acute = "\xC3\xA9";  // U+00E9
    const std::string euro = "\xE2\x82\xAC";  // U+20AC
    const std::string grinning = "\xF0\x9F\x98\x80";  // U+1F600
    BOOST_TEST(jsonEscape(e_acute) == e_acute);
    BOOST_TEST(jsonEscape(euro) == euro);
    BOOST_TEST(jsonEscape(grinning) == grinning);
    BOOST_TEST(jsonEscape("a" + e_acute + "b" + euro)
               == "a" + e_acute + "b" + euro);
}

/**
 * [Unit][Core] Malformed UTF-8 is replaced with U+FFFD, never passed through
 * raw.
 *
 * Scenario:
 *   Given a lone continuation byte (0x80), a truncated 3-byte lead (0xE2 0x80),
 *   an overlong NUL (0xC0 0x80), an invalid lead byte (0xFF) and a surrogate
 *   encoding (0xED 0xA0 0x80 => U+D800).
 *   When jsonEscape() is called.
 *   Then every output byte is valid: the raw bad bytes do not appear and at
 *   least one U+FFFD replacement is produced.
 *
 * Boundaries: covers each malformed class the decoder distinguishes (lone
 *   continuation, truncated tail, overlong, invalid lead, surrogate).
 *
 * If this breaks:
 *   - Invalid UTF-8 could escape into a JSON body and make it unparseable.
 *   - First check the replacement-and-resynchronize branches in jsonEscape.
 */
BOOST_AUTO_TEST_CASE(malformed_utf8_replaced)
{
    // Lone continuation byte.
    BOOST_TEST(jsonEscape("\x80") == k_replacement);
    // Truncated 3-byte tail at end of input: lead then one continuation.
    {
        const std::string out = jsonEscape("\xE2\x80");
        BOOST_TEST(out.find('\xE2') == std::string::npos);
        BOOST_TEST(out.find(k_replacement) != std::string::npos);
    }
    // Overlong encoding of NUL.
    {
        const std::string out = jsonEscape("\xC0\x80");
        BOOST_TEST(out.find('\xC0') == std::string::npos);
        BOOST_TEST(out.find(k_replacement) != std::string::npos);
    }
    // Invalid lead byte.
    BOOST_TEST(jsonEscape("\xFF") == k_replacement);
    // Surrogate code point U+D800 encoded as if valid UTF-8.
    {
        const std::string out = jsonEscape("\xED\xA0\x80");
        BOOST_TEST(out.find('\xED') == std::string::npos);
        BOOST_TEST(out.find(k_replacement) != std::string::npos);
    }
}

/**
 * [Unit][Core] A bad byte does not consume the valid bytes around it.
 *
 * Scenario:
 *   Given "a" + 0x80 + "b" (plain, bad, plain).
 *   When jsonEscape() is called.
 *   Then the output is "a" + U+FFFD + "b": decoding resynchronizes after the
 *   single bad byte.
 *
 * If this breaks:
 *   - A single malformed byte could swallow adjacent valid text.
 *   - First check that each replacement advances exactly one byte.
 */
BOOST_AUTO_TEST_CASE(resynchronizes_after_bad_byte)
{
    BOOST_TEST(jsonEscape("a\x80z") == "a" + k_replacement + "z");
}

/**
 * [Unit][Core] Plain ASCII text is copied through unchanged.
 *
 * Scenario:
 *   Given a printable ASCII string with spaces and punctuation.
 *   When jsonEscape() is called.
 *   Then the output equals the input (byte-identical parity with the historical
 *   escaper for already-valid input).
 *
 * If this breaks:
 *   - Common REST/command values change shape.
 *   - First check the ASCII default branch (printable bytes pushed as-is).
 */
BOOST_AUTO_TEST_CASE(plain_ascii_unchanged)
{
    const std::string plain = "Hello, world! /path?x=1 (ok)";
    BOOST_TEST(jsonEscape(plain) == plain);
}
