# 07 - serve-stdio shim

**Parent task:** 07.0 Tests and conformance
**State:** ⬜ Not started
**Depends on:** 05; Task 05.0 (`serve-stdio`)
**Blocks:** 15, 33
**Estimated size:** S
**Suggested agent:** testing-expert

## Objective

Run each case in one long-lived `url_shortener serve-stdio` process, writing one
request envelope per stdin line and reading one response envelope per stdout
line, plus transport-specific framing tests (ordering, malformed line, EOF).

## Dependencies

- Task 05.0's process-level helper `tests/integration/cli/stdio_session.py` (`StdioSession`, written for the stdio process tests and intended to be importable by this shim without modification): reuse it for spawn, line writing, timed line reading and shutdown instead of the reader-thread code sketched below; if its API differs, adapt in the shim only.
- Subtask 05; 05.0 `serve-stdio` NDJSON contract (catalog section 2: one request per line, one response per line, in order; malformed line yields `invalid_envelope` and the loop continues; clean shutdown on EOF).

## Files affected

| Path | create/modify | What changes |
|---|---|---|
| `tests/conformance/py/11_stdio_corpus.py` | create | `StdioAdapter` + corpus run |
| `tests/conformance/py/12_stdio_framing.py` | create | Framing tests not expressible as corpus cases |
| `sources.cmake` | modify | Append both to `CONFORMANCE_PY_SOURCES` |

## Classes and functions

```python
class StdioAdapter(Adapter):
    transport = "stdio"
    def begin_case(self, case, ctx):                    # spawn process: [bin, *ctx.storage_args, ("--read-only"), "serve-stdio"]
        if case.get("backend") == "inmemory": pass      # allowed: single process holds the store
        self._proc = subprocess.Popen(..., stdin=PIPE, stdout=PIPE, stderr=PIPE, text=True, bufsize=1, cwd=ctx.tmp)
    def send(self, step, request_text, ctx) -> dict:
        assert "\n" not in request_text                  # raw_request with embedded newline is a framing test, not corpus
        self._proc.stdin.write(request_text + "\n"); self._proc.stdin.flush()
        line = self._readline_with_timeout(10)           # thread + queue; no select() on Windows pipes
        return json.loads(line)
    def end_case(self):                                  # close stdin, expect exit 0 within 5 s, drain stderr
```

Role: per-step `caller` differs from the process role only if the case mixes
roles; since `--read-only` is per process, a case with mixed roles starts a
second process for the `user` steps (shim keeps a `{role: proc}` map; store
shared through the SQLite file, so `backend: sqlite` only - such cases carry
`"transports": {"skip": {"stdio": "..."}}` if they use `inmemory`).

Framing tests (`12_stdio_framing.py`, `unittest`):

| Test | Assertion |
|---|---|
| `test_ordered_pipeline` | write 50 `command.list`/`link.get` lines without reading; read 50 lines; ids echo in order |
| `test_malformed_line_continues` | `not json` line -> `invalid_envelope` envelope (id `srv-*`), next valid line still answered |
| `test_blank_line_ignored` | blank line produces no output line |
| `test_oversize_line` | line > `--max-request-body-bytes` -> `invalid_envelope` (or `invalid_field`; see 05.0) and process survives |
| `test_crlf_framing` | `\r\n` terminated lines accepted |
| `test_eof_clean_exit` | closing stdin yields exit code 0 and no partial output |
| `test_job_progress_between_lines` | `link.import` job accepted, further commands answered while it runs, `job.get` polling reaches terminal state |
| `test_stderr_is_not_protocol` | stdout contains only JSON lines; logs go to stderr |

## Implementation steps

1. Implement adapter with a reader thread pushing lines into `queue.Queue` (portable timeout).
2. Implement framing tests; keep each under 25 lines.
3. Register both.

## Commands

```bash
ctest --test-dir cmake-build -R "^conformance_1[12]_" --output-on-failure
```

## Tests

| Test file | Target | CTest label | Cases |
|---|---|---|---|
| `tests/conformance/py/11_stdio_corpus.py` | `conformance_11_stdio_corpus` | `integration;conformance` | whole corpus on `stdio` |
| `tests/conformance/py/12_stdio_framing.py` | `conformance_12_stdio_framing` | `integration;conformance` | 8 framing tests above |

## Constraints

- One process per case (state isolation), not per step.
- Never use `communicate()` mid-session (deadlocks); only the reader thread.
- Process must be killed in `end_case` on any failure to avoid orphaned binaries (`finally`).

## Success criteria

1. Corpus passes with 0 unexplained skips.
2. All 8 framing tests pass; the `test_ordered_pipeline` ordering assertion holds with 3 repetitions.
3. No zombie `url_shortener` process remains after the run (checked by test teardown via `proc.poll()`).

## Out of scope / risks

- The exact oversize-line behavior is owned by 05.0 (UNVERIFIED); the test accepts either documented error code but requires the process to survive.
- Concurrency of two stdio processes on one SQLite file: avoided except for the mixed-role case; if SQLite locking flakes, serialize those cases.
