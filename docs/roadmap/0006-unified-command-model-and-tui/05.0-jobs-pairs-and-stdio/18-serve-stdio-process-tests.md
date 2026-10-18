# 18 - serve-stdio process-level integration and e2e tests

**Parent task:** 05.0 Jobs, pair import/export and stdio transport
**State:** ⬜ Not started
**Depends on:** 05.0/17
**Blocks:** 19, 07.0/07 (the serve-stdio conformance shim reuses `StdioSession`)
**Estimated size:** M
**Suggested agent:** testing-expert

## Objective

Prove, against the real `url_shortener` binary, what unit tests cannot: pipe framing, process exit codes, EOF shutdown with a running job, stdout cleanliness, absence of a listener and platform behavior on Windows and POSIX. The cross-transport equality check ("the same corpus over stdio equals REST") belongs to the conformance suite (07.0/07 shim, corpus schema 07.0/01); this subtask supplies the stdio side as a reusable Python helper and verifies stdio on its own terms.

## Dependencies

- 05.0/17 (the mode runs). 
- Verified harness: `tests/integration/cli/cli_integration_common.py` (`find_binary`, `run_cli`, `parse_json_stdout`, `CliIntegrationBase`), Python tests registered through `CLI_INTEGRATION_SOURCES` in `sources.cmake` as CTest `cli_<stem>`, label `integration`, env `SIMPLE_HTTP_BIN`, timeout 60 s; e2e CLI sections registered through `E2E_CLI_SECTIONS` as `e2e_<section>`, label `e2e`, env `URLSHORTENER_BIN`, POSIX only (skipped on Windows hosts by `CMakeLists.txt`).
- `Python3` interpreter (already required by CMake).

## Files affected

| Path | Action | What changes |
|---|---|---|
| `tests/integration/cli/stdio_session.py` | create | helper class `StdioSession` (spawn, send line, read line with timeout, close stdin, wait) |
| `tests/integration/cli/40_serve_stdio_session.py` | create | framing, ordering, malformed line, ids, read-only |
| `tests/integration/cli/41_serve_stdio_jobs_and_eof.py` | create | import job lifecycle, EOF drain, exit codes, no listener |
| `tests/e2e/scripts/sections/30_serve_stdio.sh` | create | POSIX shell pipe scenario (`printf ... \| binary serve-stdio`) |
| `sources.cmake` | modify | append both `.py` files to `CLI_INTEGRATION_SOURCES` and `30_serve_stdio` to `E2E_CLI_SECTIONS` |

Numbering (plan.md "Test file prefixes"): Task 05.0 owns `tests/integration/cli/40-49`; e2e section 30 leaves a gap after the existing sections (which end at 18). `stdio_session.py` is a helper and is not registered as a test.

## Classes and functions

```python
class StdioSession:
    def __init__(self, binary: str, *args: str, cwd: str | None = None): ...   # Popen, binary pipes, stderr captured
    def send(self, obj_or_line): ...             # json.dumps + "\n" (bytes), flush
    def recv(self, timeout: float = 5.0) -> dict: ...   # one line, json.loads; raises on timeout/EOF
    def command(self, name: str, args: dict | None = None, **options) -> dict: ...  # send+recv convenience
    def close_stdin(self): ...
    def wait(self, timeout: float = 15.0) -> int: ...
    def stderr_text(self) -> str: ...
```

`recv` uses a reader thread with a queue so a hung binary fails the test by timeout instead of blocking CTest. Streams are opened in binary mode (no newline translation on Windows).

## Implementation steps

1. Write `stdio_session.py`; unit-sanity it by running a trivial `command.list` round trip.
2. `40_serve_stdio_session.py` cases (details under Tests).
3. `41_serve_stdio_jobs_and_eof.py` cases.
4. `30_serve_stdio.sh`: model on `13_cli_no_server_socket.sh` (reuse its `_port_open` helper, `URLSHORTENER_BIN` resolution and `mktemp -d` isolation); assert pipe output line count equals input line count and exit code 0; assert port 8000 stays closed.
5. Register in `sources.cmake`; run the whole `integration` and `e2e` labels once.
6. Windows: run the Python tests under the same interpreter CMake selected; the e2e shell section is POSIX only and is skipped by CMake on Windows hosts.

## Commands

```text
cmake --build cmake-build --target url_shortener
ctest --test-dir cmake-build -R "^cli_4[01]_" --output-on-failure
ctest --test-dir cmake-build -R "^e2e_30_serve_stdio$" --output-on-failure
```
Windows/multi-config: add `-C Debug` to `ctest`; `SIMPLE_HTTP_BIN` is set by CMake to the generator-specific path (`$<TARGET_FILE:url_shortener>`). Direct run: `python tests\integration\cli\40_serve_stdio_session.py` with `SIMPLE_HTTP_BIN` set.

## Tests

| Test file | CTest name | Label | Cases |
|---|---|---|---|
| `tests/integration/cli/40_serve_stdio_session.py` | `cli_40_serve_stdio_session` | integration | `command.list` lists `link.create`, `job.get`, `link.import`; create then get returns equal `result`; 50 pipelined requests written before reading any response return in order with matching ids; request without id gets `srv-N`; malformed line then valid line: `invalid_envelope` then success on the same process; 70 KiB line -> `invalid_envelope`, process alive; blank lines produce no response; CRLF-terminated input works; non-ASCII slug/URL data survives UTF-8 round trip; every stdout line is valid JSON with `v==1` (stdout cleanliness); stderr never contains a request URL or slug (C6); `--read-only`: `link.create` -> `not_authorized`, `link.get` works, `link.export` with `path` -> `not_authorized`; `--no-file-paths`: `link.import` with `path` -> `unsupported`; `--path-root <tmp>`: a `path` with `..` escaping it -> `invalid_field` |
| `tests/integration/cli/41_serve_stdio_jobs_and_eof.py` | `cli_41_serve_stdio_jobs_and_eof` | integration | `link.import` inline CSV of 500 rows returns a `job` envelope; polling `job.get` reaches `succeeded` with matching counts; mixed good/bad CSV reports per-line errors; `job.cancel` on a long import (5 000 rows, cancel on first poll) ends `cancelled` with `created + skipped + failed <= total` and partial result present (skip if the machine completes it first: assert `succeeded` is also accepted but log); `link.export` inline then re-import into a second session equals; EOF handling: send import, close stdin immediately, process exits 0 only after the job completes (observed via a following `link.get` in a SECOND process when 02.0 persistence is active; otherwise assert exit code and duration only); broken stdout: reader closes its end after one response while the child still has output -> child exits 4, not hung and not killed by SIGPIPE (POSIX); no listener: port 8000 closed during and after the session; `path` import of a temp file works over stdio |
| `tests/e2e/scripts/sections/30_serve_stdio.sh` | `e2e_30_serve_stdio` | e2e | `printf` of 3 requests (one malformed) piped to the binary: exactly 3 output lines, exit 0; port 8000 closed afterwards; empty stdin exits 0 with no output |

Manual checks: (a) on Windows PowerShell 5.1 and 7 run the smoke command from 17 and confirm no `\r\r\n` artifacts; (b) interactive: run `url_shortener serve-stdio`, type a request line by hand, press Ctrl-D (POSIX) / Ctrl-Z Enter (Windows console) and confirm clean exit.

## Constraints

- Tests must not depend on fixed ports other than the "closed" assertion (skip when 8000 is already in use, like `13_cli_no_server_socket.sh`).
- Timeouts everywhere (`recv` 5 s, `wait` 15 s) so a bug fails fast; CTest timeout 60 s from CMake.
- C5/C6 assertions (no listener, no request data in stderr) are part of the pass criteria.
- Persistence assertions are guarded by a capability probe (`system.info.persistent`, catalog 3.5) so the suite passes both before and after Task 02.0.
- Python style: follow siblings (`unittest`, `CliIntegrationBase`); no new third-party packages.

## Success criteria

1. `ctest --test-dir cmake-build -R "^cli_4[01]_" --output-on-failure` passes on Windows and Linux.
2. `ctest --test-dir cmake-build -R "^e2e_30_serve_stdio$"` passes on Linux/macOS.
3. A deliberate bug (writing a log line to stdout) makes `cli_40_serve_stdio_session` fail (verified once by hand, not committed).
4. `StdioSession` is importable by the 07.0/07 conformance shim without modification.

## Out of scope / risks

- Cross-transport equality (07.0/07, 07.0/15); fuzzing the framing; performance measurements (`perf` label).
- Flake risk: job timing; polling uses bounded loops with `job.get`, never fixed sleeps for ordering.
- Windows pipes and Python text mode: all I/O is bytes; decode explicitly as UTF-8.
