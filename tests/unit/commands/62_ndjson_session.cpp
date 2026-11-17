#define BOOST_TEST_MODULE CommandsNdjsonSessionTest
#include <cstddef>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <streambuf>
#include <string>
#include <variant>
#include <vector>

#include <boost/test/unit_test.hpp>

#include "url_shortener/commands/arg_spec.hpp"
#include "url_shortener/commands/command_descriptor.hpp"
#include "url_shortener/commands/command_registry.hpp"
#include "url_shortener/commands/envelope.hpp"
#include "url_shortener/commands/error_codes.hpp"
#include "url_shortener/commands/job_submitter.hpp"
#include "url_shortener/commands/ndjson_session.hpp"
#include "url_shortener/core/utils.hpp"

using url_shortener::commands::ArgSpec;
using url_shortener::commands::ArgsView;
using url_shortener::commands::ArgType;
using url_shortener::commands::CallerContext;
using url_shortener::commands::CommandContext;
using url_shortener::commands::CommandDescriptor;
using url_shortener::commands::CommandError;
using url_shortener::commands::CommandErrorCode;
using url_shortener::commands::CommandRegistry;
using url_shortener::commands::CommandResult;
using url_shortener::commands::ExecutionKind;
using url_shortener::commands::IJobContext;
using url_shortener::commands::IJobSubmitter;
using url_shortener::commands::JobBody;
using url_shortener::commands::JobInfo;
using url_shortener::commands::JobOutcome;
using url_shortener::commands::JobState;
using url_shortener::commands::JobSubmitResult;
using url_shortener::commands::NdjsonSession;
using url_shortener::commands::parse_response;
using url_shortener::commands::parse_response_result;
using url_shortener::commands::Permission;
using url_shortener::commands::session_end;

namespace
{

/// A job submitter that accepts every body and reports a fixed queued job, so a
/// `kind=job` command yields an immediate `job` envelope without running work.
class AcceptingSubmitter final : public IJobSubmitter
{
public:
    JobSubmitResult submit(std::string, std::string, JobBody) override
    {
        JobSubmitResult result;
        JobInfo job;
        job.id = "j-1";
        job.state = JobState::queued;
        result.job = job;
        return result;
    }
};

/// Builds a sync descriptor with the given name, permission and handler.
CommandDescriptor sync_command(std::string name,
                               Permission permission,
                               url_shortener::commands::CommandHandler handler)
{
    CommandDescriptor descriptor;
    descriptor.name = std::move(name);
    descriptor.summary = "a test command";
    descriptor.permission = permission;
    descriptor.mutating = permission != Permission::read;
    descriptor.handler = std::move(handler);
    return descriptor;
}

/// Registers the handful of test-only commands every scenario shares and
/// installs an accepting job submitter.
void add_test_commands(CommandRegistry& registry)
{
    // test.echo: returns {"text":<escaped>} when a `text` arg is supplied, else
    // {"ok":true}. The escaping proves an arg value with a newline never leaks
    // a raw newline onto the wire.
    CommandDescriptor echo = sync_command(
        "test.echo", Permission::read,
        [](CommandContext&, const ArgsView& args) -> CommandResult
        {
            CommandResult result;
            const auto text = args.get_string("text");
            if (text) {
                result.result_json =
                    "{\"text\":" + url_shortener::jsonString(*text) + "}";
            }
            else {
                result.result_json = R"({"ok":true})";
            }
            return result;
        });
    ArgSpec text_arg;
    text_arg.name = "text";
    text_arg.type = ArgType::string;
    text_arg.required = false;
    echo.args = {text_arg};
    registry.add(std::move(echo));

    // test.write: a write-gated command, used to prove a User caller is mapped
    // to not_authorized inside execute (no throw into the session).
    registry.add(sync_command(
        "test.write", Permission::write,
        [](CommandContext&, const ArgsView&) -> CommandResult
        {
            CommandResult result;
            result.result_json = R"({"ok":true})";
            return result;
        }));

    // test.boom: a handler that throws, to prove the escape is mapped to
    // internal and the loop continues.
    registry.add(sync_command(
        "test.boom", Permission::read,
        [](CommandContext&, const ArgsView&) -> CommandResult
        { throw std::runtime_error("boom"); }));

    // test.job: a job-kind command; with a submitter installed it returns a job
    // envelope immediately.
    CommandDescriptor job;
    job.name = "test.job";
    job.summary = "a test job command";
    job.permission = Permission::read;
    job.kind = ExecutionKind::job;
    job.job_factory =
        [](const CommandContext&,
           const ArgsView&) -> std::variant<JobBody, CommandError>
    {
        return JobBody {[](IJobContext&) { return JobOutcome {}; }};
    };
    registry.add(std::move(job));

    registry.set_job_submitter(std::make_shared<AcceptingSubmitter>());
}

/// The result of running one scripted session.
struct session_run
{
    std::string output;
    session_end end;
    std::size_t responses;
};

/// Runs a session over @p input with the given caller and returns its output,
/// terminal reason and response count.
session_run run_session(const CommandRegistry& registry,
                        const CallerContext& caller,
                        std::string input,
                        std::size_t max_line_bytes = 65536)
{
    std::istringstream in(std::move(input));
    std::ostringstream out;
    NdjsonSession session(in, out, max_line_bytes);
    const session_end end = session.serve(registry, caller);
    return {out.str(), end, session.responses_written()};
}

/// Splits @p text on '\n' into its non-empty lines.
std::vector<std::string> lines_of(const std::string& text)
{
    std::vector<std::string> lines;
    std::string current;
    for (const char c : text) {
        if (c == '\n') {
            lines.push_back(current);
            current.clear();
        }
        else {
            current.push_back(c);
        }
    }
    if (!current.empty()) {
        lines.push_back(current);
    }
    return lines;
}

/// An Admin caller labelled for stdio.
CallerContext admin_caller()
{
    CallerContext caller;
    caller.control_set = ControlSet::Admin;
    caller.transport = "test";
    return caller;
}

/// A streambuf that writes to a string but fails every byte once @p max_lines
/// newlines have been emitted, simulating a parent that stops reading stdout.
class capped_ostreambuf final : public std::streambuf
{
public:
    explicit capped_ostreambuf(std::size_t max_lines) : max_lines_(max_lines) {}

    std::string data;
    std::size_t lines = 0;

protected:
    int_type overflow(int_type ch) override
    {
        if (ch == traits_type::eof()) {
            return ch;
        }
        if (lines >= max_lines_) {
            return traits_type::eof();  // refuse further output
        }
        const char c = traits_type::to_char_type(ch);
        data.push_back(c);
        if (c == '\n') {
            ++lines;
        }
        return ch;
    }

    std::streamsize xsputn(const char* s, std::streamsize n) override
    {
        for (std::streamsize i = 0; i < n; ++i) {
            if (overflow(traits_type::to_int_type(s[i]))
                == traits_type::eof()) {
                return i;
            }
        }
        return n;
    }

private:
    std::size_t max_lines_;
};

}  // namespace

// Type: unit (happy path, ordering, id echo).
// Scenario: three valid requests with distinct client ids produce three
//   response lines in order, each echoing its request id, loop ends at eof.
// Boundaries: one response per content line, strict order.
// On failure first check: resolve_echo_id and the single-response-per-line loop.

BOOST_AUTO_TEST_CASE(three_requests_three_ordered_responses)
{
    CommandRegistry registry;
    add_test_commands(registry);
    const std::string input =
        R"({"v":1,"id":"a","command":"test.echo"})" "\n"
        R"({"v":1,"id":"b","command":"test.echo"})" "\n"
        R"({"v":1,"id":"c","command":"test.echo"})" "\n";

    const session_run run = run_session(registry, admin_caller(), input);

    BOOST_TEST((run.end == session_end::eof));
    BOOST_TEST(run.responses == 3U);
    const std::vector<std::string> lines = lines_of(run.output);
    BOOST_REQUIRE(lines.size() == 3U);
    const char* expected_ids[] = {"a", "b", "c"};
    for (std::size_t i = 0; i < 3; ++i) {
        const parse_response_result parsed = parse_response(lines[i]);
        BOOST_REQUIRE(parsed.response.has_value());
        BOOST_TEST(parsed.response->ok == true);
        BOOST_TEST(parsed.response->id == expected_ids[i]);
    }
}

// Type: unit (server id assignment).
// Scenario: two valid requests with no id get deterministic session-local
//   srv-1, srv-2 ids assigned before execute.
// Boundaries: session-local counter, not the process-wide atomic.
// On failure first check: next_server_id counter base and override before
//   execute.

BOOST_AUTO_TEST_CASE(requests_without_id_get_session_srv_ids)
{
    CommandRegistry registry;
    add_test_commands(registry);
    const std::string input = R"({"v":1,"command":"test.echo"})" "\n"
                              R"({"v":1,"command":"test.echo"})" "\n";

    const session_run run = run_session(registry, admin_caller(), input);

    const std::vector<std::string> lines = lines_of(run.output);
    BOOST_REQUIRE(lines.size() == 2U);
    BOOST_TEST(parse_response(lines[0]).response->id == "srv-1");
    BOOST_TEST(parse_response(lines[1]).response->id == "srv-2");
}

// Type: unit (malformed line between valid lines).
// Scenario: a malformed JSON line between two valid requests yields a middle
//   invalid_envelope line and the loop continues; the third request is answered.
// Boundaries: a parse failure never ends the session.
// On failure first check: the read_result::line parse-failure branch.

BOOST_AUTO_TEST_CASE(malformed_line_does_not_end_session)
{
    CommandRegistry registry;
    add_test_commands(registry);
    const std::string input =
        R"({"v":1,"id":"first","command":"test.echo"})" "\n"
        "{not json" "\n"
        R"({"v":1,"id":"third","command":"test.echo"})" "\n";

    const session_run run = run_session(registry, admin_caller(), input);

    BOOST_TEST((run.end == session_end::eof));
    const std::vector<std::string> lines = lines_of(run.output);
    BOOST_REQUIRE(lines.size() == 3U);
    BOOST_TEST(parse_response(lines[0]).response->ok == true);
    const parse_response_result middle = parse_response(lines[1]);
    BOOST_REQUIRE(middle.response.has_value());
    BOOST_TEST(middle.response->ok == false);
    BOOST_TEST((middle.response->error.code
                == CommandErrorCode::invalid_envelope));
    BOOST_TEST(parse_response(lines[2]).response->ok == true);
    BOOST_TEST(parse_response(lines[2]).response->id == "third");
}

// Type: unit (id recovery from a semantically invalid envelope).
// Scenario: a well-formed object with a string id but no command fails as
//   invalid_envelope while still echoing the recovered client id.
// Boundaries: id recovery via scan_object, not the whole-envelope parse.
// On failure first check: resolve_echo_id string-kind + length rule.

BOOST_AUTO_TEST_CASE(invalid_envelope_echoes_recovered_id)
{
    CommandRegistry registry;
    add_test_commands(registry);
    const std::string input = R"({"v":1,"id":"c-7"})" "\n";

    const session_run run = run_session(registry, admin_caller(), input);

    const std::vector<std::string> lines = lines_of(run.output);
    BOOST_REQUIRE(lines.size() == 1U);
    const parse_response_result parsed = parse_response(lines[0]);
    BOOST_REQUIRE(parsed.response.has_value());
    BOOST_TEST(parsed.response->ok == false);
    BOOST_TEST((parsed.response->error.code
                == CommandErrorCode::invalid_envelope));
    BOOST_TEST(parsed.response->id == "c-7");
}

// Type: unit (bad version field).
// Scenario: each malformed `v` (2, a string, missing) is invalid_envelope.
// Boundaries: version rule inside parse.
// On failure first check: parse_request version handling and the mapping to
//   invalid_envelope.

BOOST_AUTO_TEST_CASE(bad_version_is_invalid_envelope)
{
    CommandRegistry registry;
    add_test_commands(registry);
    const std::string input =
        R"({"v":2,"id":"a","command":"test.echo"})" "\n"
        R"({"v":"x","id":"b","command":"test.echo"})" "\n"
        R"({"id":"c","command":"test.echo"})" "\n";

    const session_run run = run_session(registry, admin_caller(), input);

    const std::vector<std::string> lines = lines_of(run.output);
    BOOST_REQUIRE(lines.size() == 3U);
    for (const std::string& line : lines) {
        const parse_response_result parsed = parse_response(line);
        BOOST_REQUIRE(parsed.response.has_value());
        BOOST_TEST(parsed.response->ok == false);
        BOOST_TEST((parsed.response->error.code
                    == CommandErrorCode::invalid_envelope));
    }
}

// Type: unit (unknown command / unknown arg).
// Scenario: an unregistered command maps to unknown_command and an undeclared
//   arg to invalid_field.
// Boundaries: execute lookup and arg validation stages.
// On failure first check: whether execute was reached (parse succeeded).

BOOST_AUTO_TEST_CASE(unknown_command_and_unknown_arg)
{
    CommandRegistry registry;
    add_test_commands(registry);
    const std::string input =
        R"({"v":1,"id":"a","command":"test.nope"})" "\n"
        R"({"v":1,"id":"b","command":"test.echo","args":{"bogus":1}})" "\n";

    const session_run run = run_session(registry, admin_caller(), input);

    const std::vector<std::string> lines = lines_of(run.output);
    BOOST_REQUIRE(lines.size() == 2U);
    BOOST_TEST((parse_response(lines[0]).response->error.code
                == CommandErrorCode::unknown_command));
    BOOST_TEST((parse_response(lines[1]).response->error.code
                == CommandErrorCode::invalid_field));
}

// Type: unit (over-long line framing).
// Scenario: a line exceeding max_line_bytes is invalid_envelope "line too
//   long" and the next line is still served.
// Boundaries: too_long verdict with a server-generated id (bytes dropped).
// On failure first check: the read_result::too_long branch and next_server_id.

BOOST_AUTO_TEST_CASE(too_long_line_then_next_served)
{
    CommandRegistry registry;
    add_test_commands(registry);
    const std::string input =
        std::string(200, 'x') + "\n"
        + R"({"v":1,"id":"after","command":"test.echo"})" + "\n";

    const session_run run = run_session(registry, admin_caller(), input, 64);

    const std::vector<std::string> lines = lines_of(run.output);
    BOOST_REQUIRE(lines.size() == 2U);
    const parse_response_result first = parse_response(lines[0]);
    BOOST_TEST(first.response->ok == false);
    BOOST_TEST((first.response->error.code
                == CommandErrorCode::invalid_envelope));
    BOOST_TEST(first.response->id == "srv-1");
    BOOST_TEST(parse_response(lines[1]).response->id == "after");
}

// Type: unit (invalid-input: NUL byte line).
// Scenario: a line carrying a NUL byte is invalid_envelope and the next line is
//   served.
// Boundaries: invalid verdict distinct from too_long and from a clean line.
// On failure first check: the read_result::invalid branch.

BOOST_AUTO_TEST_CASE(nul_byte_line_is_invalid_envelope)
{
    CommandRegistry registry;
    add_test_commands(registry);
    const std::string input =
        std::string("ab\0cd", 5) + "\n"
        + R"({"v":1,"id":"after","command":"test.echo"})" + "\n";

    const session_run run = run_session(registry, admin_caller(), input);

    const std::vector<std::string> lines = lines_of(run.output);
    BOOST_REQUIRE(lines.size() == 2U);
    BOOST_TEST((parse_response(lines[0]).response->error.code
                == CommandErrorCode::invalid_envelope));
    BOOST_TEST(parse_response(lines[1]).response->id == "after");
}

// Type: unit (blank lines produce no output).
// Scenario: a stream of only blank/whitespace lines produces zero responses and
//   ends at eof.
// Boundaries: blank-line skipping is internal to the reader.
// On failure first check: whether a blank verdict ever reaches the session.

BOOST_AUTO_TEST_CASE(blank_lines_produce_no_output)
{
    CommandRegistry registry;
    add_test_commands(registry);

    const session_run run =
        run_session(registry, admin_caller(), "\n  \n\t\n\n");

    BOOST_TEST((run.end == session_end::eof));
    BOOST_TEST(run.responses == 0U);
    BOOST_TEST(run.output.empty());
}

// Type: unit (CRLF input, LF output).
// Scenario: CRLF-terminated requests are accepted and the output uses '\n'
//   only, never '\r'.
// Boundaries: input CR stripping vs output terminator.
// On failure first check: write_response terminator and the reader CR strip.

BOOST_AUTO_TEST_CASE(crlf_input_lf_output)
{
    CommandRegistry registry;
    add_test_commands(registry);
    const std::string input =
        R"({"v":1,"id":"a","command":"test.echo"})" "\r\n"
        R"({"v":1,"id":"b","command":"test.echo"})" "\r\n";

    const session_run run = run_session(registry, admin_caller(), input);

    BOOST_TEST(run.responses == 2U);
    BOOST_TEST(run.output.find('\r') == std::string::npos);
    BOOST_TEST(lines_of(run.output).size() == 2U);
}

// Type: unit (newline in arg value is escaped).
// Scenario: an arg value containing a newline round-trips as an escaped \n and
//   the response line carries no raw newline, so output stays one line.
// Boundaries: the write_response no-raw-newline invariant (point 2).
// On failure first check: serialize_response control escaping of result_json.

BOOST_AUTO_TEST_CASE(arg_newline_never_breaks_framing)
{
    CommandRegistry registry;
    add_test_commands(registry);
    const std::string input =
        R"({"v":1,"id":"a","command":"test.echo","args":{"text":"a\nb"}})"
        "\n";

    const session_run run = run_session(registry, admin_caller(), input);

    BOOST_TEST(run.responses == 1U);
    const std::vector<std::string> lines = lines_of(run.output);
    BOOST_REQUIRE(lines.size() == 1U);
    BOOST_TEST(parse_response(lines[0]).response->ok == true);
}

// Type: unit (handler exception -> internal, loop continues).
// Scenario: a throwing handler yields an internal error response and the next
//   request is still answered.
// Boundaries: exception isolation around execute.
// On failure first check: execute's handler catch and the session try/catch.

BOOST_AUTO_TEST_CASE(handler_exception_is_internal_and_continues)
{
    CommandRegistry registry;
    add_test_commands(registry);
    const std::string input =
        R"({"v":1,"id":"a","command":"test.boom"})" "\n"
        R"({"v":1,"id":"b","command":"test.echo"})" "\n";

    const session_run run = run_session(registry, admin_caller(), input);

    const std::vector<std::string> lines = lines_of(run.output);
    BOOST_REQUIRE(lines.size() == 2U);
    BOOST_TEST((parse_response(lines[0]).response->error.code
                == CommandErrorCode::internal));
    BOOST_TEST(parse_response(lines[1]).response->ok == true);
}

// Type: unit (authorization mapped, no throw into the session).
// Scenario: a User caller invoking a write command gets not_authorized; the
//   AccessDeniedError never reaches the session.
// Boundaries: authorization performed inside execute (contract C7).
// On failure first check: execute's permission stage mapping to not_authorized.

BOOST_AUTO_TEST_CASE(user_on_write_command_is_not_authorized)
{
    CommandRegistry registry;
    add_test_commands(registry);
    CallerContext user;
    user.control_set = ControlSet::User;
    user.transport = "test";
    const std::string input = R"({"v":1,"id":"a","command":"test.write"})" "\n";

    const session_run run = run_session(registry, user, input);

    const std::vector<std::string> lines = lines_of(run.output);
    BOOST_REQUIRE(lines.size() == 1U);
    BOOST_TEST((parse_response(lines[0]).response->error.code
                == CommandErrorCode::not_authorized));
}

// Type: unit (job-kind command answered immediately).
// Scenario: a job-kind command returns an accepted job envelope immediately,
//   never blocking the loop.
// Boundaries: job acceptance is one response like any other.
// On failure first check: the submitter install and execute's job branch.

BOOST_AUTO_TEST_CASE(job_command_returns_job_envelope)
{
    CommandRegistry registry;
    add_test_commands(registry);
    const std::string input = R"({"v":1,"id":"a","command":"test.job"})" "\n";

    const session_run run = run_session(registry, admin_caller(), input);

    const std::vector<std::string> lines = lines_of(run.output);
    BOOST_REQUIRE(lines.size() == 1U);
    const parse_response_result parsed = parse_response(lines[0]);
    BOOST_REQUIRE(parsed.response.has_value());
    BOOST_TEST(parsed.response->ok == true);
    BOOST_REQUIRE(parsed.response->job.has_value());
    BOOST_TEST(parsed.response->job->id == "j-1");
}

// Type: unit (output failure stops the loop).
// Scenario: an output stream that fails after two responses makes serve return
//   output_failed with exactly two responses counted, leaving input unread.
// Boundaries: output_failed terminal, counter not advanced on the failed write.
// On failure first check: write_response stream-state check and the return path.

BOOST_AUTO_TEST_CASE(output_failure_returns_output_failed)
{
    CommandRegistry registry;
    add_test_commands(registry);
    std::string input;
    for (int i = 0; i < 5; ++i) {
        input += R"({"v":1,"id":"x","command":"test.echo"})";
        input += '\n';
    }
    std::istringstream in(input);
    capped_ostreambuf buf(2);
    std::ostream out(&buf);
    NdjsonSession session(in, out, 65536);

    const session_end end = session.serve(registry, admin_caller());

    BOOST_TEST((end == session_end::output_failed));
    BOOST_TEST(session.responses_written() == 2U);
}

// Type: unit (hard input error).
// Scenario: an input stream already in badbit yields input_failed with no
//   output.
// Boundaries: io_error verdict vs clean eof.
// On failure first check: the read_result::io_error branch.

BOOST_AUTO_TEST_CASE(badbit_input_returns_input_failed)
{
    CommandRegistry registry;
    add_test_commands(registry);
    std::istringstream in(R"({"v":1,"id":"a","command":"test.echo"})" "\n");
    in.setstate(std::ios::badbit);
    std::ostringstream out;
    NdjsonSession session(in, out, 65536);

    const session_end end = session.serve(registry, admin_caller());

    BOOST_TEST((end == session_end::input_failed));
    BOOST_TEST(session.responses_written() == 0U);
    BOOST_TEST(out.str().empty());
}

// Type: unit (final line without newline).
// Scenario: a last request lacking a trailing newline is still answered.
// Boundaries: last-line-without-terminator framing.
// On failure first check: reader's no-trailing-newline branch reaching the loop.

BOOST_AUTO_TEST_CASE(last_line_without_newline_is_answered)
{
    CommandRegistry registry;
    add_test_commands(registry);
    const std::string input =
        R"({"v":1,"id":"a","command":"test.echo"})" "\n"
        R"({"v":1,"id":"b","command":"test.echo"})";

    const session_run run = run_session(registry, admin_caller(), input);

    BOOST_TEST((run.end == session_end::eof));
    const std::vector<std::string> lines = lines_of(run.output);
    BOOST_REQUIRE(lines.size() == 2U);
    BOOST_TEST(parse_response(lines[1]).response->id == "b");
}

// Type: unit (volume and ordering).
// Scenario: 10 000 sequential requests complete, one response per line, ids
//   echoed in order, with bounded memory.
// Boundaries: large N preserves the one-line-per-request property.
// On failure first check: any accidental buffering or dropped/duplicated lines.

BOOST_AUTO_TEST_CASE(ten_thousand_requests_preserve_order)
{
    CommandRegistry registry;
    add_test_commands(registry);
    const int count = 10000;
    std::string input;
    for (int i = 0; i < count; ++i) {
        input += R"({"v":1,"id":")";
        input += "c-" + std::to_string(i);
        input += R"(","command":"test.echo"})";
        input += '\n';
    }

    const session_run run = run_session(registry, admin_caller(), input);

    BOOST_TEST((run.end == session_end::eof));
    BOOST_TEST(run.responses == static_cast<std::size_t>(count));
    const std::vector<std::string> lines = lines_of(run.output);
    BOOST_REQUIRE(lines.size() == static_cast<std::size_t>(count));
    BOOST_TEST(parse_response(lines.front()).response->id == "c-0");
    BOOST_TEST(parse_response(lines.back()).response->id == "c-9999");
}
