# 04 - ListEvents for the SQL click-event repository

**Parent task:** 03.0 Link and browse commands on the registry
**State:** ⬜ Not started
**Depends on:** 03
**Blocks:** 16
**Estimated size:** M
**Suggested agent:** cpp-expert

## Objective

Implement `IClickEventRepository::ListEvents` in `SqlClickEventRepository` for both SQLite
and PostgreSQL (one class, `is_postgres_` switch), with filtering, a whitelisted sort,
a `COUNT(*)` total and a clamped window, using bound parameters only.

## Dependencies

- Subtask 03 (`ClickEventQuery`, `ClickEventPage`, `kMaxClickEventPageSize`, the virtual).
- Verified implementation facts (`src/storage/sql/sql_click_event_repository.cpp`):
  one `soci::session session_` guarded by `std::mutex mu_` (`std::scoped_lock lk(mu_)` in
  every method); SQLite stores `occurred_at` as `INTEGER` epoch seconds, Postgres as
  `TIMESTAMPTZ` (so writes use `to_timestamp(:oat)` and reads must use
  `CAST(EXTRACT(EPOCH FROM occurred_at) AS BIGINT)`); existing code reads numeric columns by
  `CAST(... AS TEXT)` + `std::stoll` to dodge SOCI type differences; errors are reported as
  `AnalyticsErrorCode::repository` with `ex.what()`. Table `click_events` columns:
  `event_id, occurred_at, slug, link_id, domain, status_code, referrer, user_agent, client_id_hash`.
- Postgres is optional in CI: tests skip when `URL_SHORTENER_TEST_POSTGRES_DSN` is unset.

## Files affected

| Path | Action | What changes |
|---|---|---|
| `include/url_shortener/storage/sql/sql_click_event_repository.hpp` | modify | Declare `ListEvents` override. |
| `src/storage/sql/sql_click_event_repository.cpp` | modify | Implement it (file already in `COMMON_CPP_SOURCES`). |
| `tests/integration/cpp/storage/analytics_sqlite_list_events.cpp` | create | SQLite behavior against a temp DB file. |
| `tests/integration/cpp/storage/analytics_postgresql_list_events.cpp` | create | Same cases, skipped without the DSN. |
| `sources.cmake` | modify | Append both to `ANALYTICS_INTEGRATION_SOURCES`. |

## Classes and functions

```cpp
// sql_click_event_repository.hpp
bool ListEvents(const analytics::ClickEventQuery& query,
                analytics::ClickEventPage* page,
                analytics::AnalyticsError* error = nullptr) override;
```

Implementation outline (file-local helpers in the existing anonymous namespace):

- `std::string order_clause(ClickEventSort, bool descending)` returns one of three **string
  literals** (`occurred_at`, `slug`, `status_code`) plus `ASC|DESC` and always the tie-break
  `, event_id ASC`. The column name never derives from user text.
- `std::string escape_like(const std::string&)` escapes `\`, `%`, `_` and lower-cases ASCII;
  the clause is `(lower(slug) LIKE :pat ESCAPE '\' OR lower(domain) LIKE :pat ESCAPE '\')`.
- Filters are written as `(:has_slug = 0 OR slug = :slug)`, `(:has_status = 0 OR status_code = :sc)`,
  `(:has_text = 0 OR ...)`, `(:has_from = 0 OR occurred_at >= <ts(:from_ts)>)`,
  `(:has_to = 0 OR occurred_at < <ts(:to_ts)>)` where `<ts(x)>` is `x` on SQLite and
  `to_timestamp(x)` on Postgres. All parameters are always bound (absent ones with neutral
  values and `has_* = 0`), so one SQL text per backend serves every query shape.
- Two statements: `SELECT CAST(COUNT(*) AS TEXT) ... WHERE <filters>` and the row query with
  `LIMIT :limit OFFSET :offset`. Row select list per backend:
  `event_id, <occurred_epoch> AS occurred_at, slug, link_id, domain, CAST(status_code AS TEXT) AS status_code, referrer, user_agent, client_id_hash`.
  Nullable text columns map `soci::i_null` to empty strings (the model uses `std::string`).
- Validation order: `page == nullptr` -> `validation`; `from > to` -> `validation`;
  clamp `limit` to `kMaxClickEventPageSize`; wrap SOCI work in `try/catch (const std::exception&)`
  setting `AnalyticsErrorCode::repository` (same pattern as siblings).

## Implementation steps

1. Add the helpers and the method; build for SQLite first.
2. Write the SQLite integration test with a temp-dir DB file (`std::filesystem::temp_directory_path()`),
   removing it at start and end, using `InsertBatch` to seed.
3. Duplicate the cases for Postgres behind the same DSN-skip guard as
   `analytics_postgresql_repository_roundtrip.cpp`; use unique `event_id` prefixes and
   `DeleteOlderThan` to clean up.
4. Register both tests; run.
5. Review the SQL strings: every value bound, no string concatenation of user input except the
   whitelisted column/direction literals.

## Commands

```bash
cmake --build cmake-build --target storage__analytics_sqlite_list_events storage__analytics_postgresql_list_events
ctest --test-dir cmake-build -R "^storage__analytics_(sqlite|postgresql)_list_events$" --output-on-failure
```

Postgres leg (PowerShell): `$env:URL_SHORTENER_TEST_POSTGRES_DSN = "host=localhost dbname=urls user=u password=p"`
before `ctest`. Without it the test prints `SKIPPED` and passes. The integration label also
runs Python scripts that need the full binary; use `-R` as above to avoid them.

## Tests

| Test file | Target | Label | Cases |
|---|---|---|---|
| `tests/integration/cpp/storage/analytics_sqlite_list_events.cpp` | `storage__analytics_sqlite_list_events` | integration | seed 12 events over 3 slugs/2 domains/3 status codes; each filter alone and combined; `from` inclusive / `to` exclusive boundary; text with `%` and `_` characters matches literally; sort every field both directions; tie-break by `event_id`; `offset`/`limit`/`total` incl. `offset > total`; `limit` clamp to 500 (seed 600 via batches); `limit = 0`; empty `referrer`/`user_agent` (NULL) round-trip; SQL-injection strings (`'; DROP TABLE click_events;--`) in `slug`/`text` are inert and the table survives; events survive re-opening the file. |
| `tests/integration/cpp/storage/analytics_postgresql_list_events.cpp` | `storage__analytics_postgresql_list_events` | integration | same cases; timestamp round-trip through `to_timestamp`/`EXTRACT(EPOCH ...)` is lossless to the second; skipped when the DSN is unset. |

Manual check: run the SQLite test with `--log_level=message` and confirm no `SKIPPED` line;
inspect the generated DB with `tools/sqlite_state_assert.py` if a failure needs triage.

## Constraints

- Bound parameters only; the only text interpolated into SQL are the whitelisted literals
  chosen by `switch` over enums (CLAUDE.md: treat all request-derived values as untrusted).
- Never log the DSN or event contents; error messages are `ex.what()` as in siblings.
- `ListEvents` is a management-plane read: it must not be reachable from the redirect hot path
  or `AnalyticsWorker` (grep check in success criteria).
- C++17; `lower_case` names; keep the existing 4-space style of the file.
- Holding `mu_` for the whole call is required (single shared `soci::session`); keep both
  statements inside one lock scope so `total` and `items` are consistent.

## Success criteria

1. `ctest -R "^storage__analytics_sqlite_list_events$"` passes; with a DSN, the Postgres test passes too.
2. The injection case leaves `click_events` queryable and returns no rows for hostile `slug`.
3. `limit` of 100000 returns at most 500 items.
4. `git grep -n "ListEvents" -- src/analytics src/http` returns nothing.

## Out of scope / risks

- Postgres time buckets are unsupported in the existing `GetAggregateStats` (SQLite only); unrelated here.
- `LIKE` on a large table is a sequential scan; there is no index on `domain`/`slug` text search.
  Acceptable for an operator tool; revisit if `db.rows click_events` is exposed to web clients.
- `occurred_at` second resolution on both backends: sub-second ordering is not preserved
  (tie-break `event_id` keeps results deterministic).
