# 15 - StdioAdapter, EOF drain and shutdown semantics

**Parent task:** 05.0 Jobs, pair import/export and stdio transport
**State:** ⬜ Not started
**Depends on:** 05.0/03 (`wait_idle`, `cancel_all`), 05.0/12, 05.0/14
**Blocks:** 17, 19
**Estimated size:** S
**Suggested agent:** cpp-expert (tests: testing-expert)

## Objective

Provide `StdioAdapter : ITransportAdapter`: it supplies the local-operator `CallerContext`, runs an `NdjsonSession` on the provided streams and defines the shutdown contract of `serve-stdio`. Closing stdin (EOF) is the graceful stop: queued and running jobs are allowed to finish for a bounded time, then cancelled cooperatively with partial results kept, and the process exits 0.

## Dependencies

- 05.0/14 (`NdjsonSession`), 05.0/12 (interface and `TransportOptions`), 05.0/03 (`JobRunner::wait_idle`, `cancel_all`).
- `options.job_runner` is the process-wide runner (`CommandRuntime::job_runner`, 05.0/20); after this drain the caller (17) still calls
  `composition::shutdown_process_job_runtime` to join the workers.
- `ControlSet::Admin` / `ControlSet::User` (`security/control_set.hpp`, verified). Local-operator trust model: decision Q6, ADR 0002 "Authorization".

## Files affected

| Path | Action | What changes |
|---|---|---|
| `include/url_shortener/transport/stdio_adapter.hpp` | create | `StdioAdapter` |
| `src/transport/stdio_adapter.cpp` | create | `run`, drain logic |
| `src/composition/transport_factory.cpp` | modify | `"stdio"` branch returns a `StdioAdapter` |
| `tests/unit/commands/63_stdio_adapter.cpp` | create | cases below |
| `tests/unit/commands/60_transport_factory.cpp` | modify | flip the stdio expectation |
| `sources.cmake` | modify | add to `COMMON_CPP_SOURCES` (feeds `url_shortener_common`; the `URL_SHORTENER_CPP` list at the top of the file is legacy): `src/transport/stdio_adapter.cpp` (new `src/transport/` directory per the plan.md file map); test into `COMMANDS_UNIT_SOURCES` |

## Classes and functions

```cpp
namespace url_shortener::transport {
class StdioAdapter final : public commands::ITransportAdapter
{
public:
    StdioAdapter(std::istream& in, std::ostream& out, const composition::TransportOptions& options);
    std::string_view name() const override;        // "stdio"
    int run(commands::CommandRegistry& registry) override;
};
}
```

`run` algorithm:

1. `CallerContext`: principal `"local-operator"`, transport `"stdio"`, control set `Admin`, or `User` when `options.read_only` (field names UNVERIFIED, 01.0/06).
2. `end = NdjsonSession(in, out, options.max_line_bytes).serve(registry, caller)`.
3. Drain (only when `options.job_runner != nullptr`):
   - `wait_idle(options.drain_timeout)` (default 30 s); if it times out: `cancel_all()` then `wait_idle(5 s)` grace; log one `warn` line `stdio_drain_timeout` with the number of jobs cancelled (no ids needed beyond counts, no args).
   - After `output_failed`/`input_failed`: skip the long wait, `cancel_all()` immediately, grace 5 s.
4. Return codes: `eof` -> 0; `input_failed` -> 4; `output_failed` -> 4 (catalog exit code for `internal`; not 1, which means `not_found`).

Shutdown semantics to document in the class comment (and in 19):

| Event | Behavior |
|---|---|
| stdin EOF | stop reading; drain jobs as above; exit 0 |
| client reads stdout only after closing stdin | still works; remaining responses were already flushed; jobs finishing during drain produce no output |
| stdout closed / broken pipe | `output_failed`; cancel jobs; exit 4 (requires SIGPIPE ignored, subtask 17) |
| SIGINT / SIGTERM | no handler installed; default disposition terminates the process immediately. In-memory jobs are lost (non-goal), rows already created stay persisted. Parents must close stdin for a graceful stop. |
| unrecoverable registry fault | not possible: exceptions are mapped by the session |

No handler is installed on purpose: a portable interruptible blocking read on stdin does not exist (POSIX `SA_RESTART` semantics, Windows console reads are not interruptible), and a reader thread would only move the problem. EOF-as-shutdown is the documented contract.

## Implementation steps

1. Add the class; keep it in the new `src/transport/` directory (plan.md file map) with the header under `include/url_shortener/transport/`.
2. Implement `run` per the algorithm; no globals, no signal APIs.
3. Wire the factory branch and update the 60 test.
4. Tests with `std::stringstream` and a real `JobRunner`; use gated bodies and a short `drain_timeout` (50 ms) for the cancel path; never `sleep` to order events.

## Commands

```text
cmake --build cmake-build --target commands__63_stdio_adapter commands__60_transport_factory
ctest --test-dir cmake-build -R "^commands__6[03]_" --output-on-failure
```
Windows/multi-config: `--config Debug` / `-C Debug`.

## Tests

| Test file | Target | Label | Cases |
|---|---|---|---|
| `tests/unit/commands/63_stdio_adapter.cpp` | `commands__63_stdio_adapter` | unit | clean EOF -> 0, no jobs; EOF while a gated job runs: `run` waits (assert not returned before the gate opens), then returns 0 and the job is `succeeded`; drain timeout (50 ms) with a job that honors cancel: `cancel_all` called, job `cancelled` with partial result, return 0; output stream failure -> 4, running job cancelled; input `badbit` -> 4; `read_only=true`: write command -> `not_authorized`, read command works; admin default: write works; `job_runner==nullptr` -> no drain, returns immediately; `name()=="stdio"`; no bytes other than envelope lines are written to `out` (full-output equality in a scripted session) |

Manual check: none (see 18).

## Constraints

- C5: no `HttpServer`, sockets or `uri.txt`; header does not include Beast/Asio.
- C7: authorization stays in the registry; the adapter only supplies the caller (read-only downgrade per ADR 0002).
- C6: warn line carries counts only.
- Exit codes come from the catalog table (0 success, 4 internal); do not invent new ones.
- C++17, naming per `.clang-tidy`, `.cpp` in `sources.cmake`.

## Success criteria

1. `commands__63_stdio_adapter` and `commands__60_transport_factory` pass.
2. EOF with a running job returns only after the job reached a terminal state or the drain timeout elapsed (asserted).
3. No test or code path installs a signal handler: `grep -n "signal\|sigaction" src/transport/stdio_adapter.cpp` returns nothing.

## Out of scope / risks

- Interruptible reads, graceful SIGTERM handling, keep-alive/heartbeat lines.
- Risk: a client that closes stdin and immediately kills the process loses the drain; documented.
- Risk: 30 s default drain is arbitrary; exposed through `TransportOptions`, no CLI flag in V1.
