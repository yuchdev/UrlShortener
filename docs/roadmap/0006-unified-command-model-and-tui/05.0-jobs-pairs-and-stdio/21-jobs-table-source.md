# 21 - `jobs` table source for `db.tables` / `db.rows`

**Parent task:** 05.0 Jobs, pair import/export and stdio transport
**State:** ⬜ Not started
**Depends on:** 05.0/02 (`JobRunner::list`), 03.0/15 (`ITableSource`, `TableCatalog`)
**Blocks:** 20
**Estimated size:** S
**Suggested agent:** cpp-expert (review: security-auditor, C6)

## Objective

Provide the third allowlisted logical table of milestone C6 / decision Q7,
`jobs`, as an `app::ITableSource` over the process-wide `JobRunner`, so
`db.tables` lists `links`, `click_events` and `jobs` (catalog 3.2, ADR 0003 D6)
and `db.rows {"table":"jobs"}` pages the jobs of the current process. The
source exposes job *metadata only*: never `args`, never `result` (an export
result can carry the whole link table inline) and never error messages
(they may contain argument names chosen by a caller).

## Dependencies

- `ITableSource`, `TableColumn`, `TableQuery`, `TableRowsPage`, `TableCatalog`
  (Task 03.0 subtask 15, `namespace url_shortener::app`).
- `JobRunner::list(std::optional<JobState>, offset, limit) -> JobPage {items,total}`
  (newest first) and `JobRecord` fields `id, command, state, progress{done,total},
  failure{code,message}, created_at, started_at, finished_at` (subtasks 01-02).
- Timestamp formatting with `url_shortener::formatTimestamp` (`core/utils.h`;
  include it from the `.cpp` only, it pulls in Beast).

## Files affected

| Path | Action | What changes |
|---|---|---|
| `include/url_shortener/commands/jobs_table_source.hpp` | create | `make_jobs_table_source` |
| `src/commands/jobs_table_source.cpp` | create | implementation; add to `COMMON_CPP_SOURCES` |
| `tests/unit/commands/65_jobs_table_source.cpp` | create | tests |
| `sources.cmake` | modify | `COMMON_CPP_SOURCES` += .cpp; `COMMANDS_UNIT_SOURCES` += test |

## Classes and functions

```cpp
namespace url_shortener::commands {
/// Read-only `jobs` table over @p runner (non-owning; the runner is process-wide, subtask 20).
std::shared_ptr<app::ITableSource> make_jobs_table_source(const JobRunner& runner);
}
```

Columns (fixed, literal constants, in this order; S = sortable, Q = searchable):

| column | type | flags | source |
|---|---|---|---|
| `id` | string | S Q | `JobRecord::id` |
| `command` | string | S Q | `JobRecord::command` |
| `state` | string | S | `job_state_name(state)` |
| `done` | int | | `progress.done` |
| `total` | int | | `progress.total` |
| `error_code` | string | | `failure->code` or empty (never the message) |
| `created_at` | timestamp | S | RFC 3339 UTC |
| `finished_at` | timestamp | S | RFC 3339 UTC or empty |

`writable() == false`. `row_count()` = `runner.list(nullopt, 0, 1).total`.
`rows(query)`: take a snapshot `runner.list(nullopt, 0, max_retained_jobs)`,
filter by `q` (case-insensitive substring of `id` or `command`), sort by the
requested sortable column (default `created_at` descending), page by
`offset`/`limit`, serialize each row with `jsonString`-escaped values. The
snapshot is bounded by the store's retention (`max_retained_jobs`, default 256).

## Implementation steps

1. Header and source; the column list is a `static const std::vector<TableColumn>`.
2. Tests with a real `JobRunner` (`ManualClock`, `worker_count = 1`) and gated
   job bodies to produce queued/running/succeeded/failed/cancelled records.
3. Register the test; build; run.

## Commands

```powershell
cmake --build cmake-build --target commands__65_jobs_table_source
ctest --test-dir cmake-build -R "^commands__65_jobs_table_source$" --output-on-failure
```

(Multi-config: `--config Debug` / `-C Debug`.)

## Tests

| Test file | Target | Label | Cases |
|---|---|---|---|
| `tests/unit/commands/65_jobs_table_source.cpp` | `commands__65_jobs_table_source` | unit | below |

- Metadata: `name() == "jobs"`, `writable() == false`, exact column list and order.
- Rows: one row per job, key set equals the column names exactly; a job whose
  `args` contain a marker string and whose `result_json` contains another marker
  never shows either marker in any row; a failed job shows `error_code` but not
  its message.
- `q` matches `id` and `command`; sort by `created_at`/`state`/`command`
  ascending and descending; unknown sort is rejected by `db.rows` (registry), not here.
- Paging: `offset`/`limit` windows and `total`; empty runner -> `total 0`, no rows.
- Through `db.rows` with a catalog holding `links`, `click_events`, `jobs`:
  `{"table":"jobs"}` works for `ControlSet::User`; `db.tables` lists exactly the
  three names in that order.

## Constraints

- C6: column list is literal; no `args`, `args_digest`, `result`, or failure
  message; values JSON-escaped.
- C7: read-only source; the `db.*` permission is `read`.
- No header under `include/url_shortener/commands/` includes Beast or storage headers (01.0/13).
- C++17; naming per `.clang-tidy`.

## Success criteria

1. `commands__65_jobs_table_source` passes.
2. A reviewer can see from `jobs_table_source.cpp` alone that no job argument or
   result can be returned.

## Out of scope / risks

- Per-principal filtering of jobs (needed once Milestone 0002 adds users; catalog 3.4 note).
- Jobs of *other* processes are invisible (jobs are in-memory, decision Q8).
