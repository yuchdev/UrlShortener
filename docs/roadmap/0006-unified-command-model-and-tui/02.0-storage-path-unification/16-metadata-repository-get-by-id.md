# 16 - IMetadataRepository::GetById (indexed lookup by id)

**Parent task:** 02.0 Storage path unification
**State:** ⬜ Not started
**Depends on:** 02.0/03
**Blocks:** 02.0/08, 02.0/11
**Estimated size:** M
**Suggested agent:** cpp-expert (review: security-auditor for T-36; feature-reviewer for the port change)

## Objective

Add an indexed single-record lookup `GetById` to the pluggable storage port
`::IMetadataRepository`, implement it on every adapter with an indexed query
(no table scan), and switch `app::RepositoryLinkStore::findById` to call it so
the O(n) `ListLinks` paging scan introduced in subtask 03 is removed entirely.

**Why now (security):** the security review of subtask 03 re-rated finding
**T-36** from LOW to **HIGH** once the call chain to `findById` became clear.
`RepositoryLinkStore::findById` (`src/app/repository_link_store.cpp`, commit
`e35333a`) pages `ListLinks` up to `id_scan_max = 100000` records per call,
decoding each. Once subtasks 02.0/08 and 02.0/11 land, that method is reachable
from the **unauthenticated** `GET /api/v1/links/id/{id}` route
(`src/http/router_builder.cpp` -> `handleGetLinkById` ->
`LinkCommandService::GetLink` -> `ILinkStore::findById`), so an attacker can
drive up to 100k-row / 500-query scans per request against a large backend - a
DoS amplifier and a regression from the legacy store's O(1) hash lookup. The
fix restores O(1)/O(log n) lookup behavior on every backend before those
routes go live. See
[docs/security/0006-02.0-storage-path-unification-threat-model.md](/docs/security/0006-02.0-storage-path-unification-threat-model.md)
(T-36).

This decision was ratified with the user on 2026-11-14 (recorded in
[status.md](/docs/roadmap/0006-unified-command-model-and-tui/status.md)
"Notes & decisions").

## Dependencies

- **02.0/03** `RepositoryLinkStore` read side and its `findById` scan + the
  `FakeMetadataRepository` test double (`tests/unit/app/21_...`) that this
  subtask amends.
- Verified ports / types (global namespace, qualify as `::...`):
  - `::IMetadataRepository` (`include/url_shortener/storage/i_metadata_repository.hpp`)
    - `GetById` is added here, mirroring `GetByShortCode`.
  - `::LinkRecord` (`storage/models/link_record.hpp`): the `id` field is the
    caller-assigned stable identifier.
  - `SqlDialect` (`storage/sql/sql_dialect.hpp`), `ISqlSession` /
    `ISqlSessionFactory` + `SqlRow` (`storage/sql/i_sql_session_factory.hpp`),
    `SqlMetadataRepository` (`storage/sql/sql_metadata_repository.hpp`),
    `SqlMetadataRowMapper` (`storage/sql/sql_metadata_row_mapper.hpp`).
  - `InMemoryMetadataRepository`
    (`storage/inmemory/in_memory_metadata_repository.hpp`).

### Verified schema reality (drives the index work)

- In **both** backends the `id` is written to a dedicated `id` column
  (`id TEXT NOT NULL` for SQLite, `id VARCHAR NOT NULL` for PostgreSQL) by
  `InsertSql()` / `ToCreateRow`, **and** redundantly packed into
  `attributes_json`. `SqlMetadataRowMapper::FromRow` decodes `id` from
  `attributes_json` (falling back to the `id` column). Both copies are written
  from the same source, so querying the physical `id` column is correct.
- **`short_code` is the `PRIMARY KEY` in both schemas; the `id` column is
  neither indexed nor unique.** A `WHERE id = :id` query therefore scans the
  table today. This subtask adds a non-unique secondary index on `id` so the
  lookup is O(log n). (Uniqueness of `id` is a caller invariant - see the
  `CreateLink` contract - not enforced by the DB today; making the index
  `UNIQUE` is out of scope because it risks failing bootstrap on pre-existing
  rows and is a behavioral change beyond this fix. `GetById` therefore uses
  `LIMIT 1`.)

## Files affected

| Path | Create / modify | What changes |
|---|---|---|
| `include/url_shortener/storage/i_metadata_repository.hpp` | modify | Declare `GetById` (pure virtual, `const`), with full Doxygen per the `bool`/`*error` convention. |
| `include/url_shortener/storage/inmemory/in_memory_metadata_repository.hpp` | modify | Declare `GetById` override; add `by_id_` secondary index member. |
| `src/storage/inmemory/in_memory_metadata_repository.cpp` | modify | Implement `GetById`; maintain `by_id_` in `CreateLink`/`UpdateLink`/`DeleteLink`. |
| `include/url_shortener/storage/sql/sql_dialect.hpp` | modify | Declare `SelectByIdSql()` and an index-DDL accessor (see steps). |
| `src/storage/sqlite/sqlite_sql_dialect.cpp` + header | modify | Implement `SelectByIdSql()`; add `CREATE INDEX IF NOT EXISTS idx_links_id ON links(id)` to the bootstrap sequence. |
| `src/storage/postgres/postgres_sql_dialect.cpp` + header | modify | Implement `SelectByIdSql()`; add the same index to `BootstrapSchemaSql`'s sequence. |
| `include/url_shortener/storage/sql/i_sql_session_factory.hpp` | modify | Declare `ISqlSession::SelectById(sql, id, error)`. |
| `src/storage/sqlite/sqlite_session_factory.cpp` | modify | Implement `SelectById` (bind `:id`, map one row). |
| `src/storage/postgres/postgres_session_factory.cpp` | modify | Implement `SelectById`. |
| `include/url_shortener/storage/sql/sql_metadata_repository.hpp` + `src/.../sql_metadata_repository.cpp` | modify | Implement `SqlMetadataRepository::GetById` (bootstrap, open session, `SelectById`, `FromRow`). |
| `db/migrations/postgres/005_index_links_id.up.sql` | create | `CREATE INDEX IF NOT EXISTS idx_links_id ON links(id);` |
| `db/migrations/postgres/005_index_links_id.down.sql` | create | `DROP INDEX IF EXISTS idx_links_id;` |
| `include/url_shortener/app/repository_link_store.hpp` | modify | Remove `id_scan_page` / `id_scan_max` from `RepositoryLinkStoreOptions` (now dead). |
| `src/app/repository_link_store.cpp` | modify | Replace the `findById` scan with a single `m_repo_->GetById(id, &err)` + decode + L1 set. |
| `tests/unit/app/21_repository_link_store_reads.cpp` | modify | Give `FakeMetadataRepository` a `GetById`; rewrite the `findById` scan tests (see "Changes to subtask 03"). |
| `tests/unit/core/05_interface_smoke_compile.cpp` | modify | Add `GetById` to `DummyRepo` so the port stays subclassable. |
| `tests/contract/metadata/09_get_by_id.cpp` | create | Backend-agnostic `GetById` contract cases. |
| `sources.cmake` | modify | `CONTRACT_METADATA_SOURCES` += `tests/contract/metadata/09_get_by_id.cpp`. |

No new `.cpp` in `COMMON_CPP_SOURCES` beyond edits (no new source files on the
product side); the new test file goes in `CONTRACT_METADATA_SOURCES` so it runs
against in-memory + SQLite (+ PostgreSQL when `URL_SHORTENER_TEST_POSTGRES_DSN`
is set) through `MakeMetadataHarnesses()`.

## Port contract

Add to `::IMetadataRepository`, immediately after `GetByShortCode` (same shape,
same `const`, same out-param convention):

```cpp
/**
 * @brief Looks up a record by its stable caller-assigned id.
 *
 * Returns the record regardless of lifecycle status: an inactive
 * (`is_active == false`) or soft-deleted record is still returned, so
 * admin paths (`link get --id`) can inspect disabled/deleted links. This
 * mirrors GetByShortCode, which also does not filter by status.
 *
 * @param id Stable identifier to look up (the `LinkRecord::id` assigned
 * at CreateLink). An empty id never matches and returns `std::nullopt`
 * with `error` untouched.
 * @param[out] error If non-null and a backend error occurs (not a plain
 * not-found), receives the failure reason; left untouched on success and
 * when the id simply does not exist.
 * @return The matching `LinkRecord` if found; `std::nullopt` if the id
 * does not exist or a backend error occurred (callers distinguish the two
 * by inspecting `error`).
 */
virtual std::optional<LinkRecord> GetById(
    const std::string& id, RepoError* error = nullptr) const = 0;
```

Semantics (identical error model to `GetByShortCode`):

- **Absent** -> `std::nullopt`, `error` left untouched (not-found is not a
  backend error).
- **Inactive / soft-deleted present record** -> **returned** (no status filter;
  `findById` already includes these). Note: the SQL mapper does not persist
  `deleted_at`, so at the port/`LinkRecord` level "soft-deleted" is represented
  by `is_active == false` (which does round-trip); the `deleted_at` attribute is
  app-codec-packed and is exercised at the store level, not in the port contract.
- **Backend error** -> `std::nullopt`, `error` set to the mapped `RepoError`.
- **Empty id** -> `std::nullopt`, `error` untouched (adapters may short-circuit).

## Implementations

### In-memory (`InMemoryMetadataRepository`)

- Add `std::unordered_map<std::string, std::string> by_id_;` (`id -> short_code`),
  guarded by the existing `mutex_`.
- Maintain it consistently:
  - `CreateLink`: on success insert `by_id_[id] = short_code`.
  - `UpdateLink`: id is stable and short_code is the key, so normally a no-op;
    to stay robust, re-assert `by_id_[record.id] = record.short_code` and erase
    any stale `id` entry that pointed at this short_code.
  - `DeleteLink`: read the record's `id` from `by_code_` before erasing, then
    erase the `by_id_` entry (idempotent when absent).
- `GetById`: `shared_lock`, look up `by_id_`, then `by_code_`; return the record
  or `std::nullopt`. Always writes `RepoError::none` to `*error` (matching the
  existing `Exists`/`GetByShortCode` convenience in this adapter).

### SQL layer (SQLite + PostgreSQL)

- `SqlDialect::SelectByIdSql()`: identical column list to
  `SelectByShortCodeSql()` but `WHERE id = :id ... LIMIT 1`.
- `ISqlSession::SelectById(const std::string& sql, const std::string& id,
  RepoError* error)` returning `std::optional<SqlRow>`, implemented in the
  SQLite and PostgreSQL session factories exactly like `SelectByShortCode`
  (bind `:id`, map `not_found` vs. exception error, return one row).
- `SqlMetadataRepository::GetById`: `EnsureBootstrapped`, open a session via the
  factory, call `SelectById(dialect_->SelectByIdSql(), id, &error)`, map with
  `mapper_->FromRow`. On an empty id return `std::nullopt` without a query.
  Follow `GetByShortCode` for session-open and mapping-failure error handling.

### Schema index (each backend's own mechanism)

- **SQLite (bootstrap-only backend):** add
  `CREATE INDEX IF NOT EXISTS idx_links_id ON links(id);`. Because
  `ISqlSession::Bootstrap` runs one statement per `session_ << sql` (SOCI/SQLite
  executes a single statement), the index DDL **must be issued as a separate
  statement**, not concatenated onto `BootstrapSchemaSql`. Recommended: add a
  `SqlDialect` accessor (e.g. `std::string CreateIdIndexSql()`) and have
  `SqlMetadataRepository::EnsureBootstrapped` run it with a second `Bootstrap`
  call after the table DDL. Both are `IF NOT EXISTS`, so new and existing DBs
  converge idempotently. (cpp-expert picks the exact accessor shape; the
  constraint is "one statement per Bootstrap call".)
- **PostgreSQL (bootstrap + migration):** the live `SqlMetadataRepository`
  bootstrap path also runs the index DDL (same accessor as above) so
  non-migration deployments are covered; additionally add numbered migration
  `005_index_links_id` (up: create index; down: drop index) for
  `PostgresMigrationRunner`-managed schemas. Both idempotent. (Existing
  migrations are 001, 002, 004; 005 is the next free number.)

Index name `idx_links_id`, non-unique btree on `links(id)` in both backends.

## Changes to subtask 03 (findById: scan -> GetById)

**Decision: remove the scan and the options.** `RepositoryLinkStore::findById`
switches from the `ListLinks` paging loop to a single `GetById` call; the
`id_scan_page` and `id_scan_max` fields are **removed** from
`RepositoryLinkStoreOptions` rather than left unused, eliminating the T-36 scan
surface outright. The decode + L1-set behavior is unchanged.

New `findById` body (described, not implemented):

- Empty id -> `std::nullopt` (keep the existing short-circuit; no repo call).
- `::RepoError err = none; auto record = m_repo_->GetById(id, &err);`
- On backend error: log `link_store_read_failed` with `{id, error-name}` only
  (reuse the existing helper), return `std::nullopt`.
- On absent: `std::nullopt` (no log).
- On present: `decode(record, record.short_code)`; on success
  `m_l1_->set(link->slug, *link)`; return the decoded link (or `std::nullopt` on
  codec failure, already logged by `decode`).

Amendments to `tests/unit/app/21_repository_link_store_reads.cpp`:

- `FakeMetadataRepository` gains a `GetById(id, error)` that honors an injectable
  `get_by_id_error` and increments a `get_by_id_calls` counter, searching
  `records` by `LinkRecord::id` (returning inactive/soft-deleted records too).
- **`find_by_id_paged_scan`** and **`find_by_id_stops_at_scan_max`** are
  removed (they assert the deleted scan behavior / `list_calls`). Replace with:
  - `find_by_id_found` - `GetById` returns the record, `get_by_id_calls == 1`,
    and a match is written to L1 (second lookup of its slug is an L1 hit).
  - `find_by_id_absent` - `std::nullopt`.
  - `find_by_id_returns_inactive_record` - an `is_active == false` record is
    returned (status not filtered).
  - `find_by_id_backend_error_is_nullopt` - injected error -> `std::nullopt`,
    nothing cached.
- **`find_by_id_empty_id_is_nullopt`** stays but now asserts `get_by_id_calls`
  (not `list_calls`) stays 0.
- The subtask 03 spec's `findById` "Read semantics" bullet, the
  `RepositoryLinkStoreOptions` listing, the `findById` test row, and the
  "Out of scope / risks" bullet ("add `GetById` ... if admin usage shows it
  matters") are superseded by this subtask; spec 03 is annotated with a pointer
  to 16 (header `Blocks` already updated). No change to `findBySlug`,
  `slugExists`, `invalidateCache`, or the write stubs.

### Consumers of findById

`ILinkStore::findById` is consumed by `LinkCommandService::GetLink` (subtask
08/the REST `GET /api/v1/links/id/{id}` and CLI `link get --id` paths) and any
later `--id` lookups; all go through the store, so switching the store's
implementation is transparent to them (C3: REST bodies/statuses unchanged).

## Tests

| Test | Target | Label | Cases |
|---|---|---|---|
| `tests/contract/metadata/09_get_by_id.cpp` | `metadata__09_get_by_id` | contract | below (runs on every harness) |
| `tests/unit/app/21_repository_link_store_reads.cpp` | `app__21_repository_link_store_reads` | unit | amended `findById` cases (above) |
| `tests/unit/core/05_interface_smoke_compile.cpp` | `core__05_interface_smoke_compile` | unit | `DummyRepo` gains `GetById` (compile-only) |

Contract cases (each loops over `MakeMetadataHarnesses()` so every backend
proves it):

- **found** - `CreateLink(id1)`, `GetById("id1")` returns a record whose
  `short_code`/`target_url` match.
- **absent** - `GetById("nope")` returns `std::nullopt`; `error` untouched/`none`.
- **inactive (soft-deleted proxy)** - create, `UpdateLink` with
  `is_active = false`, `GetById` still returns it (status not filtered).
- **after update** - create, `UpdateLink` changing `target_url`, `GetById`
  returns the new target (and same id).
- **after delete** - create, `DeleteLink(short_code)`, `GetById` returns
  `std::nullopt`.
- (optional) **empty id** - `GetById("")` returns `std::nullopt`.

## Constraints

- Redirect fast path (C10) is untouched: `findById` is admin-only and never on
  the redirect path; this subtask only changes which query answers it.
- Error convention (CLAUDE.md "Storage: ports and adapters"): `bool`/`optional`
  + `RepoError*` out-param; never throw for `not_found`/absent.
- No new JSON library; no change to the attribute-packing codec.
- Log only `id` + error-name on a backend error; never target URLs, tags, or
  attributes (threat model privacy rule, consistent with subtask 03).
- Qualify global types (`::IMetadataRepository`, `::RepoError`, `::LinkRecord`).
- C++17; `m_`-prefixed private members; full Doxygen on the new port method,
  the dialect/session methods, and the new adapter members.
- Index DDL is `IF NOT EXISTS` and must not widen file permissions (T-30) or run
  a table scan to build on an empty/new DB.

## Success criteria

1. `GetById` exists on `::IMetadataRepository` and every adapter; the project
   and all tests build (both `DummyRepo` and `FakeMetadataRepository` compile).
2. `metadata__09_get_by_id` passes on in-memory and SQLite (and PostgreSQL when
   the DSN is set).
3. `app__21_repository_link_store_reads` passes with the scan tests replaced;
   `findById` performs exactly one `GetById` call (no `ListLinks`).
4. `RepositoryLinkStoreOptions` no longer carries `id_scan_page`/`id_scan_max`;
   a grep shows no remaining `ListLinks`-based id scan.
5. The SQLite bootstrap and the PostgreSQL bootstrap + migration `005` both
   create `idx_links_id`; an `EXPLAIN`/query-plan spot check (or the adapter
   integration test in 02.0/06) shows the id lookup uses the index, not a scan.
6. Full `ctest -L "unit|contract|integration"` shows no new failures; REST/CLI
   `GET by id` behavior is unchanged (C3).

## Security

Resolves **T-36** (findById bounded full scan -> DoS amplifier on the
unauthenticated id route). After this subtask the id lookup is O(log n) on
persistent backends and O(1) in-memory, matching the legacy store. Hand the
"indexed, not scanned" assertion and the contract cases to `testing-expert`;
hand the HIGH re-rating closure note to `security-auditor` at Task close.

## ADR note

No Accepted ADR enumerates the `::IMetadataRepository` method set as a closed
contract - ADR 0001 references the port by name only (`docs/adr/0001-...:68,135`)
and does not fix its method list, so adding `GetById` does **not** conflict with
an Accepted ADR and needs no ADR amendment. The informal port-method list in
`CLAUDE.md` / `ARCHITECTURE.md` ("CreateLink, GetByShortCode, UpdateLink,
DeleteLink, ListLinks, Exists") and `docs/storage/how-to-add-adapter.md` should
gain `GetById`; that is a docs-sync edit (Task 02.0/15), not an ADR change. If a
future reviewer considers the port list ADR-governed, flag it and run
`/adr-write` for an amendment - do not edit the ADR as part of this subtask.
