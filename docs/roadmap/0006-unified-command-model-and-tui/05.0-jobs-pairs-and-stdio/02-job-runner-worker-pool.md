# 02 - JobRunner worker pool, bounded queue and cancel

**Parent task:** 05.0 Jobs, pair import/export and stdio transport
**State:** ⬜ Not started
**Depends on:** 05.0/01, 01.0/08 (`IJobSubmitter`; job types of 01.0/06)
**Blocks:** 03, 04, 05, 21
**Estimated size:** M
**Suggested agent:** cpp-expert (tests: testing-expert)

## Objective

Implement `JobRunner`: a small fixed worker pool fed by a bounded FIFO queue. `submit` never blocks and never drops: a full queue returns a `busy` failure. Workers run a `JobBody` that reports progress and polls a cooperative cancel flag through `JobContext`. Cancelling a queued job completes it immediately; cancelling a running job flips its flag and the body stops at its next checkpoint, keeping partial results.

## Dependencies

- 05.0/01 (`JobStore`, `JobRecord`, `JobState`, `JobFailure`); 01.0/06 (`JobOutcome`, `JobBody`, `IJobContext`) and 01.0/08 (`IJobSubmitter`, `JobSubmitResult`). `JobOutcome::failure` is a `CommandError`; the runner stores it as `JobFailure{error_info_for(code).name, message}` (05.0/01 keeps its own type so the store stays free of envelope headers).
- Design decision (research item 6, see below): this class does NOT reuse `AnalyticsWorker` or `BoundedClickEventQueue`.
- `LoggerFactory::Component("job_runner")` / `Logger::info(msg, {{key, value}})` (verified in `observability/logger.hpp`).

### Reuse decision: AnalyticsWorker / BoundedClickEventQueue

Inspected `include/url_shortener/analytics/analytics_worker.hpp`, `bounded_click_event_queue.hpp` and their `.cpp`. Not reusable as-is:

| Aspect | Analytics primitives | Job needs |
|---|---|---|
| Element type | hard-typed `ClickEvent` (not a template) | arbitrary callable |
| Overflow | drop-on-overflow, `TryEnqueue` is `noexcept` and returns `DroppedFull` | reject with `busy`, never drop (ADR 0002 section 3) |
| Threads | exactly one worker thread, started only if `AnalyticsConfig::enabled` | N workers, always on |
| Work shape | batch + timed flush + retry to `IClickEventRepository` | one long task, progress, cancel |
| Lifecycle | `Stop()` drains by flushing; state is a plain atomic with non-atomic check-then-set in `Start` | cancel + join with bounded wait |

Templating the queue would modify protected analytics code and its 15 unit tests for no gain. Only the pattern is reused: `mutex + condition_variable + deque`, a `stop` flag that wakes all waiters, destructor calls `Stop`. Decision recorded in `decision-log.md` research item 6.

## Files affected

| Path | Action | What changes |
|---|---|---|
| `include/url_shortener/commands/job_runner.hpp` | create | `JobRunnerOptions`, `JobContext` (implements `IJobContext`), `JobRunner` (implements `IJobSubmitter`); includes `command_descriptor.hpp` and `job_submitter.hpp` (01.0, Beast-free) |
| `src/commands/job_runner.cpp` | create | pool, queue, worker loop, submit, cancel, get/list delegation |
| `tests/unit/commands/51_job_runner_pool.cpp` | create | cases below |
| `sources.cmake` | modify | add to `COMMON_CPP_SOURCES` (feeds `url_shortener_common`; the `URL_SHORTENER_CPP` list at the top of the file is legacy): `src/commands/job_runner.cpp`; add test to `COMMANDS_UNIT_SOURCES` |

## Classes and functions

`namespace url_shortener::commands`.

```cpp
struct JobRunnerOptions {
    std::size_t worker_count = 2;        // >= 1
    std::size_t queue_capacity = 16;     // queued (not running) jobs; >= 1
    std::size_t max_retained_jobs = 256; // forwarded to JobStore
};

// JobOutcome, JobBody and IJobContext are declared by 01.0/06 (commands/command_descriptor.hpp) and
// IJobSubmitter / JobSubmitResult by 01.0/08 (commands/job_submitter.hpp): one job mechanism (decision log D13).

class JobContext final : public IJobContext {   // handed to the body; owned by the worker
public:
    bool cancel_requested() const noexcept override;          // relaxed atomic load
    void set_total(std::uint64_t total) override;             // writes through JobStore::update_progress
    void advance(std::uint64_t n = 1) override;               // done += n
private:
    friend class JobRunner;
    JobContext(JobStore& store, std::string id, std::shared_ptr<std::atomic<bool>> cancel);
    ...
};

class JobRunner final : public IJobSubmitter {   // injected into registries as std::shared_ptr (05.0/20)
public:
    JobRunner(const IClock& clock, JobRunnerOptions options);   // starts workers; validates options (std::invalid_argument)
    ~JobRunner();                                               // calls shutdown() (defined in 03)
    JobRunner(const JobRunner&) = delete; JobRunner& operator=(const JobRunner&) = delete;

    // IJobSubmitter: busy -> {nullopt, busy}; accepted -> {JobInfo{id, "queued"}, none}
    JobSubmitResult submit(std::string command, std::string args_digest, JobBody body) override;
    std::optional<JobRecord> get(const std::string& id) const;
    JobPage list(std::optional<JobState> filter, std::size_t offset, std::size_t limit) const;
    std::optional<JobRecord> cancel(const std::string& id);
    // wait_for_terminal / wait_idle / cancel_all / shutdown: subtask 03.
private:
    void worker_loop();
    ...  JobStore m_store; std::mutex m_mutex; std::condition_variable m_cv;
         std::deque<Pending> m_queue; std::unordered_map<std::string, std::shared_ptr<std::atomic<bool>>> m_flags;
         std::vector<std::thread> m_workers; bool m_stopping = false;
};
```

Semantics:

- `submit`: under `m_mutex`; if `m_stopping` or `m_queue.size() >= queue_capacity` return `JobSubmitResult{std::nullopt, make_error(busy, "...")}` (nothing stored, no record created); else `m_store.create`, push `Pending{id, body, flag}`, `notify_one`, return `{JobInfo{id, "queued"}, {}}`.
- worker: pop, `mark_running`, run `body(ctx)` inside `try/catch (...)`. Mapping: exception -> `failed` with `{"internal","job failed"}` (the `what()` text goes to the log only, may contain paths); `outcome.cancelled` -> `cancelled` + `{"job_cancelled", "..."}` and keep `result_json`; `outcome.ok` -> `succeeded`; else `failed` with `outcome.failure` converted to `JobFailure`. Remove the flag from `m_flags`, log one `job_finished` line (id, command, state; never args).
- `cancel(id)`: unknown -> `nullopt`; terminal -> return record unchanged (no-op); queued -> erase from `m_queue`, `finish(cancelled, nullopt, {"job_cancelled",...})`; running -> set the flag, return the current record (state stays `running` until the body returns).
- A body that ignores the flag runs to completion; cancellation is cooperative by contract.

## Implementation steps

1. Header with the types above; keep Beast and `utils.hpp` out of it.
2. Constructor: build `JobStore(clock, max_retained_jobs)`, spawn `worker_count` threads running `worker_loop`.
3. `worker_loop`: `unique_lock`, `m_cv.wait(lock, [&]{ return m_stopping || !m_queue.empty(); })`; on stopping with empty queue return; pop under lock, release lock before running the body.
4. Implement `submit`, `get`, `list`, `cancel` as specified; take `m_mutex` only to touch `m_queue`/`m_flags`, never while running a body or calling into the logger.
5. Provide a stub `shutdown()` that sets `m_stopping`, notifies, joins (full semantics in 03); the destructor calls it so tests never leak threads.
6. Tests use `std::promise`/`std::future` and latches to gate bodies; no `sleep_for` to order events.
7. Register source/test; build; run.

## Commands

```text
cmake --build cmake-build --target commands__51_job_runner_pool
ctest --test-dir cmake-build -R "^commands__51_job_runner_pool$" --output-on-failure
ctest --test-dir cmake-build -R "^commands__51_job_runner_pool$" --repeat until-fail:20
```
Windows/multi-config: add `--config Debug` / `-C Debug`. The repeat run is the flake check; also run once under a sanitizer build if available (`-DCMAKE_CXX_FLAGS=-fsanitize=thread` on Linux/macOS).

## Tests

| Test file | Target | Label | Cases |
|---|---|---|---|
| `tests/unit/commands/51_job_runner_pool.cpp` | `commands__51_job_runner_pool` | unit | submit runs body and ends `succeeded` with result and progress; failed outcome -> `failed` with the code; thrown exception -> `failed`/`internal`, message does not contain the exception text; `worker_count=1, queue_capacity=2`: gate the running job, 2 more accepted, the 4th submit returns `busy` and creates NO record (store count unchanged); cancel queued job -> `cancelled`, body never invoked; cancel running job: body observes `cancel_requested()`, returns `cancelled=true` with partial `result_json`, state `cancelled`; cancel finished job is a no-op returning final state; cancel unknown id -> nullopt; flag set but body completes fully -> `succeeded`; two jobs run in parallel with `worker_count=2` (both gated, both observed running); progress `set_total`/`advance` visible through `get` while running; options validation throws on `worker_count=0` |

Manual check: none (covered by 18 for process-level behavior).

## Constraints

- Full queue returns `busy`; nothing is silently dropped (ADR 0002 section 3, catalog section 4).
- Bodies run on worker threads: they must only touch thread-safe services. `LinkCommandService` methods are `const` but its `ILinkStore` backends must be safe for concurrent use (legacy store and in-memory repository hold a `mutex`/`shared_mutex`; SQL repositories open a session per operation - UNVERIFIED for SQLite write contention, covered by the 08 concurrency test).
- C6: log lines and failure messages carry no argument values or file contents.
- C9/C++17: no new dependency, no `std::jthread`, no `std::stop_token` (C++20).
- Naming/format per `.clang-tidy`/`.clang-format`; `.cpp` in `sources.cmake`.

## Success criteria

1. `commands__51_job_runner_pool` passes 20 consecutive runs (`--repeat until-fail:20`).
2. A full queue yields `busy` and does not grow the record table (test case asserted).
3. A cancelled running job keeps its partial `result_json` (test case asserted).
4. `grep -n "AnalyticsWorker\|BoundedClickEventQueue" src/commands/job_runner.cpp include/url_shortener/commands/job_runner.hpp` returns nothing.
5. `decision-log.md` research item 6 carries the finding above.

## Out of scope / risks

- Priorities, per-command concurrency limits, job timeouts, retries: not in V1.
- A body that never checks the flag cannot be interrupted; 03 reports a timed-out drain to the caller, but the destructor still joins, so all bodies must poll the flag at least once per unit of work (one row, one page).
- Default `worker_count=2` is a guess; the TUI import plus one export is the expected peak.
