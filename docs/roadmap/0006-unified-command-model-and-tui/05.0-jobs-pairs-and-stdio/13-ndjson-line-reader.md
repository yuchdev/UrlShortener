# 13 - NDJSON line reader (framing input)

**Parent task:** 05.0 Jobs, pair import/export and stdio transport
**State:** ⬜ Not started
**Depends on:** 05.0/07 (`read_bounded_line`)
**Blocks:** 14
**Estimated size:** S
**Suggested agent:** cpp-expert (tests: testing-expert)

## Objective

Fix the input half of the NDJSON wire framing in code: a reader that turns a byte stream into request lines with a hard per-line size cap, tolerant line endings, a first-line BOM rule, and an explicit verdict for every non-line outcome, so the session loop in 14 never buffers unbounded input and never guesses.

## Dependencies

- 05.0/07 `read_bounded_line(std::istream&, std::string&, max, bytes_read*)` and `line_status`.
- Framing contract (normative for 12-19, copied into `docs/commands/adding-a-transport.md` by 19):

| Rule | Decision |
|---|---|
| Encoding | UTF-8; the reader does not validate encoding (the envelope parser rejects bad JSON) |
| Line terminator | `\n`; a preceding `\r` is stripped; a final line without terminator is a valid line |
| Max line | `ServerConfig::max_request_body_bytes` (default 65536), the same limit as a REST body; longer lines are dropped whole and reported as `too_long` |
| Blank / whitespace-only line | skipped silently, no response |
| UTF-8 BOM | stripped only at the start of the first line |
| NUL byte in a line | `invalid` verdict (never passed to the JSON parser) |
| Pretty-printed (multi-line) JSON | unsupported; each physical line is parsed alone and fails as `invalid_envelope` |

## Files affected

| Path | Action | What changes |
|---|---|---|
| `include/url_shortener/commands/ndjson_line_reader.hpp` | create | `NdjsonLineReader` |
| `src/commands/ndjson_line_reader.cpp` | create | implementation over `read_bounded_line` |
| `tests/unit/commands/61_ndjson_line_reader.cpp` | create | cases below |
| `sources.cmake` | modify | add the `.cpp`; test into `COMMANDS_UNIT_SOURCES` |

The header needs `<istream>`; it is in `commands/` but only for streams, which the 01.0 include rule (no Beast/Boost/storage) allows.

## Classes and functions

```cpp
namespace url_shortener::commands {
enum class read_result { line, too_long, invalid, eof, io_error };

class NdjsonLineReader
{
public:
    NdjsonLineReader(std::istream& in, std::size_t max_line_bytes);
    read_result next(std::string& line);        // fills `line` only for read_result::line
    std::size_t line_number() const noexcept;   // physical lines consumed so far (1-based after first read)
private:
    std::istream& m_in; std::size_t m_max; std::size_t m_line_number = 0; bool m_first = true;
};
}
```

`next` never returns an empty or all-whitespace line: it loops internally over blank lines, so the session sees only content lines or a verdict. `too_long` consumes the rest of the physical line. `invalid` (NUL) consumes the line. `eof` is returned when the stream is exhausted with no pending partial line; `io_error` when the stream is bad but not at EOF.

## Implementation steps

1. Implement `next` as: loop { `read_bounded_line`; map status; on `line`: strip BOM if first physical line, test for NUL, test blank (skip), return }.
2. Increment `m_line_number` for every physical line, including skipped ones, so diagnostics can cite real line numbers.
3. Write tests with `std::istringstream`; a `std::istream` over a custom `streambuf` that throws on read verifies `io_error` handling.
4. A memory-bound test feeds a 10 MiB single line and asserts `too_long` while `line.capacity()` stays small.

## Commands

```text
cmake --build cmake-build --target commands__61_ndjson_line_reader
ctest --test-dir cmake-build -R "^commands__61_ndjson_line_reader$" --output-on-failure
```
Windows/multi-config: `--config Debug` / `-C Debug`.

## Tests

| Test file | Target | Label | Cases |
|---|---|---|---|
| `tests/unit/commands/61_ndjson_line_reader.cpp` | `commands__61_ndjson_line_reader` | unit | LF, CRLF and mixed endings; last line without newline; empty input -> `eof`; only blank lines -> `eof` with `line_number` counting them; line of exactly `max` bytes -> `line`; `max+1` -> `too_long` and the next line is still readable; 10 MiB line -> `too_long`, bounded capacity; BOM on first line stripped, BOM on second line kept; NUL in a line -> `invalid`, next line fine; whitespace-only line skipped; a line made only of `\r` skipped; stream with `badbit` -> `io_error`; `line_number` matches physical positions across skips and errors |

Manual check: pipe a 1 MiB line of `x` into `url_shortener serve-stdio` once 17 exists and confirm one `invalid_envelope` response and the process stays alive.

## Constraints

- C2: framing is part of the wire contract and must be identical for every NDJSON transport (stdio now, IPC later); do not parameterize quirks per transport.
- Memory is bounded by `max_line_bytes` plus one chunk regardless of input.
- No JSON parsing here; the reader is byte/line level only.
- C++17, naming per `.clang-tidy`, `.cpp` in `sources.cmake`.

## Success criteria

1. `commands__61_ndjson_line_reader` passes.
2. The 10 MiB case completes without allocating more than twice `max_line_bytes` for `line`.
3. The framing table above is the single source quoted by 14, 15 and 19 (no contradictory copy elsewhere).

## Out of scope / risks

- Content-Length / length-prefixed framing, binary payloads, partial-line timeouts (a stalled writer simply blocks the read; the parent controls the pipe).
- Risk: Windows console stdin in text mode translates `\r\n` and treats Ctrl-Z as EOF; 17 switches the standard streams to binary mode.
