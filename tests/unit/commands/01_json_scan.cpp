#define BOOST_TEST_MODULE CommandsJsonScanTest
#include <string>
#include <string_view>

#include <boost/test/unit_test.hpp>

#include "url_shortener/commands/json_scan.hpp"

using url_shortener::commands::array_scan_result;
using url_shortener::commands::decode_json_string;
using url_shortener::commands::find_member;
using url_shortener::commands::json_kind;
using url_shortener::commands::scan_array;
using url_shortener::commands::scan_limits;
using url_shortener::commands::scan_object;
using url_shortener::commands::scan_result;

// ---------------------------------------------------------------------------
// Happy path
// ---------------------------------------------------------------------------

BOOST_AUTO_TEST_CASE(scan_object_happy_path_classifies_every_member)
{
    const std::string text =
        R"({"v":1,"id":"c-1","ok":true,"x":null,"a":{"k":[1,2]}})";
    scan_result r;
    BOOST_CHECK_NO_THROW(r = scan_object(text));
    BOOST_TEST(r.ok);
    BOOST_TEST(r.error.empty());
    BOOST_REQUIRE_EQUAL(r.members.size(), 5u);

    BOOST_TEST(r.members[0].key == "v");
    BOOST_TEST((r.members[0].kind == json_kind::number));
    BOOST_TEST(r.members[0].raw == "1");

    BOOST_TEST(r.members[1].key == "id");
    BOOST_TEST((r.members[1].kind == json_kind::string));
    BOOST_TEST(r.members[1].raw == "\"c-1\"");

    BOOST_TEST(r.members[2].key == "ok");
    BOOST_TEST((r.members[2].kind == json_kind::boolean));
    BOOST_TEST(r.members[2].raw == "true");

    BOOST_TEST(r.members[3].key == "x");
    BOOST_TEST((r.members[3].kind == json_kind::null));
    BOOST_TEST(r.members[3].raw == "null");

    BOOST_TEST(r.members[4].key == "a");
    BOOST_TEST((r.members[4].kind == json_kind::object));
    BOOST_TEST(r.members[4].raw == R"({"k":[1,2]})");
}

BOOST_AUTO_TEST_CASE(scan_object_empty_object_has_zero_members)
{
    scan_result r = scan_object("{}");
    BOOST_TEST(r.ok);
    BOOST_TEST(r.members.empty());
}

// ---------------------------------------------------------------------------
// Nested look-alike keys (the core/utils.hpp regression)
// ---------------------------------------------------------------------------

BOOST_AUTO_TEST_CASE(scan_object_nested_lookalike_keys_are_not_top_level)
{
    const std::string text =
        R"({"args":{"command":"evil"},"command":"link.get"})";
    scan_result r = scan_object(text);
    BOOST_TEST(r.ok);
    BOOST_REQUIRE_EQUAL(r.members.size(), 2u);

    const auto* command = find_member(r.members, "command");
    BOOST_REQUIRE(command != nullptr);
    BOOST_TEST(command->raw == "\"link.get\"");

    const auto* args = find_member(r.members, "args");
    BOOST_REQUIRE(args != nullptr);
    BOOST_TEST((args->kind == json_kind::object));
}

BOOST_AUTO_TEST_CASE(scan_object_key_text_inside_string_value_is_not_a_key)
{
    const std::string text = R"({"a":"\"b\":1","b":2})";
    scan_result r = scan_object(text);
    BOOST_TEST(r.ok);
    BOOST_REQUIRE_EQUAL(r.members.size(), 2u);
    BOOST_TEST(r.members[0].key == "a");
    BOOST_TEST((r.members[0].kind == json_kind::string));
    BOOST_TEST(r.members[1].key == "b");
    BOOST_TEST(r.members[1].raw == "2");
}

// ---------------------------------------------------------------------------
// Numbers
// ---------------------------------------------------------------------------

BOOST_AUTO_TEST_CASE(scan_object_valid_numbers_accepted)
{
    for (const char* n : {"0", "-1", "1.5", "1e3", "-0.5", "2E-2"}) {
        const std::string text = std::string(R"({"n":)") + n + "}";
        scan_result r = scan_object(text);
        BOOST_TEST(r.ok, "expected ok for number " << n);
        BOOST_REQUIRE_EQUAL(r.members.size(), 1u);
        BOOST_TEST((r.members[0].kind == json_kind::number));
        BOOST_TEST(r.members[0].raw == n);
    }
}

BOOST_AUTO_TEST_CASE(scan_object_malformed_numbers_rejected)
{
    for (const char* n : {"01", "+1", "1.", ".5", "1e", "-"}) {
        const std::string text = std::string(R"({"n":)") + n + "}";
        scan_result r = scan_object(text);
        BOOST_TEST(!r.ok, "expected failure for number " << n);
        BOOST_TEST(r.error == "malformed", "number " << n);
    }
}

// ---------------------------------------------------------------------------
// Booleans / null spelling
// ---------------------------------------------------------------------------

BOOST_AUTO_TEST_CASE(scan_object_bad_literal_spelling_is_malformed)
{
    for (const char* lit : {"True", "nul", "TRUE", "nullx", "fals"}) {
        const std::string text = std::string(R"({"x":)") + lit + "}";
        scan_result r = scan_object(text);
        BOOST_TEST(!r.ok, "expected failure for literal " << lit);
        BOOST_TEST(r.error == "malformed", "literal " << lit);
    }
}

// ---------------------------------------------------------------------------
// decode_json_string escapes and surrogates
// ---------------------------------------------------------------------------

BOOST_AUTO_TEST_CASE(decode_json_string_handles_escapes_and_surrogates)
{
    BOOST_TEST(*decode_json_string(R"("é")") == "\xC3\xA9");  // é
    BOOST_TEST(*decode_json_string(R"("😀")") == "\xF0\x9F\x98\x80");  // 😀
    // \uXXXX escapes: BMP code point and a surrogate pair combined into one
    // 4-byte UTF-8 sequence.
    BOOST_TEST(*decode_json_string(R"("é")") == "\xC3\xA9");
    BOOST_TEST(*decode_json_string(R"("😀")") == "\xF0\x9F\x98\x80");
    BOOST_TEST(*decode_json_string(R"("Aé")") == "A\xC3\xA9");
    BOOST_TEST(*decode_json_string(R"("a\nb")") == "a\nb");
    BOOST_TEST(*decode_json_string(R"("\\\"\/")") == "\\\"/");
    BOOST_TEST(decode_json_string(R"("")").value().empty());
}

BOOST_AUTO_TEST_CASE(decode_json_string_rejects_bad_escapes_and_surrogates)
{
    BOOST_TEST(!decode_json_string(R"("\x")").has_value());
    BOOST_TEST(!decode_json_string(R"("\uD83D")").has_value());  // lone high
    BOOST_TEST(!decode_json_string(R"("\uDE00")").has_value());  // lone low
    BOOST_TEST(!decode_json_string(R"("\uZZZZ")").has_value());
    BOOST_TEST(!decode_json_string("not-a-string").has_value());
    BOOST_TEST(!decode_json_string("\"").has_value());
}

// ---------------------------------------------------------------------------
// Embedded NUL in a decoded key is rejected (T-03)
// ---------------------------------------------------------------------------

BOOST_AUTO_TEST_CASE(scan_object_rejects_nul_in_decoded_key)
{
    scan_result r = scan_object(R"({"a\u0000b":1})");
    BOOST_TEST(!r.ok);
    BOOST_TEST(r.error == "malformed");
}

// ---------------------------------------------------------------------------
// Error vocabulary never echoes the input (T-02)
// ---------------------------------------------------------------------------

BOOST_AUTO_TEST_CASE(scan_object_error_message_never_echoes_input)
{
    const std::string sensitive = "marker-value\r\ninjected";
    const std::string text =
        std::string(R"({"token":")") + sensitive + R"(","bad":@})";
    scan_result r = scan_object(text);
    BOOST_TEST(!r.ok);
    BOOST_TEST(r.error == "malformed");
    BOOST_TEST(r.error.find("marker-value") == std::string::npos);
    BOOST_TEST(r.error.find('\r') == std::string::npos);
    BOOST_TEST(r.error.find('\n') == std::string::npos);
}

// ---------------------------------------------------------------------------
// Limits
// ---------------------------------------------------------------------------

BOOST_AUTO_TEST_CASE(scan_object_too_large_rejected_before_parsing)
{
    std::string text(65537, ' ');
    scan_result r;
    BOOST_CHECK_NO_THROW(r = scan_object(text));
    BOOST_TEST(!r.ok);
    BOOST_TEST(r.error == "too_large");
}

BOOST_AUTO_TEST_CASE(scan_object_depth_nine_is_too_deep)
{
    // A value nested nine container levels deep; default max_depth is 8.
    std::string value(9, '[');
    value.append(9, ']');
    const std::string text = std::string(R"({"a":)") + value + "}";
    scan_result r;
    BOOST_CHECK_NO_THROW(r = scan_object(text));
    BOOST_TEST(!r.ok);
    BOOST_TEST(r.error == "too_deep");
}

BOOST_AUTO_TEST_CASE(scan_object_depth_eight_is_accepted)
{
    std::string value(8, '[');
    value.append(8, ']');
    const std::string text = std::string(R"({"a":)") + value + "}";
    scan_result r = scan_object(text);
    BOOST_TEST(r.ok);
}

BOOST_AUTO_TEST_CASE(scan_object_too_many_members_rejected)
{
    std::string text = "{";
    for (int k = 0; k < 257; ++k) {
        if (k != 0) {
            text += ",";
        }
        text += "\"k" + std::to_string(k) + "\":" + std::to_string(k);
    }
    text += "}";
    scan_result r;
    BOOST_CHECK_NO_THROW(r = scan_object(text));
    BOOST_TEST(!r.ok);
    BOOST_TEST(r.error == "too_many_members");
}

BOOST_AUTO_TEST_CASE(scan_object_overlong_key_is_malformed)
{
    const std::string key(129, 'k');
    const std::string text = "{\"" + key + "\":1}";
    scan_result r = scan_object(text);
    BOOST_TEST(!r.ok);
    BOOST_TEST(r.error == "malformed");
}

BOOST_AUTO_TEST_CASE(scan_object_custom_limits_override_defaults)
{
    scan_limits limits;
    limits.max_members = 1;
    scan_result r = scan_object(R"({"a":1,"b":2})", limits);
    BOOST_TEST(!r.ok);
    BOOST_TEST(r.error == "too_many_members");

    limits.max_members = 2;
    scan_result r2 = scan_object(R"({"a":1,"b":2})", limits);
    BOOST_TEST(r2.ok);
}

// ---------------------------------------------------------------------------
// Structure
// ---------------------------------------------------------------------------

BOOST_AUTO_TEST_CASE(scan_object_non_object_inputs_rejected)
{
    for (const char* t : {"[1,2]", "5", "\"x\"", "", "   ", "true"}) {
        scan_result r = scan_object(t);
        BOOST_TEST(!r.ok, "expected failure for input [" << t << "]");
        BOOST_TEST(r.error == "not_an_object", "input [" << t << "]");
    }
}

BOOST_AUTO_TEST_CASE(scan_object_trailing_data_rejected)
{
    for (const char* t : {"{}}", "{}x", R"({"a":1} junk)"}) {
        scan_result r = scan_object(t);
        BOOST_TEST(!r.ok, "expected failure for input [" << t << "]");
        BOOST_TEST(r.error == "trailing_data", "input [" << t << "]");
    }
}

BOOST_AUTO_TEST_CASE(scan_object_trailing_comma_is_malformed)
{
    scan_result r = scan_object(R"({"a":1,})");
    BOOST_TEST(!r.ok);
    BOOST_TEST(r.error == "malformed");
}

BOOST_AUTO_TEST_CASE(scan_object_duplicate_key_rejected)
{
    scan_result r = scan_object(R"({"a":1,"a":2})");
    BOOST_TEST(!r.ok);
    BOOST_TEST(r.error == "duplicate_key");
}

BOOST_AUTO_TEST_CASE(scan_object_leading_and_trailing_whitespace_allowed)
{
    scan_result r = scan_object("  \n\t{\"a\":1}\r\n ");
    BOOST_TEST(r.ok);
    BOOST_REQUIRE_EQUAL(r.members.size(), 1u);
    BOOST_TEST(r.members[0].key == "a");
}

// ---------------------------------------------------------------------------
// scan_array
// ---------------------------------------------------------------------------

BOOST_AUTO_TEST_CASE(scan_array_empty_array_ok)
{
    array_scan_result r = scan_array("[]");
    BOOST_TEST(r.ok);
    BOOST_TEST(r.elements.empty());
}

BOOST_AUTO_TEST_CASE(scan_array_mixed_elements_classified)
{
    array_scan_result r = scan_array(R"(["a",1,{"k":[2]},null])");
    BOOST_TEST(r.ok);
    BOOST_REQUIRE_EQUAL(r.elements.size(), 4u);
    BOOST_TEST((r.elements[0].kind == json_kind::string));
    BOOST_TEST(r.elements[0].raw == "\"a\"");
    BOOST_TEST((r.elements[1].kind == json_kind::number));
    BOOST_TEST(r.elements[1].raw == "1");
    BOOST_TEST((r.elements[2].kind == json_kind::object));
    BOOST_TEST(r.elements[2].raw == R"({"k":[2]})");
    BOOST_TEST((r.elements[3].kind == json_kind::null));
}

BOOST_AUTO_TEST_CASE(scan_array_nested_array_kept_as_raw_text)
{
    array_scan_result r = scan_array("[[1,2],[3]]");
    BOOST_TEST(r.ok);
    BOOST_REQUIRE_EQUAL(r.elements.size(), 2u);
    BOOST_TEST((r.elements[0].kind == json_kind::array));
    BOOST_TEST(r.elements[0].raw == "[1,2]");
    BOOST_TEST(r.elements[1].raw == "[3]");
}

BOOST_AUTO_TEST_CASE(scan_array_trailing_comma_is_malformed)
{
    array_scan_result r = scan_array("[1,2,]");
    BOOST_TEST(!r.ok);
    BOOST_TEST(r.error == "malformed");
}

BOOST_AUTO_TEST_CASE(scan_array_object_input_is_not_an_array)
{
    array_scan_result r = scan_array(R"({"a":1})");
    BOOST_TEST(!r.ok);
    BOOST_TEST(r.error == "not_an_array");
}

BOOST_AUTO_TEST_CASE(scan_array_too_many_elements_rejected)
{
    scan_limits limits;
    limits.max_members = 3;
    array_scan_result r = scan_array("[1,2,3,4]", limits);
    BOOST_TEST(!r.ok);
    BOOST_TEST(r.error == "too_many_members");
}

// ---------------------------------------------------------------------------
// Robustness
// ---------------------------------------------------------------------------

BOOST_AUTO_TEST_CASE(scan_object_one_megabyte_of_brackets_is_too_large)
{
    std::string text(1024 * 1024, '[');
    scan_result r;
    BOOST_CHECK_NO_THROW(r = scan_object(text));
    BOOST_TEST(!r.ok);
    BOOST_TEST(r.error == "too_large");
}

BOOST_AUTO_TEST_CASE(scan_object_deeply_nested_within_bytes_does_not_crash)
{
    // Under the byte cap but far past the depth cap: must return too_deep,
    // never recurse or crash.
    std::string value(5000, '[');
    const std::string text = std::string(R"({"a":)") + value + "}";
    scan_result r;
    BOOST_CHECK_NO_THROW(r = scan_object(text));
    BOOST_TEST(!r.ok);
    BOOST_TEST(r.error == "too_deep");
}

BOOST_AUTO_TEST_CASE(scan_object_truncated_prefixes_never_ok_never_crash)
{
    const std::string sample =
        R"({"v":1,"id":"c-1","a":{"k":[1,2]},"s":"x\ty"})";
    for (std::size_t len = 0; len < sample.size(); ++len) {
        scan_result r;
        BOOST_CHECK_NO_THROW(
            r = scan_object(std::string_view(sample).substr(0, len)));
        BOOST_TEST(!r.ok, "prefix length " << len << " must not be ok");
    }
    // The full document is the only accepted form.
    BOOST_TEST(scan_object(sample).ok);
}
