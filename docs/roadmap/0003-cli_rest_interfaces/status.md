# Milestone 0003 - CLI & REST Command Interfaces - Status

Tracks progress against [plan.md](/docs/roadmap/0003-cli_rest_interfaces/plan.md).

## Current status

| Task | Name | Status | Tests |
|------|------|--------|-------|
| 01.0 | Command layer completion | ✅ Complete | `tests/unit/http/10_link_handlers.cpp` (characterization), `tests/unit/app/13-16_link_command_service_*.cpp` |
| 02.0 | CLI argument parsing | ✅ Complete | `tests/unit/cli/01_cli_parser_link_commands.cpp` |
| 03.0 | CLI dispatch and process lifecycle | ✅ Complete | `tests/unit/cli/02_cli_dispatch_reachable.cpp`, `tests/e2e/scripts/sections/14-18_cli_link_*.sh` |
| 04.0 | CLI output and error contract | ✅ Complete | `tests/unit/cli/03_cli_success_output.cpp`, `tests/unit/cli/04_cli_exit_codes.cpp` |
| 05.0 | Tests | ⬜ Not started | none yet |
| 06.0 | Docs and registry sync | ⬜ Not started | none yet |

**Legend:** ✅ Complete · 🔶 In progress / partial · ⬜ Not started

**Current gate status:** Milestone 0003 has not started. `link create`,
`link get`, and `link stats` already share `app::LinkCommandService` with
their REST handlers (pre-existing, not attributed to this milestone), but no
CLI entrypoint exists to invoke them - `CliParser`/`main.cpp` still only
support server-startup flags. See `plan.md`'s Background section for the
exact current state and the prior CLI-design documents this milestone
corrects (`docs/testing/cli_link_commands.md`'s `uri.txt` assumption).

## Notes & decisions

- **Scope boundary decided up front:** only the `Link` management command
  group (create/get/update/delete/enable/disable/restore/preview/stats) gets
  a CLI mirror. Observability, Compatibility aliases, Redirects, and the
  generic Fallback URI-store are explicitly excluded - see the "Explicitly
  out of scope" table in `plan.md`. This was decided before task breakdown
  to prevent scope creep into unrelated interfaces (e.g. an HTTP-polling CLI
  health check, or a CLI wrapper around the legacy Fallback store).
- **Known storage-path duality left unresolved on purpose:** the redirect
  fast path (`LinkService::Resolve`) and the link-management command layer
  (`LinkCommandService` via `LegacyLinkStore`) read/write through two
  different `IMetadataRepository`-named interfaces backed by different
  storage. This milestone mirrors the command layer as it exists today; it
  does not attempt to reconcile the two storage paths, which is a
  pre-existing condition unrelated to adding a CLI interface.

## Task 01.0 - Command layer completion

**Delivered.** `app::LinkCommandService` now has one method per in-scope
link command: the new `UpdateLink`, `DeleteLink`, `SetLinkEnabled`,
`RestoreLink` and `PreviewLink` sit beside the existing create/get/stats
methods, with matching `UpdateLinkCommand`, `DeleteLinkCommand`,
`SetLinkEnabledCommand` and `RestoreLinkCommand` DTOs and
`ILinkStore::update` / `LegacyLinkStore::update` (which wraps
`updateLinkAndInvalidateCache`, so cache invalidation is preserved on every
mutating path). `handlePatchLink`, `handleDeleteLink`,
`handleLifecycleAction` and `handlePreviewLink` in
`src/http/handlers/link_handlers.cpp` no longer call `getLinkForRead` /
`updateLinkAndInvalidateCache` directly; only `handlePlaceholderFeature` and
`handleLinkStats` still do, and both are out of scope.

**Key decisions.**

- Subtask 04 (characterization tests) was implemented *first*, before any
  migration, to honor plan contract C3; the README numbering is unchanged.
- The per-field PATCH 400 codes (`invalid_enabled`, `invalid_expires_at`,
  `invalid_tags`, `invalid_metadata`, `invalid_campaign`) and JSON body
  parsing deliberately stay in the handler: routing them through
  `codeForAppError` would collapse them into `invalid_request` and change the
  REST response. The service re-validates independently, so non-REST callers
  (the CLI in Tasks 02.0/03.0) cannot bypass validation.
- The pre-PR review caught one C3 regression that the subtask 04 tests had
  not covered: after migration, `PATCH` on a nonexistent slug with a
  malformed body returned 400 instead of 404. Fixed in `49a6e96` (existence
  check first, via `LinkCommandService::GetLink`) and pinned by
  `characterize_patch_not_found_precedes_body_validation`.
- `PreviewLink` reuses `GetLinkQuery`/`LinkView`; the reduced preview JSON
  shape remains a handler (transport) concern.

**Tests (before -> after).**

| Suite | Before (baseline) | After |
|-------|-------------------|-------|
| unit (`-L unit`) | 148/148 | 152/152 (+4 files `app__13`-`app__16`) |
| `10_link_handlers` Boost cases | pre-existing cases | +12 characterization cases, none removed or loosened |
| contract (`-L contract`, serial) | - | 8/8 |
| integration (`-L integration`) | - | 80/85 |
| e2e (`-L e2e`) | - | 11/13 |

The 7 remaining failures are all expected and need CLI mode, which Tasks 02.0
and 03.0 add: integration `cli_01`, `cli_02`, `cli_03`, `cli_07`, `cli_10`
and e2e `e2e_11_cli_link_create`, `e2e_12_cli_link_get`. (Under `-j4` three
`metadata__05`-`07` contract tests collide on a shared in-memory singleton;
this is a pre-existing isolation artifact and they pass serially.)

**Environment fixes made along the way** (not part of the task scope, needed
for a green baseline on macOS): the two psql source-scan tests read
pre-rename CamelCase paths (`a48b887`), and
`tools/sqlite_state_assert.py::assert_process_running` scanned `/proc`, which
does not exist on macOS (`3c86fa9`).

**Review.** `/pr-review` round 1: REQUEST_CHANGES (the PATCH ordering issue,
raised independently by feature-reviewer and security-auditor). Round 2:
feature-reviewer LGTM, security-auditor PASS with no findings. Threat model:
[docs/security/2026-09-21-link-command-layer-completion.md](/docs/security/2026-09-21-link-command-layer-completion.md).
No deferred subtasks.

## Task 02.0 - CLI argument parsing

**Delivered.** `ParseResult::command` now carries an optional `LinkCliCommand`
(`{LinkCliVerb verb; std::variant<the nine app:: DTOs>;}`), populated by
`CliParser::parse()` when `argv[1] == "link"`. All nine verbs -
`create`, `get`, `update`, `delete`, `enable`, `disable`, `restore`,
`preview`, `stats` - parse via `src/cli/link_command_args.cpp` into the
exact DTOs `LinkCommandService` already exposes; no CLI-local duplicate
structs. `link --help` / `link <verb> --help` describe all nine verbs and
their flags; top-level `--help` (server mode) is unchanged, pinned by an
exact-string golden test. This task adds **parsing only** - no dispatch, no
storage access, no process-lifecycle change; `main.cpp` does not read
`ParseResult::command` yet (that is Task 03.0), and every existing
server-flag behavior is unchanged (regression-tested).

**Key decisions.**

- `link update`'s three-state fields (`expires_at`, `campaign`) use the
  spec's exact syntax: `--expires-at <rfc3339>|clear` (the literal value
  `clear` means explicit-null) and `--campaign-*` / `--clear-campaign`
  (paired flags). `--tags a,b,c` and `--metadata k=v,...` are each a single
  comma-separated flag (present-with-value), with `--tags ""` /
  `--metadata ""` as the present-but-empty case. The CLI layer does only
  syntactic splitting; `LinkCommandService::UpdateLink` re-validates via
  `validateTags`/`validateMetadata`, so non-REST callers cannot bypass
  validation.
- A subtask-03 draft used a different, non-spec syntax (paired
  `--clear-expires-at`/`--tag`(repeatable)/`--clear-tags`/`--clear-metadata`
  flags). `/verify-subtask` caught the deviation against the spec's explicit
  argv examples; fixed in a follow-up commit to match the spec exactly.
- A mid-implementation agent lost its session mid-debug, having chased a
  ghost bug into the production parsing code (`fprintf` instrumentation
  left behind). The actual bug was in the test file: several
  `requireUpdate(CliParser{}.parse(...))` call sites passed a *temporary*
  `ParseResult` into a reference-returning helper, producing dangling
  references (read-back zeros, one segfault). Fixed by binding the
  `ParseResult` to a named local first at every call site (the pattern the
  subtask-02 tests already used), with a lifetime-caveat comment added to
  `requireUpdate` to prevent recurrence.
- Subtask 04's spec said "list all ten verbs"; the milestone's own command
  inventory has ten REST-endpoint rows, but `GET /api/v1/links/{slug}` and
  `GET /api/v1/links/id/{id}` both map to the single `link get --slug|--id`
  verb, so there are nine distinct CLI verbs. Corrected the spec doc rather
  than the (correct) implementation.

**Tests (before -> after).**

| Suite | Before (Task 01.0 baseline) | After |
|-------|------------------------------|-------|
| unit (`-L unit`) | 152/152 | 153/153 (new cases live inside `cli__01_cli_parser_link_commands`, one CTest target) |
| contract (`-L contract`, serial) | 8/8 | 8/8 |
| integration (`-L integration`) | 80/85 | 84/85 |
| e2e (`-L e2e`) | 11/13 | 10/13 |

`cli_01/02/03/07` (integration) turned green now that `link create`/`get`/
`stats` parse correctly enough for those tests' assertions; `cli_10`
remains red (needs real dispatch). **New, expected regression:**
`e2e_13_cli_no_server_socket` was passing before this task by accident -
`link create ...` previously failed Boost.ProgramOptions server-flag
parsing and exited fast, which incidentally satisfied "no socket bound".
Now that `link create` parses successfully into `ParseResult::command`,
but `main.cpp` still ignores that field and starts the full server (dispatch
is Task 03.0), the test hangs until its 30s timeout instead of failing fast.
`e2e_11`/`e2e_12` remain red for the same reason. All three require Task
03.0's dispatch to pass. No orphaned server processes were left behind by
the timeouts (checked after each full-suite run).

**Review.** `/pr-review`: feature-reviewer LGTM, security-auditor
PASS_WITH_FOLLOWUP (no CRITICAL/BLOCK). Threat model:
[docs/security/2026-09-23-cli-argument-parsing.md](/docs/security/2026-09-23-cli-argument-parsing.md).
Non-blocking follow-ups recorded, none gate this task's completion:

- `parseMetadataList` (used by `link update`) rejects a metadata value
  containing `=`, while `parseCreateArgs`'s metadata parsing does not -
  inconsistent between `create` and `update`. Fix by splitting on the first
  `=` only in both.
- CLI `--base-domain` skips `normalizeAndValidateBaseDomain`, which the
  HTTP path enforces - a validation-parity gap for Task 03.0 to close before
  `link create --base-domain` output is trusted.
- **Task 03.0 must route mutating verbs (`create`/`update`/`delete`/
  `enable`/`disable`/`restore`) through `AccessGuard::requireWrite` and the
  `auth_audit_log`** - the CLI parsing layer does not touch
  `ControlSet`/`AccessGuard` at all today, so nothing currently stops an
  unauthenticated dispatch path from mutating link state once wired up.
- Minor test-coverage gaps (both-selectors case for `link preview`,
  `link update --expires-at ""`) and a stale "tenth verb" comment - noted
  for Task 05.0.

No deferred subtasks.

## Task 03.0 - CLI dispatch and process lifecycle

**Delivered.** `src/main.cpp` now branches to CLI dispatch immediately after
the `parsed.help_requested` check and before `net::io_context io_context;`,
`HttpServer` construction, or the `uri.txt` load - a short-circuit, not a
start-and-tear-down. `url_shortener::cli::DispatchLinkCommand` (new,
`include/url_shortener/cli/link_command_dispatch.hpp` /
`src/cli/link_command_dispatch.cpp`) builds one `LinkCommandServiceBundle` via
`app::BuildLegacyLinkCommandService(config)` - the *same* factory
`src/http/handlers/link_handlers.cpp` uses - and calls the matching
`LinkCommandService` method for all nine verbs via `std::get<T>` on the DTO
variant. Every command returns a plain `int` (0/1) up through `main`'s
`return`, with no `std::exit`/`abort` bypassing the bundle's RAII cleanup, and
no background-thread component (`BoundedClickEventQueue`/`AnalyticsWorker`)
is reachable from the CLI path - confirmed by grep, independently reconfirmed
by both review rounds.

**Key decisions.**

- `DispatchLinkCommand`'s output is a deliberate placeholder
  (`"<verb> ok slug=<slug>"` / `"<verb> failed: <detail>"`), not JSON -
  the real success-JSON/error-envelope format is explicitly Task 04.0's job.
  This means `link create`/`get`/`stats`/`enable`(via `--help` scaffolding
  reuse) tests that assert JSON output will not go green until Task 04.0
  lands; see Tests below.
- **Cross-process state-visibility, answered per subtask 02's required
  success criterion:** `linkRepository()`
  (`src/storage/link_repository.cpp:87-91`) is a function-local `static`
  in-memory singleton, per-process only, with no disk-backed persistence.
  **Two separate CLI process invocations do NOT share state** - `link create`
  in one process is invisible to `link get` in a second process; only calls
  within the *same* process observe each other (proven by the
  `dispatch_create_then_get_roundtrip_same_process` unit test). Real CLI
  usage is one command per process, so in practice **the CLI cannot be used
  for create-then-read scripting across separate invocations** with the
  current in-memory backend - this is a real, user-facing limitation Task
  06.0 must document explicitly, not a bug to fix in this milestone (plan.md
  decided not to reconcile the two storage paths).
- Mid-task, `cli_parser.h` was found to pull in the entire HTTP stack
  (`HttpServer`, Boost.Asio) transitively, just to get `ServerConfig` for
  `ParseResult::config` - narrowed to include `core/config.h` directly
  (`85e3fd4`), verified via the actual preprocessed compile command, not just
  a source grep.
- The e2e no-server-socket guarantee (`13_cli_no_server_socket.sh`) was
  extended to the remaining six commands via five new sections
  (`14`-`18_cli_link_*.sh`, `enable`/`disable` share one file). A round-1
  review caught a real bug in all five: `if ! timeout 5 ...; then rc=$?`
  captured the *negated* exit status (always 0), making the hang-detection
  branch unreachable - a false negative on exactly the C4 invariant these
  tests exist to prove. Fixed in `1fe3919`
  (`rc=0; timeout 5 ... || rc=$?`), independently verified via `bash -c`
  under `set -euo pipefail` for the hang/normal/nonzero-exit cases by both
  the implementer and, separately, this session.

**Tests (before -> after).**

| Suite | Before (Task 02.0 baseline) | After |
|-------|------------------------------|-------|
| unit (`-L unit`) | 153/153 | 154/154 (+1: `cli__02_cli_dispatch_reachable`) |
| contract (`-L contract`, serial) | 8/8 | 8/8 |
| integration (`-L integration`) | 84/85 | 80/85 |
| e2e (`-L e2e`) | 10/13 | 16/18 (+5 new sections, all pass) |

Integration **regressed** from 84/85 to 80/85: `cli_01/02/03/08` newly fail
(dropped from passing after Task 02.0's parsing-only state) because real
dispatch now produces the placeholder text output instead of parseable JSON -
`json.loads(stdout)` fails on `"create ok slug=demo123"`. This is the
expected, direct consequence of landing real dispatch before Task 04.0's
output-format work, not a defect in this task's own scope (confirmed by
reading the actual test failure, not assumed). `cli_10` remains red (needs
the full envelope). `e2e_11`/`e2e_12` remain red for the same reason;
`e2e_13`-`18` (the no-server-socket family) are now all green. All seven
current failures are expected to turn green once Task 04.0 lands.

**Review.** `/pr-review` round 1: REQUEST_CHANGES (the dead
timeout-detection branch, caught by feature-reviewer and independently
reproduced with `bash -c` before delegating the fix). Round 2: feature-reviewer
LGTM, security-auditor PASS. Threat model:
[docs/security/2026-09-23-cli-argument-parsing.md](/docs/security/2026-09-23-cli-argument-parsing.md)
(Task 03.0 section appended). No new bypass vs. the REST path was found;
`--allow-private-targets` confirmed strictly per-invocation; no resource-
exhaustion or hang path. Two LOW/INFO items carried forward as milestone-level
follow-ups (not blocking): `--base-domain` validation parity, and CLI mutating
verbs not traversing `AccessGuard`/audit (verified to be parity with the
REST path's existing posture, not a gap this milestone introduces).

No deferred subtasks.

## Task 04.0 - CLI output and error contract

**Delivered.** `DispatchLinkCommand`'s placeholder output
(`"<verb> ok slug=X"` / `"<verb> failed: detail"`) is gone. On success, every
verb writes exactly one JSON line to stdout via the exact same serializer the
REST handler for that command already uses -
`app::serializeLinkViewJson` (create/get/update/delete/enable/disable/
restore/preview) or `app::serializeLinkStatsJson` (stats) - no second/forked
serialization path. On failure, a new `ExitCodeForAppError(app::AppErrorCode)`
(`src/cli/link_command_dispatch.{hpp,cpp}`) maps every one of the nine
`AppErrorCode` enumerators to an exit code (`none`->0, `not_found`->1,
`invalid_url`/`invalid_slug`/`invalid_field`/`reserved_slug`->2,
`slug_conflict`->3, `storage_failure`/`internal`->4, exhaustive switch, no
`default:`), with the message on stderr and stdout guaranteed empty.

**Key decisions.**

- `reserved_slug` maps to exit 2 (bad input) rather than exit 3
  (`slug_conflict`'s bucket) - a caller-chosen disallowed slug is a
  client-side input problem, distinct from a collision with an existing
  link. Documented inline.
- Along the way, an unrelated pre-existing bug was found and fixed:
  `cli_10`'s integration test queried `/health`, which was never a
  registered route (only `/healthz` exists, since the test was authored in
  an earlier commit). This test could never have passed regardless of any
  CLI work in this milestone; Task 03.0's status.md had incorrectly
  attributed its failure to "needs the full envelope." Fixed in `468fa48`.
- A feature-review finding (private members `buffer_`/`previous_` in the new
  `CoutCapture` test helper missing the `m_` prefix CLAUDE.md requires) was
  fixed directly in `a59dd16` rather than looped back through a subagent,
  since it was a trivial, unambiguous mechanical rename.

**Tests (before -> after).**

| Suite | Before (Task 03.0 baseline) | After |
|-------|------------------------------|-------|
| unit (`-L unit`) | 154/154 | 156/156 (+2: `cli__03_cli_success_output`, `cli__04_cli_exit_codes`) |
| contract (`-L contract`, serial) | 8/8 | 8/8 |
| integration (`-L integration`) | 80/85 | 84/85 |
| e2e (`-L e2e`) | 16/18 | 17/18 |

Every integration/e2e failure this task set out to close did close:
`cli_01/02/04/05/06/07/08/09/10` all pass, and `e2e_11` passes. **Only two
failures remain, both the same documented cross-process limitation** (plan.md
C5: the in-memory `linkRepository()` singleton is per-process, so a `get` in
a separate process invocation after a `create` returns `not_found` - not a
JSON/exit-code defect): `cli_03_link_create_then_get_persists_state` and
`e2e_12_cli_link_get`. Neither is fixable without a persistent backend, which
is out of this milestone's scope per [plan.md](/docs/roadmap/0003-cli_rest_interfaces/plan.md)'s storage-path decision;
recorded for Task 06.0's docs.

**Review.** `/pr-review`: feature-reviewer LGTM, security-auditor PASS with
no findings above informational (stdout output is byte-identical to REST's
serializer; `error.detail` remains static-literal-only across all nine
`AppErrorCode` paths now exercised, re-confirming Task 03.0's finding; the
exit-code mapping introduces no new information oracle relative to REST's
existing `codeForAppError`). One non-blocking naming fix applied
(`a59dd16`); two further non-blocking suggestions deferred to Task 05.0
(extract the duplicated `CoutCapture` test helper; add coverage for the
ok-but-no-value success path used by void-payload verbs).

No deferred subtasks.</new_string>

