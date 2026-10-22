# 13 - Corpus: jobs, import and export

**Parent task:** 07.0 Tests and conformance
**State:** ⬜ Not started
**Depends on:** 01, 04; Task 05.0 (job runtime, `link.import`, `link.export`, `job.*`)
**Blocks:** 15
**Estimated size:** M
**Suggested agent:** testing-expert

## Objective

Pin the job lifecycle (accepted envelope, polling, terminal states,
cancellation, `busy`) and the pair commands `link.import` / `link.export` on all
transports, using `await_job` so results are compared deterministically.

## Dependencies

- Catalog sections 3.3, 3.4, 4 and the schema `await_job` / `expect_accept` (subtask 01).
- Job runner knobs (queue capacity, worker count) as exposed by 05.0 for tests; **UNVERIFIED**: whether a config/CLI flag exists to make the queue size 1 for the `busy` case - if not, the `busy` case is `direct`-only through the fixture and skipped elsewhere with a reason.

## Files affected

| Path | create/modify | What changes |
|---|---|---|
| `tests/conformance/commands/30_link_import.json` | create | import cases |
| `tests/conformance/commands/31_link_export.json` | create | export cases |
| `tests/conformance/commands/32_job_commands.json` | create | `job.get/list/cancel`, `busy` |

## Classes and functions (cases)

**30_link_import.json (14 cases)**, `files` supplies payloads, `${tmp}/pairs.csv` for path forms:

- `link_import_csv_inline_happy` (3 rows, one blank slug -> generated): `expect_accept` `{"ok":true,"job":{"id":"<id>","state":"queued"}}`; final `job.get` result `{total:3, created:3, skipped:0, failed:0, errors:[]}`; follow-up `link.list status=all` shows 3.
- `link_import_ndjson_inline_happy`; `link_import_csv_path` (skip `rest_generic`, `rest_resource` with reason "path is rejected on REST" - the REST behavior has its own case below); `link_import_path_rejected_on_rest` (only `rest_generic`: `unsupported`).
- `link_import_on_conflict_skip` (default; existing slug counted `skipped`); `link_import_on_conflict_fail` (job `failed` or result `failed` count per catalog; assert what 05.0 documents); `link_import_on_conflict_overwrite` (not offered by REST: `rest_generic` expects `unsupported`; other transports succeed).
- `link_import_row_errors_reported` (invalid_url row, invalid slug row, reserved slug row: `errors:[{"line":2,"code":"invalid_url"},...]`, other rows created; lines are 1-based per 05.0 - **UNVERIFIED**).
- `link_import_errors_capped_at_100` (150 bad rows -> `errors` length 100, `failed` 150).
- `link_import_dry_run` (`options.dry_run`: counts reported, no rows created: follow-up `link.list total` unchanged).
- `link_import_empty_payload` (`invalid_field`); `link_import_bad_format` (`format:"xml"` -> `invalid_field`); `link_import_data_and_path_both` (`invalid_field`); `link_import_missing_source` (`invalid_field`).

**31_link_export.json (8 cases):** `link_export_csv_inline` (job result `{rows:n, path:null, data:"..."}` - assert `data` with `<regex:...>` for header and rows, rows in `created_at` order); `link_export_ndjson_inline`; `link_export_filtered_by_tag`; `link_export_status_all_includes_deleted`; `link_export_to_path` (non-REST; then the case reads back through `link.import` of the same file for a round trip: `import(export(x)) == x` on slug/target pairs); `link_export_path_rejected_on_rest`; `link_export_data_size_cap` (many rows -> job result truncated/failed per 05.0 cap; **UNVERIFIED** cap value, case written after 05.0 fixes it); `link_export_read_only_allowed` (caller `user`, `read` permission).

**32_job_commands.json (10 cases):** `job_get_unknown_id_not_found`; `job_list_empty`; `job_list_state_filter`; `job_list_pagination`; `job_get_progress_shape` (`progress{done,total}` ints); `job_cancel_unknown_not_found`; `job_cancel_running_import` (large payload; `cancel` then `job.get` ends `cancelled`; partial rows kept and reported; **timing**: use a payload of 2000 rows and cancel immediately; accept `succeeded` only if the case declares `match` accordingly - prefer a deterministic hook, see Constraints); `job_cancel_finished_noop` (returns final state); `job_cancel_requires_write` (caller `user` -> `not_authorized`); `job_queue_full_busy` (direct only).

## Implementation steps

1. Add `${tmp}`-based `files` for path cases; use forward slashes in expected strings (matcher normalizes).
2. Run on `direct` then `stdio` (jobs live in the process), then `rest_generic`; `cli_cmd` runs wait-for-completion mode (see subtask 06 constraints), so cases using `expect_accept` carry `transports.skip.cli_cmd` with a reason unless the dedicated `--no-wait` case in this file covers it: add `job_accept_envelope_no_wait` (`cli_cmd` only; asserts the accepted envelope; the job is lost when the process exits, documented).
3. For cancellation determinism, request a test hook from 05.0 (a debug-only slow-row delay is NOT acceptable in product code); otherwise, make the case robust by importing enough rows and asserting `state in {cancelled, succeeded}` via `<regex:cancelled|succeeded>`, then asserting consistency `created + skipped + failed == total` when succeeded and `<= total` when cancelled (second assertion expressed as a follow-up `link.list` count check).

## Commands

```bash
ctest --test-dir cmake-build -L conformance --output-on-failure
```

## Tests

| Test file | Target | CTest label | Cases |
|---|---|---|---|
| (data: 30, 31, 32 json) | existing runners | `integration;conformance` | 32 cases |

## Constraints

- Job ids are `<id>`; never compare against counters.
- Polling timeout 5 s; cases must finish in under 2 s each on CI.
- No network resolution: import targets use `example.com`/`example.org` literals only.

## Success criteria

1. Every job state (`queued` via accept, `running` not asserted, `succeeded`, `failed`, `cancelled`) is observed in at least one case; `busy` and `job_cancelled` appear at least once (codes feed the coverage gate).
2. Import round trip export->import is lossless for slug and target_url.
3. All cases pass on every transport that supports jobs, else carry explicit skip reasons.

## Out of scope / risks

- Durable jobs and restarts are non-goals (ADR 0002).
- Risk: flaky cancel timing; the hybrid assertion in step 3 trades strictness for stability and must be reviewed by subtask 34.
