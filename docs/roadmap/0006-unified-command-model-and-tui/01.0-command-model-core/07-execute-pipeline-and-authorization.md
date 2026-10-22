# 07 - Execute pipeline and authorization

**Parent task:** 01.0 Command model core
**State:** ⬜ Not started
**Depends on:** 01.0/06
**Blocks:** 08, 09, 10, 03.0/05, 03.0/07, 03.0/08, 03.0/09, 03.0/10, 03.0/14, 03.0/15, 03.0/17, 04.0/02, 04.0/09, 04.0/13, 05.0/04, 05.0/12, 05.0/14, 06.0/02, 06.0/18
**Estimated size:** M
**Suggested agent:** cpp-expert (review: security-auditor, because this is the authorization gate)

## Objective

Implement `CommandRegistry::execute` for plain synchronous commands: envelope
sanity, lookup, permission check through `AccessGuard`, argument validation,
the optional argument-dependent permission re-check (`permission_for`, D16), handler call with exception isolation, and result-to-envelope conversion. This
is the single entry point that every transport will use (ADR 0002 section 2,
milestone C1/C7). `options.dry_run` / `options.async` / `kind == job` are not
handled here (08) and, until then, fail closed with `unsupported`; audit is
added in 09.

## Dependencies

- 01.0/06 descriptor/registry types.
- `AccessGuard::requireRead/requireWrite/requireMigration/requireUserManagement`
  and `AccessDeniedError` (verified in `include/url_shortener/security/access_guard.hpp`;
  `src/security/access_guard.cpp` is in `SECURITY_SOURCES`, part of
  `COMMON_CPP_SOURCES`, so tests link it).

## Files affected

| Path | Create / modify | What changes |
|---|---|---|
| `src/commands/command_registry.cpp` | modify | Real `execute`, plus `execute_json` |
| `include/url_shortener/commands/command_registry.hpp` | modify | Declare `execute_json`, doc the pipeline |
| `tests/unit/commands/07_execute_pipeline.cpp` | create | Pipeline tests with fake handlers |
| `tests/unit/commands/06_registry_lookup.cpp` | modify | Remove the "stub execute returns internal" case |
| `sources.cmake` | modify | `COMMANDS_UNIT_SOURCES` += test |

## Classes and functions

```cpp
// CommandRegistry (additions)
CommandResponse execute(const CallerContext& caller,
                        const CommandRequest& request) const;

/// Convenience used by REST generic, stdio, CLI `cmd`: parse -> execute ->
/// (caller serializes). Parse failures become failure responses with the
/// best-effort id from parse_request.
CommandResponse execute_json(const CallerContext& caller,
                             std::string_view request_text,
                             const envelope_limits& limits = {}) const;
```

Pipeline in `execute`, in this fixed order (each step returns a failure
response and stops):

1. `request.v != 1` or empty `command` -> `invalid_envelope` (defense in depth;
   `parse_request` normally catches it).
2. `find(request.command)` -> `unknown_command` (message: `unknown command`;
   the unknown name is not echoed).
3. Permission: build `AccessGuard guard(caller.control_set)`; call the
   `require*` matching `descriptor.permission`; catch `AccessDeniedError` ->
   `not_authorized` with fixed message `permission denied`. This happens
   before argument validation so an unauthorized caller learns nothing about
   the schema.
4. `validate_args(descriptor.args, request.args_json, &view, &error)` ->
   `invalid_field` (message `<arg>: <reason>` from 05).
4a. Argument-dependent permission (decision log D16): if
   `descriptor.permission_for` is set, `p = max(descriptor.permission,
   descriptor.permission_for(view))` (order `read < write < migrate <
   manage_users`); `permission_check(caller, p)` false -> `not_authorized` (same
   fixed message). A hook that throws -> `internal`. The `max` makes the
   re-check tighten-only, and the base check of step 3 has already run, so an
   unauthorized caller still learns nothing about the schema.
5. Options/job handling: if `request.options.dry_run`, `request.options.async`
   or `descriptor.kind == ExecutionKind::job` -> `unsupported` with message
   `option not supported yet` (replaced in 08).
6. Call `descriptor.handler(ctx, view)` inside `try { } catch (const
   std::exception&) { } catch (...) { }`; any exception -> `internal` with fixed
   message `internal error` (never `e.what()`: it may contain paths or SQL).
7. Convert: handler error -> `make_failure(request.id, result.error)`; handler
   success with empty `result_json` -> `internal` (programming error);
   otherwise `make_success`.

`execute` and `execute_json` are `const` and never throw. Thread-safety: the
registry is read-only after startup, so concurrent `execute` calls are safe;
thread-safety of handlers is the handler's concern.

## Implementation steps

1. Add a file-local `permission_check(const CallerContext&, Permission)`
   returning `bool` (true = allowed) that wraps the `AccessGuard` calls and the
   try/catch.
2. Implement `execute` following the order above; keep each step a small
   early-return.
3. Implement `execute_json` with `parse_request`.
4. Replace the 06 stub test; write the new tests with a `FakeHandlerRecorder`
   (counts calls, returns canned `CommandResult`).
5. Build and run.

## Commands

```powershell
cmake --build cmake-build --target commands__06_registry_lookup commands__07_execute_pipeline
ctest --test-dir cmake-build -R "^commands__0[67]_" --output-on-failure
```

## Tests

| Test file | Target | Label | Cases |
|---|---|---|---|
| `tests/unit/commands/07_execute_pipeline.cpp` | `commands__07_execute_pipeline` | unit | below |

- Happy path: read command, Admin and User both succeed; response echoes the
  request id and carries the handler's `result_json` verbatim.
- `unknown_command`; message does not contain the submitted name.
- Authorization matrix: for each `Permission` x `ControlSet`
  (`read`: Admin/User ok; `write`, `migrate`, `manage_users`: only Admin ok) the
  response is ok or `not_authorized`; **handler call count is 0 on denial**.
- Denial precedes validation: an unauthorized caller with invalid args gets
  `not_authorized`, not `invalid_field`.
- `permission_for` (D16): a `read` descriptor whose hook returns `write` when
  arg `path` is present -> User without `path` ok, User with `path`
  `not_authorized` and handler not called, Admin with `path` ok; hook that
  throws -> `internal`; invalid args still give `invalid_field` before the hook
  runs (hook never sees an unvalidated view); a `write` descriptor whose hook
  returns `read` still requires `write` (tighten-only).
- Validation errors: unknown key, missing required, wrong type -> `invalid_field`
  and handler not called; message names the arg and not the value.
- Handler returns `not_found` / `slug_conflict` -> same code in the envelope,
  handler message preserved.
- Handler throws `std::runtime_error("secret-dsn")` / throws an `int` ->
  `internal`, message `internal error`, output does not contain `secret-dsn`.
- Handler returns ok with empty `result_json` -> `internal`.
- `dry_run`, `async`, and a `job` descriptor each -> `unsupported` (temporary,
  superseded by 08).
- `execute_json`: malformed JSON -> `invalid_envelope`; valid text round-trips
  through `serialize_response`.
- 8 threads x 1000 calls on one registry produce consistent results (smoke
  for the read-only claim; run under the unit label, must stay < 2 s).

## Constraints

- Authorization only here, never in adapters (C7); a regression test pins that
  the handler is not entered on denial.
- Fixed error messages; no `e.what()`, no arg values (C6 spirit).
- Do not call `LinkCommandService` or any storage type here; handlers are
  injected by tests as lambdas.
- C++17; exceptions are caught at the boundary only.

## Success criteria

1. `commands__07_execute_pipeline` passes.
2. The authorization matrix test shows `ControlSet::User` calling any
   `write|migrate|manage_users` command yields `not_authorized` with the
   handler never invoked.
3. A thrown exception message never reaches the serialized response (test
   searches the output).

## Out of scope / risks

- `dry_run`, `async`, jobs (08); audit (09); `command.list` (10).
- Rate limiting of command execution per caller is not part of this model;
  public-create rate limiting (0004 SC5) is applied at the REST layer for
  `link.create` (plan.md).
- `AccessGuard` throws for control; the try/catch cost is on the denial path
  only.
