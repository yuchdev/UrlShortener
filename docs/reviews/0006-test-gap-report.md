# 0006 test-gap report (07.0/34, step 4)

**Milestone:** 0006 - unified command model and TUI
**Scope:** `src/commands`, `src/transport`, `src/tui`, `src/app` (+ `src/composition`),
`tests/conformance` (corpus meta).
**Method:** as-built inventory via `ctest --test-dir cmake-build -N` (398 registered
tests), source-to-test mapping across `tests/unit/**`, `tests/contract/**`,
`tests/integration/**`, `tests/conformance/**`, `tests/e2e/**`. Read-only on code; no
builds run from this subtask (the build/coverage matrix runs in parallel - see below).
**Date:** 2026-11-20.

## Coverage numbers

The parallel build/coverage agent's `docs/reviews/0006-test-matrix-evidence.md` had not
appeared at the time of writing, so lcov line/branch percentages are **pending** and are
to be pasted here when that file lands. The ranking below is the inspection-mapping
fallback documented by the `/test-gap` skill for the no-lcov case (declared as such).

## Summary verdict

**GREEN with follow-ups.** Every milestone-named priority target is covered by at least
one test at the right level; no open **P0**. One owner-assigned item (the `Result::detail`
SQL integration check, 07.0/34) is disposed below as a **waiver** with reason. Remaining
items are P2/P3 defense-in-depth follow-ups or recorded-decision deferrals.

**Priority counts:** P0 = 0, P1 = 0, P2 = 3, P3 = 2.

## Gap table

| ID | priority | path / function | description | suggested test | disposition |
|---|---|---|---|---|---|
| G-01 | P2 | `src/app/links_table_source.cpp::row_count/rows` + `src/commands/db_commands.cpp::handle_db_tables/handle_db_rows` (`Result::detail` SQL integration, owner 07.0/34) | No integration test injects a *real* SQL-layer failure through `db.tables`/`db.rows` to prove the envelope `error.detail` is the fixed, source-owned string and that no SOCI exception escapes the command. Unit coverage uses a fake with a fixed detail (`commands__27`); the SQL->`Result` mapping is covered separately (`sqlite__12`, `psql__10`). | `tests/integration/storage/16_db_tables_sql_failure.cpp`, target `storage__16_db_tables_sql_failure`: open a real SQLite backend, make the `links` listing fail (drop/rename the table after bootstrap), call `handle_db_tables`, assert `error.code == storage_failure`, `error.detail == "link listing failed"`, the call does not throw, and `detail` contains no file-path/DSN substring. | **waiver: SQL error text cannot reach the envelope by construction** - the real source maps every `!page.ok()` to the fixed literal `"link listing failed"` (`links_table_source.cpp:173,202`), `handle_db_tables` only forwards that source-owned detail, the SQL->`Result` conversion is covered by `sqlite__12_error_mapping_generic_failure`/`psql__10`, and the fake-based detail forwarding is covered by `commands__27`. Residual (exception-escape on a genuine mid-session SQL fault) is low-risk; recorded as a follow-up, not a 07.0 blocker. |
| G-02 | P2 | `tests/conformance/commands/30_link_import.json` (SSRF via import, 07.0/20) | The import corpus replays one refused SSRF vector (`http://2130706433/`, case `link_import_ssrf_invalid_url`). The hex/octal/short-form/IPv6 refused encodings are exhaustively asserted at unit level only (`commands__76`, `tests/unit/core/02_url_validation_reject_test`), not on the replayed real-binary path. | Add 2-3 refused rows to `30_link_import.json` (`http://0x7f000001/`, `http://0177.0.0.1/`, `http://[::1]/`) each expecting `invalid_url`; these run through the direct, TUI-client and stdio corpus runners, hardening the real-binary import path. | **follow-up** (defense-in-depth; the unit matrix after `752dad8`/`5d970c8`/`7eea03d`/`6a83c81` is already exhaustive, and one process-level vector proves the seam). |
| G-03 | P2 | `src/transport/stdio_adapter.cpp` + `src/commands/ndjson_session.cpp` partial/interrupted-frame path | Framing is covered (`commands__61/62`, `conformance 12_stdio_framing`, `integration/cli 40/41`). No test drives a mid-line EOF / split-write where the process is torn down between a partial frame and its newline, to prove no half-parsed request is dispatched. | `tests/integration/cli/42_serve_stdio_partial_frame.py`, target `cli_42_serve_stdio_partial_frame`: write a partial NDJSON line (no `\n`), close stdin, assert the process exits cleanly with no envelope emitted for the partial line and the prior complete line's envelope is intact. | **follow-up** (robustness; `ndjson_line_reader` buffering is unit-covered, overflow is covered by `31_import_limits_process_level::test_stdio_oversize_line_survives`). |
| G-04 | P3 | `src/tui/ftxui_poster.cpp` | No direct unit test. FTXUI thread-hop adapter (`post` onto the FTXUI loop). | None (boundary). `AsyncCaller` scheduling/stale-drop logic is already covered by `tui__03_async_caller_test` with a fake poster. | **waiver: FTXUI I/O boundary** - excluded like the other FTXUI-coupled adapters (`theme_ftxui.cpp`, `*_view.cpp`, `help_overlay.cpp`); logic lives in the FTXUI-free model libraries, which are tested. |
| G-05 | P3 | `src/tui/tui_entry.cpp` interactive loop | Only the option/boundary parse is unit-tested (`tui__19_tui_entry_boundary_test`, `cli__30/31`); the live interactive render/input loop is not automated. | None (manual). | **waiver: interactive TTY loop** - covered by the `docs/testing/tui_manual_smoke.md` pass (macOS pty complete; Windows/Linux/conhost/tmux are a deferred human pass, recorded). |

## Priority targets (one line each)

- **Redirect fast path (07.0/31, 32):** covered by `redirect__redirect_fast_path_guard`,
  `commands__80_redirect_isolation_test`, `tests/unit/core/08_redirect_status_test`,
  `tests/unit/http/12/13/30/32`, integration `01/04/05_*redirect*`,
  `tests/integration/link_management/02/03/05/06_*redirect*`, e2e `03_redirects.sh`.
  Perf baseline (`tests/perf/redirect/`) is `perf`-labelled / Release-CI (recorded
  deferral, not a gap).
- **SSRF via import (07.0/20):** covered - unit matrix `commands__76_import_row_validation_parity_test`
  + `tests/unit/core/02_url_validation_reject_test` (numeric IPv4 dec/hex/octal/short + trailing
  dot, `%2e`, `localhost.`, bracketed/mapped/NAT64/6to4 IPv6, CGNAT/benchmark/multicast/reserved/
  broadcast), shared `ssrf_vectors.hpp`; process-level corpus case `link_import_ssrf_invalid_url`.
  Gap **G-02** (broaden the replayed corpus vectors) is a P2 follow-up.
- **Authz matrix (07.0/19):** covered by `commands__74_authz_descriptor_matrix_test`,
  `commands__75_authz_before_side_effects_test`, conformance `41_authorization.json` /
  `42_authorization_matrix.json`, process-level `30_authz_process_level.py` (read-only
  denial before side effect, flag cannot be overridden by args).
- **Allowlist / redaction (07.0/18):** covered by `commands__70_db_table_allowlist_test`,
  `commands__71_jobs_rows_no_secrets_test`, `commands__72_system_info_key_set_test`,
  `tui__34_no_secrets_real_registry_test`, `observability__03_startup_logging_redaction`.
- **Job cancel and busy (07.0/13):** covered - cancel by `commands__54_job_commands`
  (cooperative `cancel_requested()` observation + `serialize_cancelled_*`) and
  `commands__51/52/53`; busy by the fixture case
  `conformance__01_direct_registry_corpus::direct_reports_busy_when_job_queue_is_full`
  (recorded corpus waiver for `busy`, 07.0/15).
- **Storage-path unification persistence (02.0):** covered by
  `tests/integration/cli/03_link_create_then_get_persists_state.py`,
  `tests/integration/cli/18_server_and_cli_share_sqlite.py`, and
  `storage__15_link_backend_bootstrap` (persistence across a `LinkBackend` reset, 0600 mode).
- **TUI confirm and read-only (07.0/28):** covered - confirm by `tui__06_confirm_test`
  (typed-slug delete, `MIGRATE` token, bidi-sanitized display, missing-slug fallback);
  read-only by `tui__18_readonly_matrix_test` (every screen sends zero writes; `--yes` does
  not bypass), `tui__18_readonly_render_test`, `tui__18_capability_test`,
  `tui__37_required_permission_forward_test`.

## Recorded decisions referenced (not new gaps)

Per the task brief these are already recorded in
`docs/roadmap/0006-unified-command-model-and-tui/status.md` (Notes & decisions) and are
not re-flagged; listed here with disposition only.

| Item | disposition |
|---|---|
| `parseRfc3339Zulu` missing-`Z` leniency | product decision pending at 07.0 close (status.md) |
| `cmd LINK.CREATE` (upper-case command) crash | product decision pending at 07.0 close |
| 400 `invalid_method` vs 405 | product decision pending at 07.0 close |
| `0.0.0.0` bind | product decision pending at 07.0 close |
| Export size cap | deferral recorded (08.0/03 catalog carries the value) |
| Busy TUI mirror | deferral recorded (06.0 close) |
| SIGKILL terminal restore | manual-only (29 traceability row; `docs/testing/tui_manual_smoke.md`) |
| Windows Terminal / conhost / Linux / tmux manual smoke | deferred human pass (2026-11-19 ruling) |
| Redirect perf baseline | Release-CI (`perf` label kept out of default `ctest`) |
| `Result::detail` SQL integration check (owner 07.0/34) | **waiver** - see gap **G-01** |
| 07.0/20 SSRF fix | resolved in `752dad8`/`5d970c8`, tests `7eea03d`/`6a83c81`; backport to `master` pending |
| 07.0/15 corpus coverage waivers (`storage_failure`, `internal`, `busy`) | approved (orchestrator decision), evidence-gated |

## Notes

- Inventory basis: `ctest --test-dir cmake-build -N` = 398 registered tests (full list
  captured during this analysis).
- No secrets, DSNs or tokens appear in this report.
- Gaps are to be closed in the next step (07.0/34 step 5); this subtask does not add tests.
