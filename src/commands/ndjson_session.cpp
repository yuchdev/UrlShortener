#include <cassert>
#include <exception>
#include <optional>
#include <string>
#include <utility>

#include <url_shortener/commands/command_descriptor.hpp>
#include <url_shortener/commands/command_registry.hpp>
#include <url_shortener/commands/envelope.hpp>
#include <url_shortener/commands/error_codes.hpp>
#include <url_shortener/commands/json_scan.hpp>
#include <url_shortener/commands/ndjson_session.hpp>
#include <url_shortener/observability/logger_factory.hpp>

namespace url_shortener::commands
{

namespace
{

///< Maximum accepted length, in bytes, of a recovered client id (mirrors the
///< envelope's `max_id_len`, point 4).
constexpr std::size_t k_max_id_len = 64;

/**
 * @brief Maps a terminal reason to its stable lower-case log label.
 * @param reason The terminal condition the loop reached.
 * @return `"eof"`, `"input_failed"` or `"output_failed"`.
 */
const char* reason_label(session_end reason)
{
    switch (reason) {
    case session_end::eof:
        return "eof";
    case session_end::input_failed:
        return "input_failed";
    case session_end::output_failed:
        return "output_failed";
    }
    return "eof";
}

}  // namespace

NdjsonSession::NdjsonSession(std::istream& in,
                             std::ostream& out,
                             std::size_t max_line_bytes)
    : m_reader(in, max_line_bytes)
    , m_out(out)
    , m_max_bytes(max_line_bytes)
{
}

std::size_t NdjsonSession::responses_written() const noexcept
{
    return m_responses;
}

std::string NdjsonSession::next_server_id()
{
    ++m_server_id_counter;
    return "srv-" + std::to_string(m_server_id_counter);
}

std::string NdjsonSession::resolve_echo_id(const std::string& line)
{
    // Best-effort id recovery via the sanctioned scanner only (never
    // core/utils.hpp, which would match a nested `id`; plan C9). A recovered id
    // is accepted only when it is the exact shape the envelope parser would
    // accept as a client id: a string of 1..64 bytes left unchanged by the
    // message sanitizer (valid UTF-8, no control characters), so the echoed id
    // is always safe to serialize (threat model T-03).
    scan_limits limits;
    limits.max_bytes = m_max_bytes;
    const scan_result scan = scan_object(line, limits);
    if (scan.ok) {
        const json_member* id_member = find_member(scan.members, "id");
        if (id_member != nullptr && id_member->kind == json_kind::string) {
            std::optional<std::string> decoded =
                decode_json_string(id_member->raw);
            if (decoded && !decoded->empty() && decoded->size() <= k_max_id_len
                && sanitize_error_message(*decoded) == *decoded)
            {
                return std::move(*decoded);
            }
        }
    }
    return next_server_id();
}

bool NdjsonSession::write_response(const CommandResponse& response)
{
    std::string serialized = serialize_response(response);

    // Point 2 invariant: a response can never carry a raw newline, which would
    // split one envelope into two lines and desynchronize the wire. The
    // serializer escapes control characters, so this only ever fires on a
    // serializer bug; assert in debug and fail safe to a well-formed `internal`
    // envelope rather than corrupt the stream.
    if (serialized.find('\n') != std::string::npos
        || serialized.find('\r') != std::string::npos)
    {
        assert(false && "serialized response contains a raw newline");
        const CommandResponse safe = make_failure(
            response.id,
            make_error(CommandErrorCode::internal, "internal error"));
        serialized = serialize_response(safe);
    }

    m_out << serialized << '\n';
    m_out.flush();
    if (!m_out) {
        return false;
    }
    ++m_responses;
    return true;
}

session_end NdjsonSession::serve(const CommandRegistry& registry,
                                 const CallerContext& caller)
{
    const auto logger =
        observability::LoggerFactory::Component("ndjson_session");
    logger.debug("ndjson_session_start");

    const auto finish = [&](session_end reason) {
        logger.debug("ndjson_session_end",
                     {{"responses", std::to_string(m_responses)},
                      {"reason", reason_label(reason)}});
        return reason;
    };

    std::string line;
    for (;;) {
        const read_result verdict = m_reader.next(line);
        switch (verdict) {
        case read_result::line: {
            const std::string echo_id = resolve_echo_id(line);
            const parse_request_result parsed = parse_request(
                line, envelope_limits {m_max_bytes, k_max_id_len, 64, 8});
            CommandResponse response;
            if (!parsed.ok()) {
                // The message names only the offending field class; the line is
                // never echoed (contract C6).
                response = make_failure(echo_id, parsed.error);
            }
            else {
                // Override the parser's id with the session-local id so the
                // echoed id is deterministic and collision-free (point 4), then
                // dispatch through the single entry point.
                CommandRequest request = std::move(*parsed.request);
                request.id = echo_id;
                try {
                    response = registry.execute(caller, request);
                }
                catch (const std::exception&) {
                    response = make_failure(
                        echo_id,
                        make_error(CommandErrorCode::internal,
                                   "internal error"));
                }
                catch (...) {
                    response = make_failure(
                        echo_id,
                        make_error(CommandErrorCode::internal,
                                   "internal error"));
                }
            }
            if (!write_response(response)) {
                return finish(session_end::output_failed);
            }
            break;
        }
        case read_result::too_long: {
            // The over-long line was dropped whole, so there is nothing to scan
            // for an id (point 4); use a server-generated id.
            const CommandResponse response = make_failure(
                next_server_id(),
                make_error(CommandErrorCode::invalid_envelope,
                           "line too long"));
            if (!write_response(response)) {
                return finish(session_end::output_failed);
            }
            break;
        }
        case read_result::invalid: {
            const CommandResponse response = make_failure(
                next_server_id(),
                make_error(CommandErrorCode::invalid_envelope,
                           "invalid byte in line"));
            if (!write_response(response)) {
                return finish(session_end::output_failed);
            }
            break;
        }
        case read_result::io_error:
            return finish(session_end::input_failed);
        case read_result::eof:
            return finish(session_end::eof);
        }
    }
}

}  // namespace url_shortener::commands
