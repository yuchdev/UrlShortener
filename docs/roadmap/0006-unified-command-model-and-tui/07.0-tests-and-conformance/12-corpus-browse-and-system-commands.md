# 12 - Corpus: browse and system commands

**Parent task:** 07.0 Tests and conformance
**State:** ⬜ Not started
**Depends on:** 01, 04; Task 03.0 (`link.list`, `db.tables`, `db.rows`, `system.info`, `command.list`)
**Blocks:** 15
**Estimated size:** M
**Suggested agent:** testing-expert

## Objective

Pin pagination, filtering, sorting, table browsing, system info and
introspection output on all transports, since the TUI and the future admin
panels (0002/0004) consume exactly these shapes.

## Dependencies

- Catalog sections 3.2 and 3.5.
- Oracle: contract tests for `ListLinks` under `tests/contract/metadata/` (list filter/ordering semantics must agree).

## Files affected

| Path | create/modify | What changes |
|---|---|---|
| `tests/conformance/commands/20_link_list.json` | create | list/filter/sort/page |
| `tests/conformance/commands/21_db_tables.json` | create | `db.tables`, `db.rows` |
| `tests/conformance/commands/22_system.json` | create | `system.info`, `command.list` |

## Classes and functions (cases)

Seed with `setup` steps of `link.create` (all steps are asserted, so seed
steps carry compact `expect` with `match: "subset"` and `result.slug` only).
Standard seed `seed7`: slugs `a`..`g` created in order with tags/campaign
variety, one disabled (`c`), one deleted (`d`).

**20_link_list.json (16 cases):** `link_list_default_active_only` (Q9 default `active`, excludes `c`,`d`); `link_list_status_all`; `link_list_status_expired` and `link_list_default_excludes_expired` (Task 03.0 subtasks 12/14 add `expired` to the `status` enum and make `active` exclude expired links; catalog 3.2 already lists it); `link_list_status_disabled`; `link_list_status_deleted`; `link_list_q_slug_substring`; `link_list_q_target_substring`; `link_list_tag_filter`; `link_list_campaign_filter`; `link_list_sort_slug_asc`; `link_list_sort_created_desc_default`; `link_list_offset_limit_pages` (page 1 and page 2 do not overlap, `total` constant); `link_list_limit_max_500_ok`; `link_list_limit_501_invalid` (`invalid_field`); `link_list_limit_zero_invalid`; `link_list_offset_negative_invalid`; `link_list_unknown_sort_invalid`. Resource form: `GET /api/v1/links?status=all&limit=2` where applicable (the route is gated: the REST shims start the server with `--command-api-enabled true`).

**21_db_tables.json (12 cases):** `db_tables_lists_allowlist` (exactly `links`, `click_events`, `jobs`; `writable` flags; `rows` ints; columns list); `db_tables_no_auth_tables` (asserts the `tables[*].name` set is exactly the allowlist - no `users`, `sessions`, `auth_audit_log`, `tokens`); `db_rows_links_first_page`; `db_rows_links_sort_filter`; `db_rows_click_events_empty`; `db_rows_jobs_after_import` (added only after 05.0 lands; do not commit it earlier); `db_rows_unknown_table_not_found` (`not_found`); `db_rows_auth_table_not_found` (`users` -> `not_found`, same code as unknown: no existence oracle); `db_rows_disallowed_sort_column` (`invalid_field`); `db_rows_limit_bounds`; `db_rows_missing_table_invalid_field`; `db_rows_columns_exclude_secrets` (no column named like hash/token/salt/secret/dsn/password in `columns`; see also subtask 18).

**22_system.json (6 cases):** `system_info_shape` (`version` `<regex:.+>`, `backend` four keys, `persistent` bool, `read_only` bool); `system_info_read_only_reflects_role` (caller `user` -> `read_only: true`; transports where role is process-level only); `command_list_contains_all_commands` (`command.list` names set equals the catalog set from README section 3, checked via `subset` plus the coverage gate in 15); `command_list_descriptor_shape` (each entry has `name,summary,perm,kind,mutating,dry_run,args`; asserted on `link.create` entry via `<any>` for summary); `command_list_user_can_read` (`user` allowed); `command_list_arg_enum_present` (`link.list.status` enum values).

## Implementation steps

1. Author `seed7` as a documented JSON snippet in `tests/conformance/commands/README.md` and copy per case (no include mechanism, by design: cases are self-contained).
2. Write expectations from catalog; run on `direct`; resolve differences as defects or catalog edits (never silently).
3. For `command_list_*`, avoid snapshotting `summary` text (human prose may change): use `<any>`.

## Commands

```bash
python tools/conformance_lint.py tests/conformance/commands
ctest --test-dir cmake-build -L conformance -R "corpus" --output-on-failure
```

## Tests

| Test file | Target | CTest label | Cases |
|---|---|---|---|
| (data: 20, 21, 22 json) | existing corpus runners | `integration;conformance` | about 34 cases |

## Constraints

- Ordering assertions only where `sort`/`order` is explicit or the default is catalogued.
- Do not assert exact `rows` counts for `click_events` beyond empty (analytics is async/best-effort).
- `system.info` values for `backend` differ per run configuration: assert shape with `<regex:.+>`, not names, except the in-memory/sqlite value the harness configured (`sqlite`).

## Success criteria

1. All `link.list` args and every bound in the catalog (`limit` 1..500, `offset >= 0`) have a pass and a fail case.
2. Allowlist case fails if a fourth table is exposed.
3. Cases pass on all transports; the `jobs` rows case is added in the same PR that first has the job runtime (05.0).

## Out of scope / risks

- Postgres/Redis list semantics: covered by `ListLinks` contract tests (03.0 subtask on list ports), not repeated here.
- No `migrate`-permission command exists in Milestone 0006 (ADR 0003 rule 4 only reserves its `MIGRATE` confirmation); no case until a descriptor exists.
