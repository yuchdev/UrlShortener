# 02 - CLI storage bootstrap

**Parent task:** 03.0 CLI dispatch and process lifecycle
**State:** ⬜ Not started
**Depends on:** 01
**Blocks:** 03

## Objective

`DispatchLinkCommand` needs a `LinkCommandServiceBundle` exactly like
`makeCommandService(config)` builds in `src/http/handlers/link_handlers.cpp`
today (`app::BuildLegacyLinkCommandService(config)`). This subtask wires
that construction into the CLI path, reusing the existing factory rather
than duplicating it.

## Files to modify

- `src/cli/link_command_dispatch.cpp` - call
  `app::BuildLegacyLinkCommandService(config)` (same factory the HTTP
  handlers use, from
  `include/url_shortener/composition/link_command_service_factory.hpp`),
  keeping the resulting `LinkCommandServiceBundle` alive for the duration
  of the single command invocation.

## Tests

- `tests/unit/cli/` - a test constructing `DispatchLinkCommand`'s bundle in
  isolation (or, more practically, an integration-level test in Task 05.0
  subtask 03 verifying `link create` followed by `link get` in the same
  process invocation is not required, since each CLI invocation is a fresh
  process - but a `link create` then a second `link get` invocation must
  observe the same `linkRepository()` singleton state to the extent the
  singleton itself persists across calls within one process's lifetime;
  confirm this against however `linkRepository()` is actually scoped before
  writing this test, since a purely in-memory, per-process singleton means
  two separate CLI invocations do **not** share state unless the underlying
  storage is process-external).

## Constraints

- No new storage adapter is introduced; this subtask strictly reuses
  `BuildLegacyLinkCommandService`, matching `plan.md`'s decision not to
  unify the two storage paths in this milestone.
- If `linkRepository()`'s in-memory-only, per-process nature means two
  separate CLI invocations (e.g. `link create` then, in a new process,
  `link get`) cannot see each other's data, that must be **documented**
  (Task 06.0) rather than silently discovered by a user - this subtask's
  job is to surface that fact precisely, since it directly determines
  whether the CLI is useful for multi-step scripting versus single-shot
  queries only.

## Success criteria

- [ ] `DispatchLinkCommand` builds its `LinkCommandServiceBundle` via the
      existing `BuildLegacyLinkCommandService` factory, not a new one.
- [ ] The cross-process state-visibility question above is answered
      explicitly (yes or no, with evidence) and recorded in `status.md`
      before Task 06.0 writes user-facing docs about it.
