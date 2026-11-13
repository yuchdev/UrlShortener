#define BOOST_TEST_MODULE CommandsRequestParseTest
#include <boost/test/unit_test.hpp>

#include <atomic>
#include <set>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include "url_shortener/commands/envelope.hpp"
#include "url_shortener/commands/error_codes.hpp"

using url_shortener::commands::CommandErrorCode;
using url_shortener::commands::envelope_limits;
using url_shortener::commands::is_valid_command_name;
using url_shortener::commands::next_server_id;
using url_shortener::commands::parse_request;
using url_shortener::commands::parse_request_result;
using url_shortener::commands::sanitize_error_message;

namespace
{

// Asserts a failed parse: invalid_envelope, no request, and a message that
// never contains any byte of @p forbidden (the "no echo of input" guarantee).
void expect_invalid(const parse_request_result& result,
                    std::string_view forbidden = {})
{
    BOOST_TEST(!result.ok());
    BOOST_TEST(!result.request.has_value());
    BOOST_TEST((result.error.code == CommandErrorCode::invalid_envelope));
    BOOST_TEST(!result.error.message.empty());
    if (!forbidden.empty())
    {
        BOOST_TEST(result.error.message.find(forbidden) == std::string::npos);
    }
}

} // namespace

// ---------------------------------------------------------------------------
// Catalog round-trips (success criteria 2)
// ---------------------------------------------------------------------------

// Scenario: the fully-populated section-1 example parses with every field.
// Boundaries: explicit id, empty args object, both option flags present.
BOOST_AUTO_TEST_CASE(catalog_section1_example_round_trips)
{
    const parse_request_result r = parse_request(
        R"({"v":1,"id":"c-42","command":"link.create","args":{},)"
        R"("options":{"dry_run":false,"async":false}})");
    BOOST_REQUIRE(r.ok());
    BOOST_TEST(r.request->v == 1);
    BOOST_TEST(r.request->id == "c-42");
    BOOST_TEST(r.id == "c-42");
    BOOST_TEST(r.request->command == "link.create");
    BOOST_TEST(r.request->args_json == "{}");
    BOOST_TEST(!r.request->options.dry_run);
    BOOST_TEST(!r.request->options.async);
}

// Scenario: the section-2 worked example with a nested args object parses and
// the args text is preserved byte-for-byte.
BOOST_AUTO_TEST_CASE(catalog_section2_worked_example_round_trips)
{
    const std::string args =
        R"({"target_url":"https://example.com/docs","slug":"docs"})";
    const parse_request_result r = parse_request(
        R"({"v":1,"command":"link.create","args":)" + args + "}");
    BOOST_REQUIRE(r.ok());
    BOOST_TEST(r.request->command == "link.create");
    BOOST_TEST(r.request->args_json == args);
}

// Scenario: the minimal legal envelope generates a server id and defaults.
// Boundaries: no id, no args, no options.
BOOST_AUTO_TEST_CASE(minimal_envelope_generates_server_id_and_defaults)
{
    const parse_request_result r =
        parse_request(R"({"v":1,"command":"command.list"})");
    BOOST_REQUIRE(r.ok());
    BOOST_TEST(r.request->id.rfind("srv-", 0) == 0u);
    BOOST_TEST(r.id == r.request->id);
    BOOST_TEST(r.request->args_json == "{}");
    BOOST_TEST(!r.request->options.dry_run);
    BOOST_TEST(!r.request->options.async);
}

// ---------------------------------------------------------------------------
// id rules
// ---------------------------------------------------------------------------

// Scenario: a valid client id is echoed unchanged; 64 bytes accepted.
// Boundaries: length exactly max_id_len.
BOOST_AUTO_TEST_CASE(id_echoed_and_64_chars_accepted)
{
    const std::string id(64, 'a');
    const parse_request_result r = parse_request(
        R"({"v":1,"id":")" + id + R"(","command":"link.get"})");
    BOOST_REQUIRE(r.ok());
    BOOST_TEST(r.request->id == id);
    BOOST_TEST(r.id == id);
}

// Scenario: an over-length id (65) is rejected and the echoed id is a server
// fallback, not the client value.
BOOST_AUTO_TEST_CASE(id_65_chars_rejected_and_not_echoed)
{
    const std::string id(65, 'a');
    const parse_request_result r = parse_request(
        R"({"v":1,"id":")" + id + R"(","command":"link.get"})");
    expect_invalid(r, id);
    BOOST_TEST(r.id.rfind("srv-", 0) == 0u);
}

// Scenario: an empty-string id is rejected.
BOOST_AUTO_TEST_CASE(id_empty_string_rejected)
{
    const parse_request_result r =
        parse_request(R"({"v":1,"id":"","command":"link.get"})");
    expect_invalid(r);
    BOOST_TEST(r.id.rfind("srv-", 0) == 0u);
}

// Scenario: a control character in the id is rejected, the hostile id never
// reaches the echoed id, and the message contains no byte of it (T-02/T-05).
BOOST_AUTO_TEST_CASE(id_control_char_rejected_and_not_echoed)
{
    const parse_request_result r = parse_request(
        "{\"v\":1,\"id\":\"a\\u0007b\",\"command\":\"link.get\"}");
    expect_invalid(r);
    BOOST_TEST(r.id.rfind("srv-", 0) == 0u);
    BOOST_TEST(r.id.find('\a') == std::string::npos);
    BOOST_TEST(r.error.message.find('\a') == std::string::npos);
}

// Scenario: an id of the wrong JSON type is rejected.
BOOST_AUTO_TEST_CASE(id_wrong_type_rejected)
{
    const parse_request_result r =
        parse_request(R"({"v":1,"id":5,"command":"link.get"})");
    expect_invalid(r);
    BOOST_TEST(r.id.rfind("srv-", 0) == 0u);
}

// ---------------------------------------------------------------------------
// id re-validation on the error path (threat model T-05)
// ---------------------------------------------------------------------------

// Scenario: a valid id with another bad field still echoes the client id.
BOOST_AUTO_TEST_CASE(valid_id_with_bad_field_echoes_client_id)
{
    const parse_request_result r =
        parse_request(R"({"v":2,"id":"keep-me","command":"link.get"})");
    expect_invalid(r);
    BOOST_TEST(r.id == "keep-me");
}

// Scenario: a hostile (control-char) id combined with a bad field yields a
// server id, never the hostile value, with no input echoed in the message.
BOOST_AUTO_TEST_CASE(hostile_id_with_bad_field_falls_back_to_server_id)
{
    const parse_request_result r = parse_request(
        "{\"v\":2,\"id\":\"ev\\nil\",\"command\":\"link.get\"}");
    expect_invalid(r);
    BOOST_TEST(r.id.rfind("srv-", 0) == 0u);
    BOOST_TEST(r.id.find('\n') == std::string::npos);
    BOOST_TEST(r.error.message.find('\n') == std::string::npos);
}

// ---------------------------------------------------------------------------
// command rules
// ---------------------------------------------------------------------------

// Scenario: a missing command is rejected.
BOOST_AUTO_TEST_CASE(command_missing_rejected)
{
    expect_invalid(parse_request(R"({"v":1})"));
}

// Scenario: a command of the wrong JSON type is rejected.
BOOST_AUTO_TEST_CASE(command_not_a_string_rejected)
{
    expect_invalid(parse_request(R"({"v":1,"command":5})"));
}

// Scenario: each malformed command name shape is rejected.
// Boundaries: upper-case, single segment, doubled/leading dot, 65-char name.
BOOST_AUTO_TEST_CASE(command_bad_names_rejected)
{
    const std::string long_name = "a." + std::string(63, 'b'); // 65 bytes
    for (const std::string& name :
         {std::string("Link.Create"), std::string("link"),
          std::string("link..x"), std::string(".x"), long_name})
    {
        const parse_request_result r =
            parse_request(R"({"v":1,"command":")" + name + R"("})");
        BOOST_TEST(!r.ok(), "expected rejection of command name: " << name);
        if (!r.ok())
        {
            BOOST_TEST((r.error.code == CommandErrorCode::invalid_envelope));
        }
    }
}

// Scenario: is_valid_command_name accepts/rejects the grammar directly.
BOOST_AUTO_TEST_CASE(is_valid_command_name_grammar)
{
    BOOST_TEST(is_valid_command_name("link.create"));
    BOOST_TEST(is_valid_command_name("a.b.c"));
    BOOST_TEST(is_valid_command_name("link.get_by_id"));
    BOOST_TEST(!is_valid_command_name(""));
    BOOST_TEST(!is_valid_command_name("link"));
    BOOST_TEST(!is_valid_command_name("Link.Create"));
    BOOST_TEST(!is_valid_command_name("link..x"));
    BOOST_TEST(!is_valid_command_name(".x"));
    BOOST_TEST(!is_valid_command_name("link."));
    BOOST_TEST(!is_valid_command_name("1link.create"));
    BOOST_TEST(!is_valid_command_name("link.1create"));
}

// ---------------------------------------------------------------------------
// v rules
// ---------------------------------------------------------------------------

// Scenario: every non-`1` form of v is rejected as invalid_envelope.
// Boundaries: missing, 0, 2, string "1", float 1.0, boolean true.
BOOST_AUTO_TEST_CASE(v_bad_values_rejected)
{
    expect_invalid(parse_request(R"({"command":"link.get"})"));
    expect_invalid(parse_request(R"({"v":0,"command":"link.get"})"));
    expect_invalid(parse_request(R"({"v":2,"command":"link.get"})"));
    expect_invalid(parse_request(R"({"v":"1","command":"link.get"})"));
    expect_invalid(parse_request(R"({"v":1.0,"command":"link.get"})"));
    expect_invalid(parse_request(R"({"v":true,"command":"link.get"})"));
}

// ---------------------------------------------------------------------------
// args rules
// ---------------------------------------------------------------------------

// Scenario: an args value that is not an object is rejected.
// Boundaries: array, string, number, null.
BOOST_AUTO_TEST_CASE(args_wrong_type_rejected)
{
    expect_invalid(parse_request(R"({"v":1,"command":"c.x","args":[1,2]})"));
    expect_invalid(parse_request(R"({"v":1,"command":"c.x","args":"s"})"));
    expect_invalid(parse_request(R"({"v":1,"command":"c.x","args":5})"));
    expect_invalid(parse_request(R"({"v":1,"command":"c.x","args":null})"));
}

// Scenario: a nested args object is preserved byte-for-byte.
BOOST_AUTO_TEST_CASE(args_nested_object_preserved_verbatim)
{
    const std::string args = R"({"a":{"b":[1,2,3]},"c":"x"})";
    const parse_request_result r =
        parse_request(R"({"v":1,"command":"c.x","args":)" + args + "}");
    BOOST_REQUIRE(r.ok());
    BOOST_TEST(r.request->args_json == args);
}

// ---------------------------------------------------------------------------
// options rules
// ---------------------------------------------------------------------------

// Scenario: both option flags set true are parsed.
BOOST_AUTO_TEST_CASE(options_both_flags_true)
{
    const parse_request_result r = parse_request(
        R"({"v":1,"command":"c.x","options":{"dry_run":true,"async":true}})");
    BOOST_REQUIRE(r.ok());
    BOOST_TEST(r.request->options.dry_run);
    BOOST_TEST(r.request->options.async);
}

// Scenario: an option flag of the wrong type is rejected.
BOOST_AUTO_TEST_CASE(options_wrong_type_rejected)
{
    expect_invalid(parse_request(
        R"({"v":1,"command":"c.x","options":{"dry_run":"yes"}})"));
}

// Scenario: an unknown option key is rejected (closed options object).
BOOST_AUTO_TEST_CASE(options_unknown_key_rejected)
{
    expect_invalid(
        parse_request(R"({"v":1,"command":"c.x","options":{"foo":1}})"));
}

// Scenario: options of the wrong JSON type is rejected.
BOOST_AUTO_TEST_CASE(options_not_an_object_rejected)
{
    expect_invalid(parse_request(R"({"v":1,"command":"c.x","options":1})"));
}

// ---------------------------------------------------------------------------
// closed envelope (threat model T-04/T-05)
// ---------------------------------------------------------------------------

// Scenario: an unknown top-level key is rejected.
BOOST_AUTO_TEST_CASE(unknown_top_level_key_rejected)
{
    expect_invalid(parse_request(R"({"v":1,"command":"c.x","foo":1})"));
}

// Scenario: a duplicate top-level key is rejected (scanner duplicate_key).
BOOST_AUTO_TEST_CASE(duplicate_top_level_key_rejected)
{
    const parse_request_result r =
        parse_request(R"({"v":1,"command":"c.x","v":1})");
    expect_invalid(r);
    BOOST_TEST(r.error.message == "duplicate_key");
}

// Scenario: a duplicate key inside options is rejected.
BOOST_AUTO_TEST_CASE(duplicate_options_key_rejected)
{
    const parse_request_result r = parse_request(
        R"({"v":1,"command":"c.x","options":{"dry_run":true,"dry_run":false}})");
    expect_invalid(r);
    BOOST_TEST(r.error.message == "duplicate_key");
}

// ---------------------------------------------------------------------------
// malformed / oversize input
// ---------------------------------------------------------------------------

// Scenario: non-JSON, empty text and a top-level array are all rejected and an
// unparseable body gets a server-generated echo id.
BOOST_AUTO_TEST_CASE(non_json_empty_and_array_rejected)
{
    for (const std::string& text : {std::string("not json"), std::string(),
                                    std::string("[1,2,3]")})
    {
        const parse_request_result r = parse_request(text);
        expect_invalid(r);
        BOOST_TEST(r.id.rfind("srv-", 0) == 0u);
    }
}

// Scenario: an over-limit (70 KB) body is rejected as too_large.
BOOST_AUTO_TEST_CASE(oversize_body_rejected)
{
    std::string text = R"({"v":1,"command":"c.x","args":{"x":")";
    text.append(70 * 1024, 'a');
    text += "\"}}";
    const parse_request_result r = parse_request(text);
    expect_invalid(r);
    BOOST_TEST(r.error.message == "too_large");
}

// ---------------------------------------------------------------------------
// no-echo of sensitive values (success criteria 3)
// ---------------------------------------------------------------------------

// Scenario: a recognizable marker value in args with a bad v never appears in
// the error message (stands in for a secret; value-free messages, T-02).
BOOST_AUTO_TEST_CASE(marker_in_args_never_echoed_on_failure)
{
    const std::string needle = "RECOGNIZABLE-NEEDLE-VALUE-9999";
    const parse_request_result r = parse_request(
        R"({"v":9,"command":"c.x","args":{"token":")" + needle + R"("}})");
    expect_invalid(r, needle);
}

// ---------------------------------------------------------------------------
// sanitize_error_message helper (carry-forward from subtask 02 review)
// ---------------------------------------------------------------------------

// Scenario: control characters are replaced and length bounded to 128.
BOOST_AUTO_TEST_CASE(sanitize_error_message_neutralizes_and_bounds)
{
    const std::string out = sanitize_error_message("a\r\nb\tc\x7f");
    BOOST_TEST(out == "a??b?c?");
    const std::string long_in(300, 'x');
    BOOST_TEST(sanitize_error_message(long_in).size() == 128u);
    BOOST_TEST(sanitize_error_message("plain text").find('?') ==
               std::string::npos);
}

// Scenario: invalid UTF-8 and C1 controls are neutralized byte-for-byte and the
// 128-byte cap never splits a multi-byte character (security review LOW x2 on
// 01.0/03; threat-model T-03).
BOOST_AUTO_TEST_CASE(sanitize_error_message_is_utf8_safe)
{
    // Valid multi-byte characters pass through unchanged.
    BOOST_TEST(sanitize_error_message("caf\xC3\xA9 \xE2\x82\xAC \xF0\x9F\x98\x80")
               == "caf\xC3\xA9 \xE2\x82\xAC \xF0\x9F\x98\x80");

    // Stray continuation byte, invalid lead bytes.
    BOOST_TEST(sanitize_error_message("a\x80" "b") == "a?b");
    BOOST_TEST(sanitize_error_message("\xC0\xAF") == "??");  // overlong '/'
    BOOST_TEST(sanitize_error_message("\xFF") == "?");

    // Surrogate, above U+10FFFF, truncated sequence at end of input.
    BOOST_TEST(sanitize_error_message("\xED\xA0\x80") == "???");
    BOOST_TEST(sanitize_error_message("\xF4\x90\x80\x80") == "????");
    BOOST_TEST(sanitize_error_message("x\xE2\x82") == "x??");

    // C1 control U+0085 (NEL) is a line break in some log viewers.
    BOOST_TEST(sanitize_error_message("a\xC2\x85" "b") == "a?b");

    // 127 ASCII bytes + a 3-byte character: the character does not fit and
    // is dropped whole rather than split.
    const std::string near_cap = std::string(127, 'x') + "\xE2\x82\xAC";
    const std::string cut = sanitize_error_message(near_cap);
    BOOST_TEST(cut == std::string(127, 'x'));

    // 126 ASCII bytes + a 2-byte character fits exactly at 128.
    const std::string exact = std::string(126, 'x') + "\xC3\xA9";
    BOOST_TEST(sanitize_error_message(exact) == exact);
}

// ---------------------------------------------------------------------------
// next_server_id uniqueness / monotonicity
// ---------------------------------------------------------------------------

// Scenario: 1000 sequential calls are strictly increasing and srv-prefixed.
BOOST_AUTO_TEST_CASE(next_server_id_monotonic)
{
    std::uint64_t previous = 0;
    for (int i = 0; i < 1000; ++i)
    {
        const std::string id = next_server_id();
        BOOST_REQUIRE(id.rfind("srv-", 0) == 0u);
        const std::uint64_t n = std::stoull(id.substr(4));
        BOOST_TEST(n > previous);
        previous = n;
    }
}

// Scenario: ids drawn concurrently from 4 threads are all unique.
BOOST_AUTO_TEST_CASE(next_server_id_unique_across_threads)
{
    constexpr int threads = 4;
    constexpr int per_thread = 500;
    std::vector<std::vector<std::string>> buckets(threads);
    std::vector<std::thread> workers;
    for (int t = 0; t < threads; ++t)
    {
        workers.emplace_back([&buckets, t]() {
            buckets[t].reserve(per_thread);
            for (int i = 0; i < per_thread; ++i)
            {
                buckets[t].push_back(next_server_id());
            }
        });
    }
    for (std::thread& worker : workers)
    {
        worker.join();
    }
    std::set<std::string> all;
    for (const std::vector<std::string>& bucket : buckets)
    {
        for (const std::string& id : bucket)
        {
            all.insert(id);
        }
    }
    BOOST_TEST(all.size() == static_cast<std::size_t>(threads * per_thread));
}
