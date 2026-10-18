# 20 - Process-wide job runtime in `build_command_runtime`

**Parent task:** 05.0 Jobs, pair import/export and stdio transport
**State:** ⬜ Not started
**Depends on:** 05.0/03 (`wait_idle`, `cancel_all`, `shutdown`), 05.0/04 (`set_job_runner`), 05.0/05 (`register_job_commands`), 05.0/08, 05.0/09, 05.0/10 (`register_pair_commands`, `PairCommandDeps`, `overwrite` support), 05.0/21 (`jobs` table source), 03.0/18 (`build_command_runtime`, `CommandRuntime`), 02.0/12 (`main.cpp` shutdown order)
**Blocks:** 11, 17, 04.0/13, 06.0/19
**Estimated size:** M
**Suggested agent:** cpp-expert (review: security-auditor for lifetimes and shutdown)

## Objective

Make `composition::build_command_runtime` (Task 03.0 subtask 18) the single
place where jobs are wired, for **every** process mode (milestone C11):

- one **process-wide** `JobRunner`, shared by every `CommandRuntime` of the
  process (REST builds a runtime per request, Task 04.0 subtask 02; CLI, TUI and
  `serve-stdio` build one per process);
- registration of `job.get` / `job.list` / `job.cancel` (05) and
  `link.import` / `link.export` (08-10) on the same registry as the link and
  browse commands, plus `registry->set_job_runner(&runner)` (04);
- the `jobs` table source (21) added to the same `TableCatalog` as `links` and
  `click_events`;
- one shutdown function that drains jobs (bounded), cancels the rest and joins
  the workers, called by every mode before it exits (server signal handler,
  `cmd`, `tui`, `serve-stdio`);
- the audit sink stays the one installed by 03.0/18 (`LoggerAuditSink`); job
  acceptance is audited with outcome `job_accepted` (01.0/09).

No mode builds its own registry after this subtask: `serve-stdio` (17), `cmd`
(04.0/13), `tui` (06.0/19) and the REST bridge (04.0/02) all call
`build_command_runtime`.

## Dependencies

- `JobRunner(const IClock&, JobRunnerOptions)`, `JobRunnerOptions {worker_count = 2, queue_capacity = 16, max_retained_jobs = 256}`,
  `wait_idle(ms)`, `cancel_all()`, `shutdown()`, `wait_for_terminal(id, timeout)` (subtasks 02-03).
- `CommandRegistry::set_job_runner(JobRunner*)` (non-owning, subtask 04); `register_job_commands(CommandRegistry&, JobRunner&)` (05);
  `register_pair_commands(CommandRegistry&, PairCommandDeps)` with `service`, `max_inline_bytes`, `list_page` (08, 10).
- `make_jobs_table_source(const JobRunner&)` (21).
- `CommandRuntime`, `CommandRuntimeOptions`, `build_command_runtime(const ServerConfig&, const CommandRuntimeOptions&)` (03.0/18).
- `SystemClock` (`core/clock.hpp`).
- Shutdown point of the server: the signal handler sketched in 02.0/12 (`server.stop()` -> drain -> `backend.flush()` -> `io_context.stop()`).

## Files affected

| Path | Action | What changes |
|---|---|---|
| `include/url_shortener/composition/job_runtime.hpp` | create | process-wide runner accessors and shutdown |
| `src/composition/job_runtime.cpp` | create | implementation; add to `COMMON_CPP_SOURCES` |
| `include/url_shortener/composition/command_runtime.hpp` | modify | `CommandRuntime::job_runner`; `CommandRuntimeOptions::file_paths` |
| `src/composition/command_runtime.cpp` | modify | register job and pair commands, `set_job_runner`, `jobs` table |
| `src/main.cpp` | modify | server signal handler: `shutdown_process_job_runtime(30 s)` before `backend.flush()` |
| `tests/unit/commands/64_command_runtime_jobs.cpp` | create | tests below |
| `sources.cmake` | modify | `COMMON_CPP_SOURCES` += .cpp; `COMMANDS_UNIT_SOURCES` += test |

## Classes and functions

```cpp
// composition/job_runtime.hpp   namespace url_shortener::composition
struct JobRuntimeSettings {
    commands::JobRunnerOptions runner;               // defaults of subtask 02
};
/// Call before the first runtime is built (main, per mode). Returns false (and changes nothing)
/// if the runner already exists.
bool configure_process_job_runtime(JobRuntimeSettings settings);
/// The process-wide runner; created on first use (function-local static holder with a
/// process-lifetime SystemClock), thread-safe. Never destroyed before
/// shutdown_process_job_runtime has run.
commands::JobRunner& process_job_runner();
/// wait_idle(drain_timeout); then cancel_all(); then shutdown() (joins workers). Idempotent;
/// a no-op if the runner was never created. Returns true when the drain finished in time.
bool shutdown_process_job_runtime(std::chrono::milliseconds drain_timeout);
/// Tests only: shut down and drop the runner so the next process_job_runner() creates a fresh one.
void reset_process_job_runtime_for_tests();

// command_runtime.hpp additions
struct PairFileOptions {                 // serve-stdio --path-root / --no-file-paths (05.0/16)
    bool allow_paths = true;
    std::optional<std::string> root;     // canonicalized by 05.0/07 rules
};
struct CommandRuntimeOptions {           // (03.0/18) gains:
    PairFileOptions file_paths;
};
struct CommandRuntime {                  // (03.0/18) gains:
    commands::JobRunner* job_runner = nullptr;   // == &process_job_runner(), not owned
};
```

`build_command_runtime` additions, after the link and browse registration of 03.0/18:

1. `auto& runner = process_job_runner();`
2. `register_job_commands(*registry, runner);`
3. `register_pair_commands(*registry, PairCommandDeps{link_service, server.max_request_body_bytes,
   list_page_from(link_lister), options.file_paths});` - `list_page_from` captures the
   `std::shared_ptr<app::ILinkLister>` **by value**.
4. `registry->set_job_runner(&runner); tables->add(make_jobs_table_source(runner));`
5. `runtime.job_runner = &runner;`

**Lifetime rule (the reason this is not trivial):** a job outlives the request
whose per-request `CommandRuntime` submitted it. Job bodies therefore capture
only shared ownership (`std::shared_ptr` copies of `LinkCommandService`,
`ILinkLister`, `TableCatalog` pieces they need) and plain values, never a
reference to the `CommandRuntime`, its registry or a `ServerConfig&`. 05.0/08 and
05.0/10 already hold `std::shared_ptr` in `PairCommandDeps`; this subtask adds the
test that proves it.

Shutdown, per mode (each call site is one line):

| Mode | Where | Call |
|---|---|---|
| server | signal handler, after `server.stop()`, before `backend.flush()` (02.0/12) | `shutdown_process_job_runtime(std::chrono::seconds(30))` |
| `cmd` | before `DispatchCmdCommand` returns (04.0/13) | same, after the optional wait |
| `link <verb>` sugar | no jobs are submitted; nothing to do (the call is a no-op) | - |
| `tui` | on quit, after the UI loop ends (06.0/19) | same |
| `serve-stdio` | the adapter's EOF drain (05.0/15) uses `runtime.job_runner->wait_idle/cancel_all`; then 05.0/17 calls | same |

## Implementation steps

1. Implement `job_runtime.hpp/.cpp` (a `std::once_flag` or mutex-guarded
   `std::unique_ptr<JobRunner>`; `configure_*` stores settings until first use).
2. Extend `CommandRuntime`/`CommandRuntimeOptions` and `build_command_runtime` as above.
3. Add the server shutdown line in `main.cpp`.
4. Write test 64; extend `commands__29_command_runtime_wiring` expectations
   (03.0/18) to include `job.get`, `job.list`, `job.cancel`, `link.import`,
   `link.export` and the `jobs` table, and the `db.tables` name set of `commands__30_browse_security_regression` to `{links, click_events, jobs}`.
5. Build; run the `commands__` set and the REST/CLI suites that build runtimes.

## Commands

```powershell
cmake --build cmake-build --target commands__64_command_runtime_jobs commands__29_command_runtime_wiring url_shortener
ctest --test-dir cmake-build -R "^commands__(29|64)_" --output-on-failure
ctest --test-dir cmake-build -L unit --output-on-failure
```

## Tests

| Test file | Target | Label | Cases |
|---|---|---|---|
| `tests/unit/commands/64_command_runtime_jobs.cpp` | `commands__64_command_runtime_jobs` | unit | below |

- Two runtimes built one after the other report the same `job_runner` pointer
  (process-wide); a job submitted through runtime A (`link.export`, inline) is
  visible with `job.get` through runtime B.
- **Lifetime:** submit a gated `link.export` through a runtime, destroy that
  runtime, open the gate -> the job finishes `succeeded` with the expected `rows`
  (no dangling reference; run under ASan where available).
- `command.list` contains the nine link commands, `link.list`, `db.tables`,
  `db.rows`, `system.info`, `command.list`, `job.get`, `job.list`, `job.cancel`,
  `link.import`, `link.export`; `db.tables` lists `links`, `click_events`, `jobs`.
- Audit: with a recording sink in `CommandRuntimeOptions`, a `link.import`
  submission records outcome `job_accepted` with the job id as target; `job.get`
  records nothing.
- `PairFileOptions{allow_paths=false}` -> `link.import` with `path` -> `unsupported`;
  `root` set -> a `path` outside the root -> `invalid_field` (rule from 05.0/07).
- Shutdown: `shutdown_process_job_runtime(50 ms)` with one gated running job and
  one queued job returns `false`, the queued job ends `cancelled`, the running job
  observes its cancel flag; a second call returns immediately; never-created
  runner -> returns `true` without creating one.
- `configure_process_job_runtime` after first use returns `false`.
- Every case ends with `reset_process_job_runtime_for_tests()`.

## Constraints

- C11 / C1: one registry builder; this file never includes transport headers
  (`http/`, `cli/`, `tui/`, `transport/`).
- C5: no listener; C10: redirect path untouched.
- No job body captures `CommandRuntime&`, `CommandRegistry&` or `ServerConfig&`.
- Shutdown never blocks longer than the given drain timeout plus the time running
  bodies need to observe cancellation (bodies poll once per row/page, 05.0 README).
- C++17; naming per `.clang-tidy`; `.cpp` in `COMMON_CPP_SOURCES`.

## Success criteria

1. `commands__64_command_runtime_jobs` and the updated `commands__29_command_runtime_wiring` pass.
2. `grep -rn "CommandRegistry registry" src/cli src/tui src/transport src/http` finds no registry construction outside `src/composition/`.
3. Server mode: `POST /api/v1/commands` `link.export` (gate enabled) followed by `GET /api/v1/jobs/{id}` in a later request returns the job (covered by 05.0/11 tests once this lands).

## Out of scope / risks

- Durable jobs (decision Q8).
- Tuning flags for worker count / queue capacity (defaults only; a later flag would call `configure_process_job_runtime`).
- Per-principal job visibility (Milestone 0002).
