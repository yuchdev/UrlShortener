# 02 - Contract tests for extended listing

**Parent task:** 03.0 Link and browse commands on the registry
**State:** ⬜ Not started
**Depends on:** 01
**Blocks:** 12
**Estimated size:** S
**Suggested agent:** testing-expert

## Objective

Prove that `IMetadataRepository::ListLinksPage` returns identical filtered, sorted,
paged results and totals on **every** metadata backend by adding backend-agnostic
contract tests that run through the existing `MakeMetadataHarnesses()` (in-memory,
SQLite, and Postgres when `URL_SHORTENER_TEST_POSTGRES_DSN` is set). Include the
legacy-flag and `extra_filter` cases, and make the one known SQL limitation (`deleted_at`
is not persisted) explicit in a test instead of a surprise.

## Dependencies

- Subtask 01 (`ListLinksPage`, `ApplyListQuery`, new `ListLinksQuery` fields).
- Harness: `tests/contract/metadata/metadata_contract_common.hpp` (`MetadataHarness {name, clock, repo}`,
  `BasicRequest(code, target)`); `ManualClock` from `url_shortener/core/clock.hpp`.
- Real constraints discovered: SQL stores `created_at`/`expires_at` as **epoch seconds**,
  so test records must be at least 1 s apart (the existing test uses `+ 1s`); the SQLite
  harness deletes and re-creates `url_shortener_contract.sqlite3` in the temp dir on each
  call to `MakeMetadataHarnesses()`, so each test file builds its harnesses once.

## Files affected

| Path | Action | What changes |
|---|---|---|
| `tests/contract/metadata/09_list_page_text_and_sort.cpp` | create | Text filter, sort fields/orders, tie-break. |
| `tests/contract/metadata/10_list_page_window_and_total.cpp` | create | Offset/limit/total boundaries, scan behavior with many rows. |
| `tests/contract/metadata/11_list_page_flags_and_predicate.cpp` | create | `include_inactive`, `owner`, `extra_filter` on all backends; `include_deleted` in-memory only. |
| `sources.cmake` | modify | Append the three files to `CONTRACT_METADATA_SOURCES` (currently ends at `08_concurrent_create_one_wins.cpp`). |

No `CMakeLists.txt` change: `register_labeled_cpp_tests(CONTRACT_METADATA_SOURCES "contract")`
already exists.

## Classes and functions

No production code. Helpers (file-local, in an anonymous namespace per test, or added to
`metadata_contract_common.hpp` if used by two files):

```cpp
// Creates `count` links "c000".."cNNN" (3-char minimum from CreateLinkRequest::isValid),
// created_at = clock->now() + i seconds.
inline void SeedLinks(MetadataHarness& h, int count);

// Runs ListLinksPage and BOOST_REQUIREs success.
inline LinkPage Page(MetadataHarness& h, const ListLinksQuery& q);
```

Real symbols used: `IMetadataRepository::CreateLink(const CreateLinkRequest&, const std::string& id, const std::chrono::system_clock::time_point& now, RepoError*)`,
`UpdateLink(const LinkRecord&, RepoError*)`, `GetByShortCode`, `CreateLinkRequest{short_code, target_url, expires_at, owner, attributes, is_active}`.

## Implementation steps

1. Add the three test files following the `BOOST_TEST_MODULE ...` + `BOOST_AUTO_TEST_CASE` style of
   `06_list_filtering_pagination.cpp`; loop over `MakeMetadataHarnesses()` and put the
   harness name in every `BOOST_TEST` message (`<< h.name`).
2. 09: seed `abc`, `Abd`, `xyz` with distinct targets; assert `text="AB"` matches the first two on every
   backend; `text` matching only the target URL; sort `short_code` asc/desc; sort `created_at` desc;
   equal `created_at` (create two records with the same `now`) ordered by `short_code`.
3. 10: seed 7 links; `limit=3` pages 0/3/6 give 3/3/1 items, `total == 7` every page; `offset=7`
   and `offset=100` -> 0 items, `total == 7`; `limit=0` -> 0 items, `total == 7`.
4. 11: seed an active link, an inactive one (`is_active=false`) and two owners. Assert `include_inactive=false`
   drops the inactive one on every backend; `owner` narrows; an `extra_filter` that keeps only records whose
   `attributes` contains a given key/value returns exactly those (the lister's tag/campaign/state predicates rely on
   this) and `total` counts after the predicate. For `include_deleted`: `if (h.name != "inmemory") continue;` with a
   comment pointing to Task 02.0 (SQL does not persist `LinkRecord::deleted_at`; 02.0 stores deletion in the
   `lnk_deleted` attribute and the lister filters it via the predicate); on in-memory create, then `UpdateLink`
   with `deleted_at` set and assert `include_deleted` true/false.
5. Append the files to `CONTRACT_METADATA_SOURCES`; build and run.
6. If any SQL harness fails because of data not backend logic, fix the test, never the helper.

## Commands

```bash
cmake --build cmake-build --target metadata__09_list_page_text_and_sort metadata__10_list_page_window_and_total metadata__11_list_page_flags_and_predicate
ctest --test-dir cmake-build -R "^metadata__(09|10|11)_" --output-on-failure
# whole contract label, to prove no cross-test interference on the shared sqlite file
ctest --test-dir cmake-build -L contract --output-on-failure
# Postgres leg (PowerShell):  $env:URL_SHORTENER_TEST_POSTGRES_DSN = "host=localhost dbname=urls user=u password=p"
```

Windows/multi-config: add `--config Debug` and `-C Debug`. Contract tests run serially
(they share `url_shortener_contract.sqlite3`); do not pass `-j` to `ctest` for the
`contract` label.

## Tests

| Test file | Target | Label | Cases |
|---|---|---|---|
| `tests/contract/metadata/09_list_page_text_and_sort.cpp` | `metadata__09_list_page_text_and_sort` | contract | case-insensitive text on slug; text on URL only; no match -> empty & total 0; sort slug asc/desc; created desc; created tie -> slug asc. |
| `tests/contract/metadata/10_list_page_window_and_total.cpp` | `metadata__10_list_page_window_and_total` | contract | pages of 3 over 7 rows; `total` stable across pages; `offset == total`; `offset > total`; `limit == 0`; single-row repository. |
| `tests/contract/metadata/11_list_page_flags_and_predicate.cpp` | `metadata__11_list_page_flags_and_predicate` | contract | `include_inactive` true/false; `owner`; `extra_filter` on an attribute with `total` counted after it; predicate rejecting everything; `include_deleted` (in-memory only, named skip on SQL). |

Manual check: with Postgres available, set `URL_SHORTENER_TEST_POSTGRES_DSN` and confirm the
harness list contains `postgres` (print `h.name` once with `BOOST_TEST_MESSAGE`).

## Constraints

- Tests exercise only the port (`IMetadataRepository`), never SQL or SOCI directly.
- Deterministic time: only `ManualClock`; no `std::chrono::system_clock::now()` in assertions.
- Do not weaken a case to make SQL pass; skip with a named reason instead.
- Repo test conventions: numeric prefix continuing the directory (`09`, `10`, `11`),
  Boost.Test, `BOOST_TEST_MODULE` defined per file.

## Success criteria

1. `ctest -R "^metadata__(09|10|11)_"` passes with harnesses `inmemory` and `sqlite`
   (and `postgres` when the DSN is set).
2. Every backend returns the same ordered slug list and `total` for each case (assert
   against literal expected vectors, not against another backend).
3. `ctest -L contract` is green (no interference with the existing eight contract tests).
4. The `include_deleted` case is skipped on SQL with a comment referencing Task 02.0.

## Out of scope / risks

- If a later change persists `deleted_at` in SQL (real column), remove the in-memory-only guard in `11_...` in that change, not here.
- Large-scan behavior (100000 rows) is covered by the fake in subtask 01, not here (too slow for SQLite CI).
