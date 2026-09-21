# Milestone 0003 - CLI & REST Command Interfaces - Status

Tracks progress against [plan.md](/docs/roadmap/0003-cli_rest_interfaces/plan.md).

## Current status

| Task | Name | Status | Tests |
|------|------|--------|-------|
| 01.0 | Command layer completion | ✅ Complete | `tests/unit/http/10_link_handlers.cpp` (characterization), `tests/unit/app/13-16_link_command_service_*.cpp` |
| 02.0 | CLI argument parsing | ⬜ Not started | none yet |
| 03.0 | CLI dispatch and process lifecycle | ⬜ Not started | none yet |
| 04.0 | CLI output and error contract | ⬜ Not started | none yet |
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
