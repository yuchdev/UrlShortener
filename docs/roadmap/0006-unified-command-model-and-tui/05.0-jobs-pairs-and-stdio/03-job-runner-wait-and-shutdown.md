# 03 - JobRunner wait, drain and shutdown

**Parent task:** 05.0 Jobs, pair import/export and stdio transport
**State:** ⬜ Not started
**Depends on:** 05.0/02
**Blocks:** 15 (stdio EOF drain), 20 (process-wide shutdown; also gives 04.0/13 `cmd` its `wait_for_terminal`)
**Estimated size:** S
**Suggested agent:** cpp-expert (tests: testing-expert)

## Objective

Add the blocking and lifecycle operations every long-lived or one-shot host needs: wait for one job to finish (one-shot `cmd` waits for `link.import`, catalog section 4), wait until the runner is idle (stdio EOF drain), cancel everything, and an orderly `shutdown()` that rejects new work, cancels queued jobs, signals running ones and joins the workers.

## Dependencies

- 05.0/02 (`JobRunner`, `JobStore`, cancel flags).
- Consumers: 04.0/13 (`cmd` waits unless `--no-wait`, through `CommandRuntime::job_runner`), 05.0/15 (`StdioAdapter` drain),
  05.0/20 (`shutdown_process_job_runtime`, used by the server, `cmd` and the TUI quit path).

## Files affected

| Path | Action | What changes |
|---|---|---|
| `include/url_shortener/commands/job_runner.hpp` | modify | declare `wait_for_terminal`, `wait_idle`, `cancel_all`, `shutdown` |
| `src/commands/job_runner.cpp` | modify | implement them with a second condition variable `m_idle_cv` |
| `tests/unit/commands/52_job_runner_shutdown.cpp` | create | cases below |
| `sources.cmake` | modify | add the test to `COMMANDS_UNIT_SOURCES` (no new `.cpp`) |

## Classes and functions

```cpp
// JobRunner additions (namespace url_shortener::commands)
std::optional<JobRecord> wait_for_terminal(const std::string& id,
                                           std::chrono::milliseconds timeout) const;
    // nullopt if id unknown; otherwise the record when terminal, or the latest
    // non-terminal snapshot if the timeout elapsed (caller checks is_terminal).
bool wait_idle(std::chrono::milliseconds timeout) const;
    // true when queued + running == 0 before the timeout.
std::size_t cancel_all();
    // cancels every queued job (-> cancelled) and flags every running job;
    // returns the number of jobs affected. Idempotent.
void shutdown();
    // 1. m_stopping = true (submit now returns busy "shutting down")
    // 2. cancel_all()
    // 3. notify all workers, join them (threads exit when queue empty and stopping)
    // 4. idempotent: second call returns immediately
```

`wait_*` are `const` and use `mutable` mutex/cv members; they wake on every `finish` (workers notify `m_idle_cv` after updating the store). The store's own mutex is never held while waiting on `m_idle_cv`.

## Implementation steps

1. Add `mutable std::condition_variable m_idle_cv` and notify it from the worker after each `finish` and from `cancel` of a queued job.
2. `wait_for_terminal`: loop `m_idle_cv.wait_until(lock, deadline, pred)` where `pred` re-reads the record from the store.
3. `wait_idle`: predicate `m_queue.empty() && m_running == 0` (add `std::size_t m_running` maintained by the worker loop under `m_mutex`).
4. `cancel_all`: lock, move all queued ids out of `m_queue`, finish each as cancelled (outside the lock where the store call is made), set every flag in `m_flags`.
5. `shutdown`: as listed; make the destructor call it. `join` is unconditional: there is no portable timed join, so drain timeouts are expressed by calling `wait_idle(timeout)` first and reporting the result before `shutdown()`.
6. Tests with latches; for the timeout case use a body that waits on a `promise` released by the test after the assertion.

## Commands

```text
cmake --build cmake-build --target commands__52_job_runner_shutdown
ctest --test-dir cmake-build -R "^commands__52_job_runner_shutdown$" --output-on-failure
ctest --test-dir cmake-build -R "^commands__5[12]_" --repeat until-fail:20
```
Windows/multi-config: `--config Debug` / `-C Debug`.

## Tests

| Test file | Target | Label | Cases |
|---|---|---|---|
| `tests/unit/commands/52_job_runner_shutdown.cpp` | `commands__52_job_runner_shutdown` | unit | `wait_for_terminal` returns the final record when a job finishes during the wait; returns a non-terminal snapshot on timeout (timeout 50 ms, body gated); unknown id -> nullopt; `wait_idle` true on empty runner (boundary: timeout 0), true after jobs finish, false while a gated job runs; `cancel_all` with 1 running + 3 queued returns 4, queued become `cancelled` without running, the running body sees the flag; `cancel_all` twice is a no-op the second time; `shutdown` joins after cooperative bodies return and rejects later `submit` with `busy`; `shutdown` twice is safe; destructor without explicit shutdown does not hang or leak (run with a queued job pending); partial results of a cancelled running job survive shutdown |

Manual check: none.

## Constraints

- No detached threads, no `std::thread::detach`, no `std::terminate` on exit paths.
- Waiting functions must not hold `m_mutex` while calling into `JobStore` accessors that take their own lock in a different order (document lock order: `m_mutex` before store mutex, never the reverse).
- C++17 only; `.cpp` already in `sources.cmake`; naming per `.clang-tidy`.
- C6: no argument values in the shutdown log lines.

## Success criteria

1. `commands__52_job_runner_shutdown` and `commands__51_job_runner_pool` pass 20 consecutive runs.
2. After `shutdown()` returns, `JobStore::active_count()` is 0 for cooperative bodies (asserted).
3. `submit` after `shutdown()` returns code `busy` (asserted).
4. No thread remains: the test process exits promptly (CTest default timeout not hit).

## Out of scope / risks

- Hard-killing a non-cooperative body; documented limitation (see 02).
- Lock ordering mistakes are the main risk; keep a comment block at the top of `job_runner.cpp` stating the order and run the tests under ThreadSanitizer where available.
