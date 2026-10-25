# 14 - NdjsonSession (request/response loop and error framing)

**Parent task:** 05.0 Jobs, pair import/export and stdio transport
**State:** ⬜ Not started
**Depends on:** 05.0/13, 01.0/01 (`scan_object` for id recovery), 01.0/03 (envelope parse, `invalid_envelope`), 01.0/04 (response serialize), 01.0/07 (`CommandRegistry::execute`, `CallerContext`)
**Blocks:** 15, 19
**Estimated size:** M
**Suggested agent:** cpp-expert (tests: testing-expert)

## Objective

Implement the transport-independent heart of `serve-stdio`: `NdjsonSession`, which reads request lines, calls `CommandRegistry::execute` and writes exactly one response line per content line, in order, flushing after each. It takes plain `std::istream`/`std::ostream`, so the stdio adapter (15) and any future IPC adapter (pipe or socket stream) reuse it unchanged. This subtask fixes the output framing and the error/continue rules.

## Dependencies

- 05.0/13 (`NdjsonLineReader`, framing table).
- 01.0/03 (`parse_request`), 01.0/04 (`serialize_response`, `make_error` response), 01.0/07 (`execute`). Symbols below marked NEW-in-01.0 are partly UNVERIFIED names; the capabilities required are: parse a line into `CommandRequest` with a structured `invalid_envelope` error, serialize a `CommandResponse` to a single-line string, build an error `CommandResponse` from a code and message, and `CommandRegistry::execute(const CallerContext&, const CommandRequest&) const` (verified in ADR 0002 section 2).
- Best-effort `id` recovery from a rejected line uses the sanctioned scanner (`commands::scan_object` + `find_member("id")`, 01.0/01),
  not `core/utils.hpp::extractJsonStringField` (which would match a nested `id`, plan C9).

## Files affected

| Path | Action | What changes |
|---|---|---|
| `include/url_shortener/commands/ndjson_session.hpp` | create | `NdjsonSession`, `session_end` |
| `src/commands/ndjson_session.cpp` | create | loop and framing |
| `tests/unit/commands/62_ndjson_session.cpp` | create | scripted sessions over `std::stringstream` |
| `sources.cmake` | modify | add the `.cpp`; test into `COMMANDS_UNIT_SOURCES` |

## Classes and functions

```cpp
namespace url_shortener::commands {
enum class session_end { eof, input_failed, output_failed };

class NdjsonSession
{
public:
    NdjsonSession(std::istream& in, std::ostream& out, std::size_t max_line_bytes);
    session_end serve(const CommandRegistry& registry, const CallerContext& caller);
    std::size_t responses_written() const noexcept;
private:
    bool write_response(const CommandResponse& response);   // serialize + '\n' + flush; false on stream failure
    std::string next_server_id();                           // "srv-<n>", session-local counter
    NdjsonLineReader m_reader; std::ostream& m_out; std::size_t m_server_id_counter = 0; std::size_t m_responses = 0;
};
}
```

Normative behavior:

1. One content line in, exactly one response line out, strictly in order. The next line is not read until the previous response is flushed. Long work never blocks the loop because `kind=job` commands return the `job` envelope immediately.
2. Output line = compact single-line JSON followed by a single `\n` (never `\r\n`), flushed. The serializer must escape control characters so a response can never contain a raw newline; `write_response` asserts this in debug and replaces the response with an `internal` error if violated.
3. Verdict mapping: `read_result::line` -> parse; parse failure -> `invalid_envelope` response (message names the problem class, never echoes the line); `too_long` -> `invalid_envelope` "line too long"; `invalid` (NUL) -> `invalid_envelope` "invalid byte in line"; `io_error` -> end with `input_failed`; `eof` -> end with `eof`.
4. `id`: echoed from the request; when parsing fails, the id is recovered best-effort with `scan_object(line)` + `find_member("id")` when the line is at least a well-formed object, string kind only (accepted only if <= 64 chars), else `srv-<n>`; when the request is valid but has no id, the session assigns `srv-<n>` before `execute` so the response id is deterministic.
5. Any `std::exception` escaping `execute` becomes an `internal` error response with a fixed message and the loop continues. `AccessDeniedError` from `AccessGuard` must already be mapped to `not_authorized` inside `execute` (01.0/07); the session test asserts no exception reaches the session for a `User` caller invoking a write command.
6. A malformed line never ends the session; only EOF or a stream failure does.
7. No log line contains request content (C6); one `debug`-level line per session start/end with counts.

## Implementation steps

1. Write the header with a doc comment embedding the normative list above.
2. Implement `serve` as a plain loop over `NdjsonLineReader::next` with a `switch` on `read_result`.
3. Implement `write_response`: `out << serialized << '\n'; out.flush(); return static_cast<bool>(out);`.
4. Add the `try/catch` around `execute` and the newline guard.
5. Tests: build a real `CommandRegistry` with `command.list` (01.0) and a test-only descriptor `test.echo` returning its arg; drive with `std::stringstream` input and capture output; split output by `\n` to assert one line per request.
6. Compare against the shared conformance corpus format (07.0) so the stdio adapter shim can reuse the helper.

## Commands

```text
cmake --build cmake-build --target commands__62_ndjson_session
ctest --test-dir cmake-build -R "^commands__62_ndjson_session$" --output-on-failure
```
Windows/multi-config: `--config Debug` / `-C Debug`.

## Tests

| Test file | Target | Label | Cases |
|---|---|---|---|
| `tests/unit/commands/62_ndjson_session.cpp` | `commands__62_ndjson_session` | unit | three valid requests -> three response lines in order, ids echoed; request without id -> `srv-1`, `srv-2` sequence; malformed JSON between two valid requests -> middle line `invalid_envelope`, loop continues, third answered; valid JSON with `"id":"c-7"` but no `command` -> `invalid_envelope` echoing `c-7`; bad `v` (`2`, `"x"`, missing) -> `invalid_envelope`; unknown command -> `unknown_command`; unknown arg -> `invalid_field`; line of `max+1` bytes -> `invalid_envelope` and next line served; NUL byte line -> `invalid_envelope`; blank lines produce no output; CRLF input accepted, output uses `\n` only; response contains no raw newline even when an arg value contains `\n` (escaped); exception thrown by a test handler -> `internal`, loop continues; `User` caller on write command -> `not_authorized`, no throw; output stream failing after N responses -> `serve` returns `output_failed` and stops reading; `badbit` input -> `input_failed`; EOF without trailing newline on last request still answered; 10 000 sequential requests complete (ordering preserved, bounded memory); job-kind test command returns `job` envelope immediately while the body is gated |

Manual check: none here; process-level checks are in 18.

## Constraints

- C2: output is the response envelope only; nothing else (no banners, no prompts, no logs) is ever written to `out`.
- C1: no per-command code; the session only knows `CommandRequest`/`CommandResponse`.
- C6: error messages never include request bytes; the audit hook stays in the registry.
- C5: no sockets, no `HttpServer`, no `uri.txt` access (stdio is stream based).
- Single-threaded by design; do not add concurrency (ordering guarantee).
- C++17, naming per `.clang-tidy`, `.cpp` in `sources.cmake`; header free of Beast.

## Success criteria

1. `commands__62_ndjson_session` passes.
2. For every input line count N with content, the output has exactly N lines (property test over random mixes of valid/invalid/blank lines, seeded and logged on failure).
3. The framing section of this spec is identical to the one produced by 19 (checked in review via `/doc-xref`).

## Out of scope / risks

- Pipelining/concurrent execution with out-of-order responses (would need `id` matching rules; deliberately not offered).
- Back-pressure: a client that never reads stdout blocks `flush`; the parent owns the pipe, documented in 19.
- Risk: 01.0 may place `id` generation (`srv-<n>`) in `execute`; keep a single source: if so, delete `next_server_id()` for valid requests and keep it only for parse failures.
