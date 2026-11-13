#define BOOST_TEST_MODULE CommandsResponseSerializeTest
#include <string>
#include <string_view>
#include <vector>

#include <boost/test/unit_test.hpp>

#include "url_shortener/commands/envelope.hpp"
#include "url_shortener/commands/error_codes.hpp"
#include "url_shortener/commands/json_scan.hpp"

using url_shortener::commands::all_error_infos;
using url_shortener::commands::CommandError;
using url_shortener::commands::CommandErrorCode;
using url_shortener::commands::CommandResponse;
using url_shortener::commands::error_info;
using url_shortener::commands::exit_code_for;
using url_shortener::commands::http_status_for;
using url_shortener::commands::JobInfo;
using url_shortener::commands::JobState;
using url_shortener::commands::make_error;
using url_shortener::commands::make_failure;
using url_shortener::commands::make_job_accepted;
using url_shortener::commands::make_success;
using url_shortener::commands::parse_response;
using url_shortener::commands::parse_response_result;
using url_shortener::commands::scan_object;
using url_shortener::commands::serialize_response;

namespace
{

// Confirms @p text is still well-formed JSON after serialization: a serializer
// bug can only ever emit a single object, never truncated or broken JSON.
void expect_valid_json_object(const std::string& text)
{
    BOOST_TEST(scan_object(text).ok);
}

// The canonical `internal` fallback envelope for a given echoed id.
std::string internal_envelope(const std::string& id)
{
    return R"({"v":1,"id":")" + id
        + R"(","ok":false,"error":{"code":"internal",)"
        + R"("message":"internal error"}})";
}

}  // namespace

// ---------------------------------------------------------------------------
// Byte-exact catalog examples (success criteria 2)
// ---------------------------------------------------------------------------

// Scenario: the three section-1 examples serialize byte-for-byte, fixing field
// order v, id, ok, then result | job | error.
// Boundaries: empty result object; a queued job; a failure with a message.
BOOST_AUTO_TEST_CASE(catalog_examples_are_byte_exact)
{
    BOOST_TEST(serialize_response(make_success("c-42", "{}"))
               == R"({"v":1,"id":"c-42","ok":true,"result":{}})");

    JobInfo job;
    job.id = "j-9";
    job.state = JobState::queued;
    BOOST_TEST(
        serialize_response(make_job_accepted("c-43", job))
        == R"({"v":1,"id":"c-43","ok":true,"job":{"id":"j-9","state":"queued"}})");

    BOOST_TEST(
        serialize_response(make_failure(
            "c-42",
            make_error(CommandErrorCode::slug_conflict, "slug already exists")))
        == R"({"v":1,"id":"c-42","ok":false,"error":{"code":"slug_conflict","message":"slug already exists"}})");
}

// ---------------------------------------------------------------------------
// result_json embedded verbatim (milestone contract C1)
// ---------------------------------------------------------------------------

// Scenario: a result carrying quotes, a unicode escape and nested braces is
// embedded exactly as produced, never re-escaped.
// Boundaries: pre-escaped quote, `é` sequence, nested object.
BOOST_AUTO_TEST_CASE(result_json_is_embedded_verbatim)
{
    const std::string result =
        R"({"a":"he said \"hi\"","u":"café","n":{"x":1}})";
    const std::string out = serialize_response(make_success("c-1", result));
    BOOST_TEST(out
               == R"({"v":1,"id":"c-1","ok":true,"result":)" + result + "}");
    // The verbatim payload still leaves the whole envelope valid JSON.
    expect_valid_json_object(out);
}

// ---------------------------------------------------------------------------
// Message escaping (threat model T-17 / contract C6)
// ---------------------------------------------------------------------------

// Scenario: a message with a quote, backslash, newline and a control character
// always produces valid JSON (control chars are neutralized, quotes/backslash
// escaped).
// Boundaries: `"`, `\`, `\n` (0x0A), NUL and 0x01 control bytes.
BOOST_AUTO_TEST_CASE(error_message_is_escaped_to_valid_json)
{
    std::string message = "a\"b\\c\nd";
    message.push_back('\0');
    message.push_back('\x01');
    const std::string out = serialize_response(
        make_failure("c-1", make_error(CommandErrorCode::internal, message)));
    expect_valid_json_object(out);
    // The raw control bytes never reach the wire.
    BOOST_TEST(out.find('\n') == std::string::npos);
    BOOST_TEST(out.find('\0') == std::string::npos);
}

// ---------------------------------------------------------------------------
// Invariant violations -> internal envelope (threat model T-17)
// ---------------------------------------------------------------------------

// Scenario: neither/both payloads, an ok-with-error response, and a result that
// is not a single well-formed JSON value all yield a well-formed `internal`
// envelope, never malformed JSON.
// Boundaries: 0 payloads, 2 payloads, ok/error mismatch, result that is not a
// JSON value.
BOOST_AUTO_TEST_CASE(invariant_violations_emit_internal_envelope)
{
    // Zero payloads: default response (ok == false, no error set).
    CommandResponse none;
    none.id = "c-1";
    BOOST_TEST(serialize_response(none) == internal_envelope("c-1"));
    expect_valid_json_object(serialize_response(none));

    // Two payloads: both result and job set.
    CommandResponse both;
    both.id = "c-1";
    both.ok = true;
    both.result_json = "{}";
    JobInfo job;
    job.id = "j-1";
    both.job = job;
    BOOST_TEST(serialize_response(both) == internal_envelope("c-1"));

    // ok == true with an error payload set.
    CommandResponse ok_with_error;
    ok_with_error.id = "c-1";
    ok_with_error.ok = true;
    ok_with_error.error = make_error(CommandErrorCode::not_found, "x");
    BOOST_TEST(serialize_response(ok_with_error) == internal_envelope("c-1"));

    // Result that is not a JSON value at all.
    CommandResponse bad_result;
    bad_result.id = "c-1";
    bad_result.ok = true;
    bad_result.result_json = "oops";
    BOOST_TEST(serialize_response(bad_result) == internal_envelope("c-1"));
    expect_valid_json_object(serialize_response(bad_result));
}

// ---------------------------------------------------------------------------
// result_json single-value validation (threat model T-17 / CWE-116)
// ---------------------------------------------------------------------------

// Scenario: a result payload that is not exactly one well-formed JSON value is
// rejected with the well-formed `internal` envelope, closing result-envelope
// smuggling. Validation goes beyond a first-character check: a payload that
// merely begins with a JSON-value character but carries a sibling key or
// trailing data must never be embedded verbatim.
// Boundaries: object + injected sibling key; malformed trailing after an
// object; a string that smuggles a sibling key; trailing garbage after a valid
// string, number and literal.
// On failure first check: result_json_is_single_value / the synthetic
// `{"r":<payload>}` scan in serialize_response.
BOOST_AUTO_TEST_CASE(result_json_smuggling_and_trailing_data_rejected)
{
    const auto rejected = [](const std::string& result)
    {
        CommandResponse r;
        r.id = "c-1";
        r.ok = true;
        r.result_json = result;
        const std::string out = serialize_response(r);
        expect_valid_json_object(out);
        return out == internal_envelope("c-1");
    };

    // The two canonical smuggling payloads from the threat model.
    BOOST_TEST(rejected(R"({"a":1},"injected":true)"));
    BOOST_TEST(rejected(R"({"a":1}, "ok": false, "x": {)"));

    // A string that starts and ends with a quote but smuggles a sibling key.
    BOOST_TEST(rejected(R"("a","injected":"true")"));

    // Trailing garbage after an otherwise valid string / number / literal.
    BOOST_TEST(rejected(R"("hello" garbage)"));
    BOOST_TEST(rejected("42 garbage"));
    BOOST_TEST(rejected("true garbage"));
    BOOST_TEST(rejected("null null"));
}

// Scenario: a large but well-formed result (a 2000-element array) is accepted
// and embedded verbatim, then round-trips. The result-specific scan limits
// (k_result_scan_limits) deliberately admit payloads far larger and wider than
// a request envelope (paged listings, db rows).
// On failure first check: k_result_scan_limits in envelope.hpp.
BOOST_AUTO_TEST_CASE(large_valid_result_accepted)
{
    std::string array = "[";
    for (int i = 0; i < 2000; ++i) {
        if (i != 0) {
            array.push_back(',');
        }
        array += std::to_string(i);
    }
    array.push_back(']');

    const std::string out = serialize_response(make_success("c-1", array));
    BOOST_TEST(out == R"({"v":1,"id":"c-1","ok":true,"result":)" + array + "}");
    expect_valid_json_object(out);

    const parse_response_result parsed = parse_response(out);
    BOOST_REQUIRE(parsed.response.has_value());
    BOOST_TEST(parsed.response->result_json == array);
}

// ---------------------------------------------------------------------------
// Round trips (success criteria 3)
// ---------------------------------------------------------------------------

// Scenario: parse_response is the exact inverse of serialize_response for a
// success result.
BOOST_AUTO_TEST_CASE(round_trip_success)
{
    const CommandResponse original = make_success("c-7", R"({"k":1})");
    const parse_response_result parsed =
        parse_response(serialize_response(original));
    BOOST_REQUIRE(parsed.response.has_value());
    BOOST_TEST(parsed.error.empty());
    BOOST_TEST(parsed.response->ok);
    BOOST_TEST(parsed.response->id == "c-7");
    BOOST_TEST(parsed.response->result_json == R"({"k":1})");
}

// Scenario: round trip holds for every JobState, preserving id and state.
BOOST_AUTO_TEST_CASE(round_trip_job_all_states)
{
    const std::vector<JobState> states {JobState::queued,
                                        JobState::running,
                                        JobState::succeeded,
                                        JobState::failed,
                                        JobState::cancelled};
    for (const JobState state : states) {
        JobInfo job;
        job.id = "j-3";
        job.state = state;
        const parse_response_result parsed =
            parse_response(serialize_response(make_job_accepted("c-3", job)));
        BOOST_REQUIRE(parsed.response.has_value());
        BOOST_REQUIRE(parsed.response->job.has_value());
        BOOST_TEST(parsed.response->ok);
        BOOST_TEST(parsed.response->job->id == "j-3");
        BOOST_TEST((parsed.response->job->state == state));
    }
}

// Scenario: round trip holds for a failure of every CommandErrorCode except
// none, preserving code and message.
BOOST_AUTO_TEST_CASE(round_trip_failure_every_code)
{
    for (const error_info& row : all_error_infos()) {
        if (row.code == CommandErrorCode::none) {
            continue;
        }
        const CommandResponse original =
            make_failure("c-9", make_error(row.code, "detail"));
        const parse_response_result parsed =
            parse_response(serialize_response(original));
        BOOST_REQUIRE(parsed.response.has_value());
        BOOST_TEST(!parsed.response->ok);
        BOOST_TEST((parsed.response->error.code == row.code));
        BOOST_TEST(parsed.response->error.message == "detail");
    }
}

// ---------------------------------------------------------------------------
// parse_response rejections
// ---------------------------------------------------------------------------

// Scenario: parse_response rejects the malformed envelope shapes a conformance
// client must detect.
// Boundaries: two payload fields, zero payload fields, non-boolean ok, unknown
// error code, unknown job state.
BOOST_AUTO_TEST_CASE(parse_response_rejects_bad_shapes)
{
    const auto rejected = [](std::string_view text)
    {
        const parse_response_result r = parse_response(text);
        return !r.response.has_value() && !r.error.empty();
    };

    BOOST_TEST(rejected(
        R"({"v":1,"id":"a","ok":true,"result":{},"job":{"id":"j","state":"queued"}})"));
    BOOST_TEST(rejected(R"({"v":1,"id":"a","ok":true})"));
    BOOST_TEST(rejected(R"({"v":1,"id":"a","ok":"true","result":{}})"));
    BOOST_TEST(rejected(
        R"({"v":1,"id":"a","ok":false,"error":{"code":"nope","message":"x"}})"));
    BOOST_TEST(rejected(
        R"({"v":1,"id":"a","ok":true,"job":{"id":"j","state":"flying"}})"));
}

// Scenario: parse_response neutralizes control bytes smuggled into
// error.message through a `\uXXXX` escape (LOW) so a decoded ESC sequence can
// never drive a TUI terminal; the decoded message is routed through
// sanitize_error_message before it is stored.
// Boundaries: a `\u001b` (ESC, a C0 control) in the middle of the message.
// On failure first check: the sanitize_error_message call on the error branch
// of parse_response.
BOOST_AUTO_TEST_CASE(parse_response_sanitizes_error_message)
{
    const parse_response_result parsed =
        parse_response(R"({"v":1,"id":"c-1","ok":false,)"
                       R"("error":{"code":"internal","message":"a\u001bb"}})");
    BOOST_REQUIRE(parsed.response.has_value());
    BOOST_TEST(!parsed.response->ok);
    BOOST_TEST(parsed.response->error.message == "a?b");
}

// ---------------------------------------------------------------------------
// Status and exit-code mapping (subtask 02 table only)
// ---------------------------------------------------------------------------

// Scenario: HTTP status and CLI exit code come only from the error table; ok
// and accepted-job responses are 200/0 regardless of any residual error.
// Boundaries: ok, job accepted, not_found (404/1), busy (503/4), job_cancelled
// (200/4).
BOOST_AUTO_TEST_CASE(status_and_exit_codes_follow_the_table)
{
    const CommandResponse ok = make_success("c-1", "{}");
    BOOST_TEST(http_status_for(ok) == 200u);
    BOOST_TEST(exit_code_for(ok) == 0);

    JobInfo job;
    job.id = "j-1";
    const CommandResponse accepted = make_job_accepted("c-1", job);
    BOOST_TEST(http_status_for(accepted) == 200u);
    BOOST_TEST(exit_code_for(accepted) == 0);

    const CommandResponse not_found =
        make_failure("c-1", make_error(CommandErrorCode::not_found, "x"));
    BOOST_TEST(http_status_for(not_found) == 404u);
    BOOST_TEST(exit_code_for(not_found) == 1);

    const CommandResponse busy =
        make_failure("c-1", make_error(CommandErrorCode::busy, "x"));
    BOOST_TEST(http_status_for(busy) == 503u);
    BOOST_TEST(exit_code_for(busy) == 4);

    const CommandResponse cancelled =
        make_failure("c-1", make_error(CommandErrorCode::job_cancelled, "x"));
    BOOST_TEST(http_status_for(cancelled) == 200u);
    BOOST_TEST(exit_code_for(cancelled) == 4);
}
