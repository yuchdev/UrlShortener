#include <url_shortener/commands/envelope.hpp>

#include <atomic>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <url_shortener/commands/error_codes.hpp>
#include <url_shortener/commands/json_scan.hpp>

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

} // namespace url_shortener::commands
