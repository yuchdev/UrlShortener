#include <url_shortener/commands/envelope.hpp>

#include <atomic>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <url_shortener/commands/error_codes.hpp>
#include <url_shortener/commands/json_scan.hpp>
#include <url_shortener/core/utils.hpp>

namespace url_shortener::commands
{

namespace
{

/**
 * @brief Tests whether @p ch is a JSON control character forbidden in an id.
 * @param ch The byte to test.
 * @return `true` for any byte below `0x20` (including NUL) or `0x7f`.
 */
bool is_control_char(unsigned char ch)
{
    return ch < 0x20 || ch == 0x7f;
}

/**
 * @brief Tests whether @p value is an acceptable client correlation id.
 * @param value Decoded candidate id.
 * @param max_len Maximum accepted length in bytes.
 * @return `true` when @p value is 1..@p max_len bytes and contains no control
 * character; `false` otherwise.
 */
bool is_valid_id(std::string_view value, std::size_t max_len)
{
    if (value.empty() || value.size() > max_len)
    {
        return false;
    }
    for (const char raw : value)
    {
        if (is_control_char(static_cast<unsigned char>(raw)))
        {
            return false;
        }
    }
    return true;
}

/**
 * @brief Maps a `json_scan` error code to the envelope's fixed message.
 * @param scan_error One of the scanner's `error` vocabulary strings.
 * @return `"too_large"`, `"too_deep"` or `"duplicate_key"` when the scanner
 * reported one of those; `"malformed"` for every other scanner failure.
 */
std::string scan_error_message(const std::string& scan_error)
{
    if (scan_error == "too_large" || scan_error == "too_deep" ||
        scan_error == "duplicate_key")
    {
        return scan_error;
    }
    return "malformed";
}

/**
 * @brief Builds a failure result, routing the message through the sanitizer.
 * @param echo_id The id to echo in the response (already validated or a
 * server-generated fallback).
 * @param message Fixed, field-naming message; never input text.
 * @return A @ref parse_request_result with no request and an `invalid_envelope`
 * error.
 */
parse_request_result fail(std::string echo_id, std::string_view message)
{
    parse_request_result result;
    result.id = std::move(echo_id);
    result.error = make_error(CommandErrorCode::invalid_envelope,
                              sanitize_error_message(message));
    return result;
}

} // namespace

bool is_valid_command_name(std::string_view name)
{
    if (name.empty())
    {
        return false;
    }
    std::size_t i = 0;
    int segments = 0;
    while (true)
    {
        // Each segment must begin with a lower-case letter.
        if (i >= name.size() || !(name[i] >= 'a' && name[i] <= 'z'))
        {
            return false;
        }
        ++i;
        while (i < name.size())
        {
            const char ch = name[i];
            const bool is_body = (ch >= 'a' && ch <= 'z') ||
                                 (ch >= '0' && ch <= '9') || ch == '_';
            if (!is_body)
            {
                break;
            }
            ++i;
        }
        ++segments;
        if (i == name.size())
        {
            break;
        }
        if (name[i] != '.')
        {
            return false;
        }
        ++i; // consume the dot; the loop then requires a non-empty segment
    }
    return segments >= 2;
}

std::string next_server_id()
{
    static std::atomic<std::uint64_t> counter{0};
    const std::uint64_t n = counter.fetch_add(1, std::memory_order_relaxed) + 1;
    return "srv-" + std::to_string(n);
}

parse_request_result parse_request(std::string_view text,
                                   const envelope_limits& limits)
{
    scan_limits sl;
    sl.max_bytes = limits.max_bytes;
    sl.max_depth = limits.max_depth;

    const scan_result scan = scan_object(text, sl);
    if (!scan.ok)
    {
        // The envelope could not be tokenized at all, so no client id can be
        // trusted; fall back to a server-generated id.
        return fail(next_server_id(), scan_error_message(scan.error));
    }

    // Resolve the echoed id first (threat model T-05): adopt the client id only
    // when it passes the id rules, otherwise use a server-generated fallback,
    // so no response or audit line ever echoes an unvalidated id even when
    // another field is the one that fails below.
    std::optional<std::string> client_id;
    const json_member* id_member = find_member(scan.members, "id");
    bool id_present_but_invalid = false;
    if (id_member != nullptr)
    {
        if (id_member->kind == json_kind::string)
        {
            std::optional<std::string> decoded =
                decode_json_string(id_member->raw);
            if (decoded && is_valid_id(*decoded, limits.max_id_len))
            {
                client_id = std::move(decoded);
            }
            else
            {
                id_present_but_invalid = true;
            }
        }
        else
        {
            id_present_but_invalid = true;
        }
    }
    const std::string echo_id =
        client_id ? *client_id : next_server_id();

    // Closed envelope: every top-level key must be one of the five known
    // fields. A duplicate key was already rejected by scan_object.
    for (const json_member& member : scan.members)
    {
        if (member.key != "v" && member.key != "id" &&
            member.key != "command" && member.key != "args" &&
            member.key != "options")
        {
            return fail(echo_id, "unknown field");
        }
    }

    // v: present, an integer token equal to 1.
    const json_member* v_member = find_member(scan.members, "v");
    if (v_member == nullptr)
    {
        return fail(echo_id, "missing field v");
    }
    if (v_member->kind != json_kind::number)
    {
        return fail(echo_id, "invalid field v");
    }
    if (v_member->raw != "1")
    {
        // Any other number (0, 2, 1.0, ...) is an unsupported version; still
        // invalid_envelope per the error catalog.
        return fail(echo_id, "unsupported version");
    }

    // id: when present it must be a valid string (already resolved above).
    if (id_present_but_invalid)
    {
        return fail(echo_id, "invalid field id");
    }

    // command: present, a string, valid dotted name within max_command_len.
    const json_member* command_member = find_member(scan.members, "command");
    if (command_member == nullptr)
    {
        return fail(echo_id, "missing field command");
    }
    if (command_member->kind != json_kind::string)
    {
        return fail(echo_id, "invalid field command");
    }
    const std::optional<std::string> command =
        decode_json_string(command_member->raw);
    if (!command || command->size() > limits.max_command_len ||
        !is_valid_command_name(*command))
    {
        return fail(echo_id, "invalid field command");
    }

    // args: optional; when present must be an object, stored verbatim.
    std::string args_json = "{}";
    const json_member* args_member = find_member(scan.members, "args");
    if (args_member != nullptr)
    {
        if (args_member->kind != json_kind::object)
        {
            return fail(echo_id, "invalid field args");
        }
        args_json = args_member->raw;
    }

    // options: optional closed object of boolean dry_run/async.
    CommandOptions options;
    const json_member* options_member = find_member(scan.members, "options");
    if (options_member != nullptr)
    {
        if (options_member->kind != json_kind::object)
        {
            return fail(echo_id, "invalid field options");
        }
        const scan_result opt_scan = scan_object(options_member->raw, sl);
        if (!opt_scan.ok)
        {
            return fail(echo_id, scan_error_message(opt_scan.error));
        }
        for (const json_member& opt : opt_scan.members)
        {
            if (opt.key == "dry_run" || opt.key == "async")
            {
                if (opt.kind != json_kind::boolean)
                {
                    return fail(echo_id, "invalid field options");
                }
                const bool flag = (opt.raw == "true");
                if (opt.key == "dry_run")
                {
                    options.dry_run = flag;
                }
                else
                {
                    options.async = flag;
                }
            }
            else
            {
                return fail(echo_id, "unknown field");
            }
        }
    }

    parse_request_result result;
    result.id = echo_id;
    CommandRequest request;
    request.v = 1;
    request.id = echo_id;
    request.command = *command;
    request.args_json = std::move(args_json);
    request.options = options;
    result.request = std::move(request);
    return result;
}

namespace
{

/**
 * @brief The static job-state table in enumerator order.
 * @return Reference to `{state, wire-name}` rows, one per @ref JobState.
 */
const std::vector<std::pair<JobState, std::string_view>>& job_state_table()
{
    static const std::vector<std::pair<JobState, std::string_view>> table{
        {JobState::queued, "queued"},
        {JobState::running, "running"},
        {JobState::succeeded, "succeeded"},
        {JobState::failed, "failed"},
        {JobState::cancelled, "cancelled"}};
    return table;
}

/**
 * @brief Tests the cheap first-character sanity of a result payload.
 * @param result_json Candidate verbatim result text.
 * @return `true` when non-empty and the first byte can begin a JSON value
 * (`{`, `[`, `"`, a digit, `t`, `f` or `n`); `false` otherwise.
 */
bool result_json_looks_like_value(const std::string& result_json)
{
    if (result_json.empty())
    {
        return false;
    }
    const char c = result_json.front();
    return c == '{' || c == '[' || c == '"' || (c >= '0' && c <= '9') ||
           c == 't' || c == 'f' || c == 'n';
}

/**
 * @brief Builds the fallback `internal` failure envelope for invariant breaks.
 * @param id Correlation id to echo; escaped with `jsonString`.
 * @return A well-formed v1 envelope with `ok:false` and the `internal` code and
 * a fixed, value-free message.
 */
std::string internal_envelope(const std::string& id)
{
    const error_info& info = error_info_for(CommandErrorCode::internal);
    std::string out = R"({"v":1,"id":)";
    out += jsonString(id);
    out += R"(,"ok":false,"error":{"code":")";
    out += std::string(info.name);
    out += R"(","message":)";
    out += jsonString(sanitize_error_message("internal error"));
    out += "}}";
    return out;
}

} // namespace

std::string_view job_state_name(JobState state)
{
    for (const auto& row : job_state_table())
    {
        if (row.first == state)
        {
            return row.second;
        }
    }
    // Only reachable for a cast-in out-of-range value; default to the sole
    // state an envelope reports today.
    return "queued";
}

std::optional<JobState> job_state_from_name(std::string_view name)
{
    for (const auto& row : job_state_table())
    {
        if (row.second == name)
        {
            return row.first;
        }
    }
    return std::nullopt;
}

CommandResponse make_success(std::string id, std::string result_json)
{
    CommandResponse response;
    response.id = std::move(id);
    response.ok = true;
    response.result_json = std::move(result_json);
    return response;
}

CommandResponse make_job_accepted(std::string id, JobInfo job)
{
    CommandResponse response;
    response.id = std::move(id);
    response.ok = true;
    response.job = std::move(job);
    return response;
}

CommandResponse make_failure(std::string id, CommandError error)
{
    CommandResponse response;
    response.id = std::move(id);
    response.ok = false;
    response.error = std::move(error);
    return response;
}

std::string serialize_response(const CommandResponse& response)
{
    const bool has_result = !response.result_json.empty();
    const bool has_job = response.job.has_value();
    const bool has_error = !response.error.ok();
    const int payloads = (has_result ? 1 : 0) + (has_job ? 1 : 0) +
                         (has_error ? 1 : 0);

    // Invariant: exactly one payload, and `ok` matches the payload kind
    // (result/job imply ok, error implies !ok).
    if (payloads != 1 || response.ok != (has_result || has_job))
    {
        return internal_envelope(response.id);
    }
    if (has_result && !result_json_looks_like_value(response.result_json))
    {
        return internal_envelope(response.id);
    }

    std::string out = R"({"v":1,"id":)";
    out += jsonString(response.id);
    out += R"(,"ok":)";
    out += response.ok ? "true" : "false";
    if (has_result)
    {
        out += R"(,"result":)";
        out += response.result_json; // embedded verbatim, never re-escaped
    }
    else if (has_job)
    {
        out += R"(,"job":{"id":)";
        out += jsonString(response.job->id);
        out += R"(,"state":")";
        out += std::string(job_state_name(response.job->state));
        out += R"("})";
    }
    else
    {
        out += R"(,"error":{"code":")";
        out += std::string(error_info_for(response.error.code).name);
        out += R"(","message":)";
        out += jsonString(sanitize_error_message(response.error.message));
        out += "}";
    }
    out += "}";
    return out;
}

namespace
{

/**
 * @brief Builds a rejected parse result carrying a fixed diagnostic.
 * @param message Fixed, value-free diagnostic naming the first problem.
 * @return A @ref parse_response_result with no response and `error` set.
 */
parse_response_result response_reject(std::string message)
{
    parse_response_result result;
    result.error = std::move(message);
    return result;
}

} // namespace

parse_response_result parse_response(std::string_view text)
{
    const scan_result scan = scan_object(text);
    if (!scan.ok)
    {
        return response_reject(scan.error);
    }

    // v: present, integer token equal to 1.
    const json_member* v_member = find_member(scan.members, "v");
    if (v_member == nullptr || v_member->kind != json_kind::number ||
        v_member->raw != "1")
    {
        return response_reject("invalid field v");
    }

    // id: present, a string.
    const json_member* id_member = find_member(scan.members, "id");
    if (id_member == nullptr || id_member->kind != json_kind::string)
    {
        return response_reject("invalid field id");
    }
    std::optional<std::string> id = decode_json_string(id_member->raw);
    if (!id)
    {
        return response_reject("invalid field id");
    }

    // ok: present, a boolean.
    const json_member* ok_member = find_member(scan.members, "ok");
    if (ok_member == nullptr || ok_member->kind != json_kind::boolean)
    {
        return response_reject("invalid field ok");
    }
    const bool ok = (ok_member->raw == "true");

    // Exactly one payload field.
    const json_member* result_member = find_member(scan.members, "result");
    const json_member* job_member = find_member(scan.members, "job");
    const json_member* error_member = find_member(scan.members, "error");
    const int payloads = (result_member != nullptr ? 1 : 0) +
                         (job_member != nullptr ? 1 : 0) +
                         (error_member != nullptr ? 1 : 0);
    if (payloads != 1)
    {
        return response_reject("expected exactly one of result/job/error");
    }

    CommandResponse response;
    response.v = 1;
    response.id = std::move(*id);
    response.ok = ok;

    if (result_member != nullptr)
    {
        // Captured verbatim so an embedded quote/brace/unicode round-trips.
        response.result_json = result_member->raw;
    }
    else if (job_member != nullptr)
    {
        if (job_member->kind != json_kind::object)
        {
            return response_reject("invalid field job");
        }
        const scan_result job_scan = scan_object(job_member->raw);
        if (!job_scan.ok)
        {
            return response_reject(job_scan.error);
        }
        const json_member* job_id = find_member(job_scan.members, "id");
        const json_member* job_state = find_member(job_scan.members, "state");
        if (job_id == nullptr || job_id->kind != json_kind::string ||
            job_state == nullptr || job_state->kind != json_kind::string)
        {
            return response_reject("invalid field job");
        }
        std::optional<std::string> decoded_id =
            decode_json_string(job_id->raw);
        std::optional<std::string> decoded_state =
            decode_json_string(job_state->raw);
        if (!decoded_id || !decoded_state)
        {
            return response_reject("invalid field job");
        }
        const std::optional<JobState> state =
            job_state_from_name(*decoded_state);
        if (!state)
        {
            return response_reject("unknown job state");
        }
        JobInfo job;
        job.id = std::move(*decoded_id);
        job.state = *state;
        response.job = std::move(job);
    }
    else
    {
        if (error_member->kind != json_kind::object)
        {
            return response_reject("invalid field error");
        }
        const scan_result error_scan = scan_object(error_member->raw);
        if (!error_scan.ok)
        {
            return response_reject(error_scan.error);
        }
        const json_member* code = find_member(error_scan.members, "code");
        const json_member* message =
            find_member(error_scan.members, "message");
        if (code == nullptr || code->kind != json_kind::string ||
            message == nullptr || message->kind != json_kind::string)
        {
            return response_reject("invalid field error");
        }
        std::optional<std::string> decoded_code =
            decode_json_string(code->raw);
        std::optional<std::string> decoded_message =
            decode_json_string(message->raw);
        if (!decoded_code || !decoded_message)
        {
            return response_reject("invalid field error");
        }
        const std::optional<CommandErrorCode> code_value =
            error_code_from_name(*decoded_code);
        if (!code_value)
        {
            return response_reject("unknown error code");
        }
        response.error = make_error(*code_value, std::move(*decoded_message));
    }

    parse_response_result result;
    result.response = std::move(response);
    return result;
}

unsigned http_status_for(const CommandResponse& response)
{
    if (response.ok)
    {
        return 200;
    }
    return error_info_for(response.error.code).http_status;
}

int exit_code_for(const CommandResponse& response)
{
    if (response.ok)
    {
        return 0;
    }
    return error_info_for(response.error.code).exit_code;
}

} // namespace url_shortener::commands
