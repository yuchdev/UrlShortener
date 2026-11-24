#pragma once

#include <cstddef>
#include <iosfwd>
#include <string>

#include <url_shortener/commands/ndjson_line_reader.hpp>

namespace url_shortener::commands
{

class CommandRegistry;
struct CallerContext;
struct CommandResponse;

/**
 * @brief Why a @ref NdjsonSession::serve loop stopped.
 *
 * A session runs until exactly one of these terminal conditions; a malformed
 * request line is never terminal (it produces an error response and the loop
 * continues). The adapter (subtask 15) maps each value to a process exit code.
 */
enum class session_end
{
    ///< The input stream was exhausted cleanly (no pending partial line); the
    ///< normal end of a piped session.
    eof,

    ///< The input stream reported a hard read error (`std::ios::badbit`) while
    ///< not at end of stream; reading stopped.
    input_failed,

    ///< A write to the output stream failed (`flush` or insertion set a failure
    ///< bit); the session stops reading immediately, since no further response
    ///< could be delivered (threat model T-105: a parent that stops reading
    ///< stdout can never make the session buffer without bound).
    output_failed
};

/**
 * @brief The transport-independent request/response loop of `serve-stdio`
 * (threat model T-104/T-105, milestone contracts C1/C2/C6).
 *
 * Reads NDJSON request lines from a plain `std::istream`, executes each through
 * the one @ref CommandRegistry::execute entry point, and writes exactly one
 * response line to a plain `std::ostream`, in order, flushing after each. It
 * knows only @ref CommandRequest / @ref CommandResponse (contract C1); it holds
 * no sockets and no `HttpServer` (constraint C5), so the stdio adapter (15) and
 * any future stream-based IPC adapter reuse it unchanged.
 *
 * Normative behavior (identical to the framing section of the subtask-19
 * guide):
 *
 * 1. One content line in, exactly one response line out, strictly in order. The
 *    next line is not read until the previous response has been flushed. Long
 *    work never blocks the loop because a `kind=job` command returns the `job`
 *    envelope immediately.
 * 2. An output line is compact single-line JSON followed by a single `\n`
 *    (never `\r\n`), flushed. @ref serialize_response escapes control
 *    characters so a response can never carry a raw newline;
 *    @ref write_response asserts this in debug and substitutes an `internal`
 *    error response if it is ever violated.
 * 3. Verdict mapping over @ref read_result: `line` is parsed and executed; a
 *    parse failure becomes an `invalid_envelope` response whose message names
 *    the problem class and never echoes the line; `too_long` becomes
 *    `invalid_envelope` "line too long"; `invalid` (a NUL byte) becomes
 *    `invalid_envelope` "invalid byte in line"; `io_error` ends the loop with
 *    @ref session_end::input_failed; `eof` ends it with @ref session_end::eof.
 * 4. The response `id` is echoed from the request. When the line is a valid
 *    object, the id is recovered best-effort with the sanctioned scanner
 *    (`scan_object` + `find_member("id")`), string kind only and accepted only
 *    when 1..64 bytes and free of control characters; otherwise a session-local
 *    `srv-<n>` is assigned. The same `srv-<n>` fallback names a `too_long` or
 *    `invalid` line (whose bytes were dropped and cannot be scanned). The
 *    counter is per session, so a scripted session's ids are deterministic and
 *    cannot be forced to collide by a client sending `"id":"srv-5"` (threat
 *    model T-104).
 * 5. Any `std::exception` escaping @ref CommandRegistry::execute becomes an
 *    `internal` error response with a fixed message and the loop continues;
 *    `AccessDeniedError` is already mapped to `not_authorized` inside `execute`,
 *    so a `User` caller invoking a write command never throws into the session.
 * 6. A malformed line never ends the session; only EOF or a stream failure
 *    does.
 * 7. Nothing but response envelopes is ever written to the output stream
 *    (contract C2, threat model T-102): no banners, prompts or logs. The one
 *    `debug`-level session start/end line carries only counts and a reason (no
 *    request content, contract C6) and goes to the logger, never to `out`.
 *
 * Single-threaded by design: one session owns one reader and one output stream
 * and drives them from one thread. The ordering guarantee depends on this; do
 * not add concurrency.
 */
class NdjsonSession
{
public:
    /**
     * @brief Binds a session to an input stream, an output stream and a
     * per-line byte cap.
     *
     * @param in The untrusted request source, read one line per iteration;
     * never logged. The reference is captured and must outlive this session.
     * @param out The response sink; only response envelopes are written to it
     * (contract C2). The reference is captured and must outlive this session.
     * @param max_line_bytes The maximum bytes a single request line may occupy
     * before it is rejected as `too_long`; also the byte cap passed to the
     * envelope parser. Callers pass `ServerConfig::max_request_body_bytes`
     * (default 65536).
     */
    NdjsonSession(std::istream& in,
                  std::ostream& out,
                  std::size_t max_line_bytes);

    /**
     * @brief Runs the read/execute/write loop until a terminal condition.
     *
     * Each iteration reads one @ref read_result, maps it per the verdict table
     * above, and (for every verdict but `eof`/`io_error`) writes exactly one
     * response line and flushes before reading again. Returns only on clean EOF,
     * a hard input error, or an output-stream failure.
     *
     * @param registry The command catalog whose @ref CommandRegistry::execute
     * is the sole dispatch and authorization entry point; must outlive the call.
     * @param caller The authenticated caller on whose behalf every request in
     * this session runs. Identity is fixed by composition and never read from
     * the input stream (threat model T-107); must outlive the call.
     * @return @ref session_end::eof on a clean end of input; @ref
     * session_end::input_failed on a hard read error; @ref
     * session_end::output_failed when a response could not be written.
     */
    session_end serve(const CommandRegistry& registry,
                      const CallerContext& caller);

    /**
     * @brief The number of response lines successfully written and flushed so
     * far.
     *
     * @return A count that rises by one for each response delivered to the
     * output stream; a response whose write failed is not counted. `noexcept`.
     */
    std::size_t responses_written() const noexcept;

private:
    /**
     * @brief Serializes one response, appends a single `\n`, and flushes.
     *
     * Enforces the no-raw-newline invariant (point 2): if the serialized text
     * ever contains a `\n` or `\r` it is asserted against in debug builds and
     * replaced with a well-formed `internal` error envelope echoing the same
     * id, so the wire framing can never be corrupted. On success the response
     * counter is advanced.
     *
     * @param response The response to serialize and emit.
     * @return `true` when the line was written and flushed with the stream still
     * good; `false` when the insertion or flush set a stream failure bit, in
     * which case the counter is not advanced and @ref serve stops.
     */
    bool write_response(const CommandResponse& response);

    /**
     * @brief Resolves the response id for one content line (point 4).
     *
     * Recovers a client id from @p line only when the line is a well-formed JSON
     * object carrying a string `id` of 1..64 bytes with no control characters,
     * using the sanctioned scanner; otherwise falls back to
     * @ref next_server_id. Never returns raw, unvalidated client bytes.
     *
     * @param line The content line bytes (terminator already stripped).
     * @return A validated client id, or a fresh session-local `srv-<n>`.
     */
    std::string resolve_echo_id(const std::string& line);

    /**
     * @brief Returns the next session-local correlation id.
     *
     * Backed by a plain per-session counter (not the process-wide atomic), so a
     * scripted session produces a deterministic `srv-1`, `srv-2`, ... sequence
     * and cannot be made to collide with a client-supplied id.
     *
     * @return An id of the form `"srv-<n>"` with `<n>` starting at `1`.
     */
    std::string next_server_id();

    ///< The bounded line reader over the input stream (framing, subtask 13).
    NdjsonLineReader m_reader;

    ///< The response sink; owned by the caller, not this session. Only response
    ///< envelopes are written here (contract C2).
    std::ostream& m_out;

    ///< Byte cap passed to the envelope parser and id-recovery scanner; equals
    ///< the reader's per-line cap, so a line the reader accepted is never
    ///< rejected by the parser as over-size.
    std::size_t m_max_bytes;

    ///< Session-local `srv-<n>` counter; `0` before the first fallback id.
    std::size_t m_server_id_counter = 0;

    ///< Count of response lines successfully written and flushed.
    std::size_t m_responses = 0;
};

}  // namespace url_shortener::commands
