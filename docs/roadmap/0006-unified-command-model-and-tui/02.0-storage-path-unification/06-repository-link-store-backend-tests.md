# 06 - Store tests on in-memory and SQLite

**Parent task:** 02.0 Storage path unification
**State:** ⬜ Not started
**Depends on:** 02.0/04 (05 for the stats case)
**Blocks:** none
**Estimated size:** M
**Suggested agent:** testing-expert

## Objective

Prove that `RepositoryLinkStore` behaves the same on the in-memory repository
and on a real SQLite file by running one shared "link store contract" against
both, including persistence across store instances (the property the CLI needs).
This is the acceptance criterion "contract suite passes through the new adapter
on in-memory and SQLite" of the original spec, written as a link-store contract
because the existing `tests/contract/metadata/*` suite targets
`::IMetadataRepository`, not `app::ILinkStore`.

## Dependencies

- 02.0/04 store (writes), 02.0/05 (`flush_stats`, used by one case).
- Existing helpers (verified): `tests/contract/metadata/metadata_contract_common.hpp`
  (`MakeMetadataHarnesses`) as the style reference; the SQLite wiring pattern in
  `tests/integration/storage/07_sqlite_service_full_flow.cpp`
  (`SqlConnectionConfig`, `SqliteSessionFactory`, `SqliteSqlDialect`,
  `SqlMetadataRowMapper`, `SqlMetadataRepository`).
- `tests/contract/metadata` is already on the include path of every test
  registered with `register_labeled_cpp_tests` (macro adds it, `CMakeLists.txt`
  line 161), so the shared header can live there.

## Files affected

| Path | Create / modify | What changes |
|---|---|---|
| `tests/contract/metadata/link_store_contract_common.hpp` | create | `run_link_store_contract(make_store, reopen_store)` shared cases + `make_sample_link()` |
| `tests/unit/storage/inmemory/16_repository_link_store_contract.cpp` | create | In-memory instantiation (label unit) |
| `tests/integration/storage/15_repository_link_store_sqlite.cpp` | create | SQLite instantiation (label integration) |
| `sources.cmake` | modify | `INMEMORY_UNIT_SOURCES` += 16; `STORAGE_INTEGRATION_SOURCES` += 15 |

No `CMakeLists.txt` change: both lists are already registered with
`register_labeled_cpp_tests` (targets `inmemory__16_repository_link_store_contract`
and `storage__15_repository_link_store_sqlite`).

## Classes and functions

Test-only:

```cpp
struct LinkStoreFixture
{
    std::function<std::unique_ptr<url_shortener::app::ILinkStore>()> make_store;     // fresh store, same backing data
    bool persistent = false;                                                          // true: data survives make_store()
    std::function<void()> cleanup;
};

void run_link_store_contract(const LinkStoreFixture& fixture);   // BOOST_TEST assertions
url_shortener::Link make_sample_link(const std::string& slug);   // all fields populated
```

In-memory fixture: one `InMemoryMetadataRepository` shared by every
`make_store()` call (models "same process"); `persistent = false`.
SQLite fixture: a temp file path (`std::filesystem::temp_directory_path() /
"15_repository_link_store_sqlite.db"`, removed before and after); each
`make_store()` builds a **new** `SqlMetadataRepository` + `RepositoryLinkStore`
over the same file (models "next process"); `persistent = true`.

Shared cases:

1. create then `findBySlug` in the same store: every `Link` field equal.
2. `findBySlug` through a **second store instance** (`persistent` only): equal.
3. duplicate slug -> `slug_conflict`; first record unchanged.
4. update `enabled=false`, tags with `.`/`&`/`=`, metadata with `.` keys,
   campaign, `expires_at`; re-read through a new instance (persistent): equal.
5. soft delete (`deleted_at` set) then restore; both survive reopen.
6. `findById` returns the link; unknown id -> `nullopt`.
7. `slugExists` true/false.
8. `recordRedirect` + `flush_stats()` then reopen: counters persisted, other
   fields untouched (the lost-update guard with a real SQL round trip).
9. 8 threads x `create` of the same slug: exactly one succeeds, others get
   `slug_conflict`.
10. 200 links created, `findById` for the last one succeeds within the scan caps.

## Implementation steps

1. Write the shared header with the cases.
2. Write the two thin instantiation files.
3. Register in `sources.cmake`; reconfigure, build, run both.
4. If SQLite hits `database is locked` in case 9, use the repository's busy
   timeout (verified to exist: `tests/unit/storage/sqlite/11_busy_timeout_configuration.cpp`);
   adjust the test, not the production code, unless the failure shows a real bug.

## Commands

```powershell
cmake -S . -B cmake-build -G Ninja
cmake --build cmake-build --target inmemory__16_repository_link_store_contract storage__15_repository_link_store_sqlite
ctest --test-dir cmake-build -R "^(inmemory__16_repository_link_store_contract|storage__15_repository_link_store_sqlite)$" --output-on-failure
```

(Multi-config: `--config Debug` / `-C Debug`. The SQLite test needs SOCI's
SQLite backend, which `FetchContent` builds automatically per CLAUDE.md.)

## Tests

| Test file | Target | Label | Cases |
|---|---|---|---|
| `tests/unit/storage/inmemory/16_repository_link_store_contract.cpp` | `inmemory__16_repository_link_store_contract` | unit | contract cases 1, 3-7, 9, 10 (non-persistent subset) |
| `tests/integration/storage/15_repository_link_store_sqlite.cpp` | `storage__15_repository_link_store_sqlite` | integration | all ten cases incl. reopen |

Manual check: run the SQLite test twice in a row (file removal makes it
re-entrant) and once with the temp file pre-created and empty.

## Constraints

- Tests use their own temp paths; never write into the repo tree or the user's
  working directory.
- No network, no Postgres/Redis (those suites stay separate; a Postgres
  instantiation can reuse the header later).
- Boost.Test only; deterministic (no `sleep`); thread test bounded to a few
  seconds.
- Contracts: C3/C4 by construction (data round trip), cache-aside invalidation
  observed through read-your-writes.

## Success criteria

1. Both targets pass.
2. The persistent cases fail if the codec drops `deleted_at` (demonstrated by
   temporarily removing `lnk_deleted` locally) - i.e. the test guards the SQL
   schema gap.
3. Case 9 shows exactly one winner on SQLite.

## Out of scope / risks

- Postgres/Redis variants (need services; follow the pattern of
  `08_postgres_service_full_flow.cpp` later).
- Performance numbers (R-2) - measured with the perf harness, not here.
- SQLite file locking under concurrent writers from multiple *processes* is
  covered in subtask 14, not here.
