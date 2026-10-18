# 04 - Registry job dispatch (kind=job and options.async)

**Parent task:** 05.0 Jobs, pair import/export and stdio transport
**State:** ⬜ Not started
**Depends on:** 05.0/02, 01.0/06 (`CommandDescriptor`, `CommandResponse` with `job`), 01.0/07 (`CommandRegistry::execute`), 01.0/08 (dry-run/async/job seam)
**Blocks:** 05, 08, 10, 20
**Estimated size:** M
**Suggested agent:** cpp-expert (tests: testing-expert)

## Objective

Teach `CommandRegistry::execute` to run `kind=job` commands (and `async_capable` sync commands with `options.async=true`) through a `JobRunner`: validate and authorize on the caller thread, build a self-contained `JobBody`, submit it, and answer immediately with the `job` envelope (`{"ok":true,"job":{"id":"j-9","state":"queued"}}`). A full queue yields `busy`; a registry without a runner yields `unsupported`.

## Dependencies

- 05.0/02 for `JobRunner::submit`.
- 01.0/06 (descriptor: `kind`, `async_capable`, `supports_dry_run`), 01.0/07 (`execute` pipeline, `CallerContext`, `AccessGuard`), 01.0/08 (the
  dry-run/async job seam and `JobInfo`/`make_job_accepted` from 01.0/04). Some final symbol names are UNVERIFIED (specs were written concurrently); the capabilities this subtask needs from them are listed here and must be reconciled when implementing: `CommandDescriptor::kind` (`ExecutionKind::sync|job`), an `async_capable` flag, `CommandRequest::options.async/dry_run`, `CommandResponse` able to carry a `job` object, `CallerContext`, `ArgsView`, `AccessGuard` enforcement already inside `execute`.
- External library: OpenSSL `SHA256` (already linked via `url_shortener_common`) for `args_digest`.

## Files affected

| Path | Action | What changes |
|---|---|---|
| `include/url_shortener/commands/command_registry.hpp` | modify | add `JobHandlerFactory`, descriptor field `job_factory`, `CommandRegistry::set_job_runner(JobRunner*)` |
| `src/commands/command_registry.cpp` | modify | job branch in `execute`; digest helper in anonymous namespace |
| `tests/unit/commands/53_registry_job_dispatch.cpp` | create | cases below |
| `sources.cmake` | modify | test into `COMMANDS_UNIT_SOURCES` (no new `.cpp`) |

01.0/08 implements the seam (the branch that hands a job to a runner interface); this subtask plugs the real `JobRunner` into it and adds whatever delta is missing; do not duplicate.

## Classes and functions

```cpp
// NEW in command_registry.hpp. ArgsView is a non-owning view of the request, so a
// factory must COPY every value it needs into the returned body.
using JobHandlerFactory =
    std::function<std::variant<JobBody, CommandError>(const CommandContext&, const ArgsView&)>;

// CommandDescriptor gains (UNVERIFIED shape of the existing struct, see Dependencies):
//   JobHandlerFactory job_factory;   // required iff kind == ExecutionKind::job
//   bool async_capable = false;      // sync handler may also run as a job

void CommandRegistry::set_job_runner(JobRunner* runner) noexcept;   // non-owning; null = jobs unsupported
```

`execute` job branch, in order (steps 1-4 are the existing pipeline, unchanged):

1. envelope validation; 2. lookup; 3. permission (`AccessGuard`) -> `not_authorized`; 4. `ArgSpec` validation -> `invalid_field`.
5. If descriptor `kind==job` or (`options.async` and `async_capable`): else if `options.async` and not capable -> `unsupported`.
6. No runner -> `unsupported` ("jobs not available in this process").
7. Call `job_factory(ctx, args)`; a returned `CommandError` (bad `path`, `unsupported` transport, unknown `on_conflict`) is returned as the error response and NOTHING is queued.
8. `runner->submit(name, args_digest, std::move(body))`; `!accepted` -> `busy` response; else `CommandResponse{ok, job={id, state="queued"}}`.
9. Audit record for mutating commands is emitted at submission through the registry's `IAuditSink` (01.0/09): outcome `job_accepted` with the job id as target, or the error code name (`busy`, `unsupported`, ...) when nothing was queued; never args values.

`args_digest`: first 16 hex chars of `SHA256(raw args text)` using `hexEncode` from `core/utils.h` (verified signature `hexEncode(const unsigned char*, size_t)`), computed in the `.cpp` only.

`options.dry_run` is passed to the factory via `CommandContext::dry_run` (UNVERIFIED field; add if missing): job commands that support dry-run (link.import) implement it inside the body, so the job still appears in `job.get` with counts and no mutation.

## Implementation steps

1. Extend `CommandDescriptor`/`CommandRegistry` headers as above; keep Beast/`utils.h` out of the header.
2. Implement the job branch after authorization and `ArgSpec` validation so unauthorized callers never reach a factory (factories open files).
3. Implement the digest helper; unit-test determinism and length (16 chars, hex).
4. Write the tests with a fake descriptor whose body is gated by a promise; use a `ManualClock` and a real `JobRunner` with `worker_count=1`.
5. Verify the catalog statement: sync-kind command with `options.async=true` and no `async_capable` -> `unsupported`.

## Commands

```text
cmake --build cmake-build --target commands__53_registry_job_dispatch
ctest --test-dir cmake-build -R "^commands__53_registry_job_dispatch$" --output-on-failure
```
Windows/multi-config: `--config Debug` / `-C Debug`.

## Tests

| Test file | Target | Label | Cases |
|---|---|---|---|
| `tests/unit/commands/53_registry_job_dispatch.cpp` | `commands__53_registry_job_dispatch` | unit | job command returns `ok:true` with `job.id` and `state:"queued"` and no `result`; exactly one of `result|job|error`; `ControlSet::User` calling a write job -> `not_authorized` and factory NOT invoked; invalid args -> `invalid_field`, factory not invoked; factory error propagates, nothing queued, no job record; no runner set -> `unsupported`; queue full -> `busy` envelope; `options.async` on non-capable sync command -> `unsupported`; `options.async` on `async_capable` command runs via runner; args copied: mutate/destroy the request buffer after execute, body still sees original values; `args_digest` is 16 hex chars and equal for equal args; audit hook receives command/outcome and never arg values |

Manual check: none.

## Constraints

- C1/C7: authorization and validation stay inside `execute`; adapters never see job internals.
- C2: the `job` envelope shape is exactly `{"id","state"}` (catalog section 1); additive only.
- C6: audit and `args_digest` never expose argument values.
- C9/C++17: no new library; header free of Beast; `.cpp` already listed.
- C10: nothing here is reachable from `GET /{slug}`.

## Success criteria

1. `commands__53_registry_job_dispatch` passes.
2. Unauthorized and invalid requests never invoke a `job_factory` (asserted by a counter in the test).
3. A rejected submit (`busy`) leaves `JobRunner::list` unchanged (asserted).
4. 01.0 registry tests still pass: `ctest --test-dir cmake-build -R "^commands__" --output-on-failure`.

## Out of scope / risks

- Sync-with-wait helper for CLI `cmd` lives in 04.0/13 (uses 03's `wait_for_terminal` through 05.0/20's `CommandRuntime::job_runner`).
- Which `JobRunner` is attached in each process is decided by 05.0/20 (one process-wide runner), not here.
- Risk: 01.0/06's descriptor may already model job handlers differently; reconcile names rather than adding a second mechanism.
- Risk: dry-run flag plumbing (`CommandContext::dry_run`) is owned by 01.0/08; if absent, add the single field there rather than a side channel.
