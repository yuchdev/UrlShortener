# 08 - dry_run, async and job-submitter seam

**Parent task:** 01.0 Command model core
**State:** ⬜ Not started
**Depends on:** 01.0/04, 01.0/07
**Blocks:** 13, 05.0/04
**Estimated size:** S
**Suggested agent:** cpp-expert

## Objective

Replace the temporary `unsupported` fallback of step 5 in `execute` with the
real option handling: `options.dry_run` for descriptors that support it, and
`options.async` / `kind == job` routed to an injected `IJobSubmitter`. The
registry itself never runs jobs (the `JobRunner` is Task 05.0); it only defines
the seam and returns the "accepted as a job" envelope. Without a submitter,
job requests fail closed with `unsupported`.

## Dependencies

- 01.0/04 `JobInfo`, `make_job_accepted`.
- 01.0/07 pipeline.
- Catalog section 1 (`options`) and section 4 (job model): `async` applies only
  to `async_capable` commands, `kind == job` commands always run as jobs,
  `dry_run` only where `supports_dry_run`.

## Files affected

| Path | Create / modify | What changes |
|---|---|---|
| `include/url_shortener/commands/job_submitter.hpp` | create | `IJobSubmitter`, `JobSubmitResult` |
| `include/url_shortener/commands/command_registry.hpp` | modify | `set_job_submitter`, member |
| `src/commands/command_registry.cpp` | modify | Step 5 logic |
| `tests/unit/commands/07_execute_pipeline.cpp` | modify | Remove the temporary "dry_run/async/job -> unsupported" case (superseded here) |
| `tests/unit/commands/08_options_dry_run_async.cpp` | create | Tests |
| `sources.cmake` | modify | `COMMANDS_UNIT_SOURCES` += test (header-only addition needs no `.cpp` entry) |

## Classes and functions

```cpp
namespace url_shortener::commands {

struct JobSubmitResult
{
    std::optional<JobInfo> job;   // set on acceptance
    CommandError error;           // busy, internal, ...
};

/// Implemented by the Task 05.0 JobRunner. Called on the executing thread;
/// must return quickly (enqueue only).
class IJobSubmitter
{
public:
    virtual ~IJobSubmitter() = default;
    virtual JobSubmitResult submit(const CommandDescriptor& descriptor,
                                   const CallerContext& caller,
                                   const CommandRequest& request) = 0;
};

// CommandRegistry (additions)
void set_job_submitter(std::shared_ptr<IJobSubmitter> submitter);   // startup only
}
```

Step 5 semantics (after authorization and validation, before the handler):

| `dry_run` | `async` | descriptor | Result |
|---|---|---|---|
| true | any | `!supports_dry_run` | `unsupported` ("dry_run not supported") |
| true | true | any | `unsupported` (dry run and job are mutually exclusive) |
| true | false | `supports_dry_run`, sync | handler called with `ctx.dry_run = true`; success envelope; the handler must not mutate |
| true | false | `supports_dry_run`, `kind == job` | handler called synchronously with `ctx.dry_run = true` (validation pass of an import); no job is created |
| false | true | `!async_capable` and `kind == sync` | `unsupported` ("async not supported") |
| false | any | `kind == job` or (`async` and `async_capable`) | `submitter == nullptr` -> `unsupported`; else `submit(...)`: error -> `make_failure`, job -> `make_job_accepted` |
| false | false | sync | handler (as 07) |

An `async_capable` command that is also `kind == job` simply runs as a job.
`set_job_submitter` replaces any previous submitter; it is not thread-safe and
must be called before serving starts (documented, mirrors `add`).

## Implementation steps

1. Add `job_submitter.hpp`.
2. Add the member and setter to `CommandRegistry` (`std::shared_ptr` so the
   runner can outlive scoped test fixtures).
3. Rewrite step 5 as a small `resolve_dispatch(descriptor, options)` returning
   an enum `{handler, dry_run, job, error}` so the table above is a single
   switch; keep it a file-local function.
4. Tests, sources, build, run.

## Commands

```powershell
cmake --build cmake-build --target commands__08_options_dry_run_async
ctest --test-dir cmake-build -R "^commands__08_options_dry_run_async$" --output-on-failure
```

## Tests

| Test file | Target | Label | Cases |
|---|---|---|---|
| `tests/unit/commands/08_options_dry_run_async.cpp` | `commands__08_options_dry_run_async` | unit | below |

- Each row of the table above, with a fake submitter recording calls and a
  fake handler recording `ctx.dry_run`.
- `dry_run` on a supporting command: handler sees `dry_run == true`; a
  handler that would mutate is a counter the test asserts is unchanged by the
  handler's own dry-run branch (proves the flag reaches it).
- Job acceptance: response is `ok` with `job.id` / `job.state == queued` as
  returned by the fake submitter; `result` absent; serialized envelope matches
  the catalog job example.
- Submitter returns `busy` -> failure envelope with `busy` (HTTP 503 / exit 4
  via the table); returns `internal` likewise.
- No submitter set + job descriptor -> `unsupported`.
- Authorization and validation still run first: unauthorized `async` request
  -> `not_authorized`, submitter not called; invalid args -> `invalid_field`,
  submitter not called.
- `set_job_submitter(nullptr)` clears the seam.
- Exception thrown by the submitter -> `internal` (same isolation as handlers).

## Constraints

- The registry must not own threads or queues (Task 05.0 owns the runtime);
  `commands/` headers stay free of `<thread>` coupling beyond what `std::function`
  needs.
- A rejected or failed submission must not leave a half-created job: the
  contract on `IJobSubmitter` is "either returns a job or an error, never both".
- Milestone contracts: C1, C7 (authorization before submit), C2 (job envelope).
- Audit of async/dry-run outcomes is added in 09.

## Success criteria

1. `commands__08_options_dry_run_async` passes.
2. The table above is covered row by row (test names mirror the rows).
3. With no submitter set, no code path can create a job (test).

## Out of scope / risks

- Implementing `JobRunner`, bounded pool, cancellation, job store, and the
  `busy` policy (Task 05.0 subtasks 01-04; `busy` is 05.0/02, the dispatch
  through this seam is 05.0/04). The seam signature may need to grow a
  progress/cancel token; if so it changes in Task 05.0 and this header is the
  single place to edit.
- The one-shot CLI "wait for the job" behavior (catalog section 4) is an
  adapter concern (Task 04.0 / 05.0).
