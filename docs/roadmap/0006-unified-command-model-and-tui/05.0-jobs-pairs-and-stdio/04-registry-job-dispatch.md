# 04 - Registry job dispatch with the real JobRunner (kind=job and options.async)

**Parent task:** 05.0 Jobs, pair import/export and stdio transport
**State:** ⬜ Not started
**Depends on:** 05.0/02, 01.0/06 (`CommandDescriptor::job_factory`, job types), 01.0/07 (`CommandRegistry::execute`), 01.0/08 (`IJobSubmitter`, `set_job_submitter`, step-5 dispatch)
**Blocks:** 05, 08, 10, 20
**Estimated size:** S
**Suggested agent:** cpp-expert (tests: testing-expert)

## Objective

Prove the single job mechanism of the milestone (decision log D13) end to end with the real `JobRunner`:
a descriptor's `job_factory` (01.0/06) builds a self-contained `JobBody` on the caller thread after
authorization and `ArgSpec` validation; `CommandRegistry::execute` (01.0/08) hands it to the injected
`IJobSubmitter`, which is the `JobRunner` (05.0/02) held as `std::shared_ptr`; the caller immediately gets the
`job` envelope (`{"ok":true,"job":{"id":"j-9","state":"queued"}}`). A full queue yields `busy`; a registry
without a submitter yields `unsupported`. This subtask adds **no** new registry API: the dispatch code lives in
01.0/08; here it is exercised with real threads, real cancellation and the job commands' expectations.

## Dependencies

- 05.0/02 `JobRunner` (implements `IJobSubmitter`) and `JobContext` (implements `IJobContext`).
- 01.0/06 `JobHandlerFactory`, `JobBody`, `JobOutcome`, `CommandDescriptor::job_factory`, `async_capable`, `supports_dry_run`.
- 01.0/07 `execute` pipeline, `CallerContext`, `AccessGuard` enforcement.
- 01.0/08 `set_job_submitter(std::shared_ptr<IJobSubmitter>)`, step-5 table, `args_digest`.

## Files affected

| Path | Action | What changes |
|---|---|---|
| `tests/unit/commands/53_registry_job_dispatch.cpp` | create | integration of registry + real `JobRunner` (cases below) |
| `sources.cmake` | modify | test into `COMMANDS_UNIT_SOURCES` (no new `.cpp`) |

## Classes and functions

No new production symbols. Test helper (in the test file): a descriptor factory
`make_gated_job_descriptor(name, Permission, std::shared_ptr<std::promise<void>> gate)` whose `job_factory`
copies its args into a body that waits on the gate, reports progress through `IJobContext` and honours
`cancel_requested()`.

## Implementation steps

1. Build a registry, `auto runner = std::make_shared<JobRunner>(clock, JobRunnerOptions{1, 1, 16})`,
   `registry.set_job_submitter(runner)`.
2. Write the cases with gated bodies; a `ManualClock` for timestamps.
3. Run the 01.0 registry tests too (`commands__0*`), they must stay green.

## Commands

```text
cmake --build cmake-build --target commands__53_registry_job_dispatch
ctest --test-dir cmake-build -R "^commands__53_registry_job_dispatch$" --output-on-failure
```
Windows/multi-config: `--config Debug` / `-C Debug`.

## Tests

| Test file | Target | Label | Cases |
|---|---|---|---|
| `tests/unit/commands/53_registry_job_dispatch.cpp` | `commands__53_registry_job_dispatch` | unit | job command returns `ok:true` with `job.id` and `state:"queued"` and no `result`; the job reaches `succeeded` in `runner->get(id)` with the body's `result_json`; exactly one of `result|job|error`; `ControlSet::User` calling a write job -> `not_authorized` and factory NOT invoked; invalid args -> `invalid_field`, factory not invoked; factory error propagates, nothing queued, no job record; no submitter set -> `unsupported`; queue full (capacity 1, one running, one queued) -> `busy` envelope and `runner->list` unchanged; `options.async` on non-capable sync command -> `unsupported`; `options.async` on `async_capable` command runs via the runner; args copied: destroy the request buffer after execute, body still sees original values; cancel through `runner->cancel` while gated -> `cancelled` with partial `result_json`; two registries sharing the same `shared_ptr<JobRunner>` see the same jobs (the REST per-request case); audit sink receives `job_accepted` with the job id and never arg values |

Manual check: none.

## Constraints

- C1/C7: authorization and validation stay inside `execute`; adapters never see job internals.
- C2: the `job` envelope shape is exactly `{"id","state"}` (catalog section 1); additive only.
- C6: audit and `args_digest` never expose argument values.
- C9/C++17: no new library; header free of Beast.
- C10: nothing here is reachable from `GET /{slug}`.

## Success criteria

1. `commands__53_registry_job_dispatch` passes.
2. Unauthorized and invalid requests never invoke a `job_factory` (asserted by a counter in the test).
3. A rejected submit (`busy`) leaves `JobRunner::list` unchanged (asserted).
4. 01.0 registry tests still pass: `ctest --test-dir cmake-build -R "^commands__" --output-on-failure`.
5. `grep -rn "set_job_runner" include src` is empty (one mechanism only).

## Out of scope / risks

- Sync-with-wait helper for CLI `cmd` (04.0/13; the waiting itself is the follow-up edit owned by 05.0/20).
- Which runner is injected in each process is decided by 05.0/20 (one process-wide `std::shared_ptr<JobRunner>`).
