# Milestone 0003 - CLI & REST Command Interfaces - Status

Tracks progress against [plan.md](/docs/roadmap/0003-cli_rest_interfaces/plan.md).

## Current status

| Task | Name | Status | Tests |
|------|------|--------|-------|
| 01.0 | Command layer completion | ⬜ Not started | none yet |
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
