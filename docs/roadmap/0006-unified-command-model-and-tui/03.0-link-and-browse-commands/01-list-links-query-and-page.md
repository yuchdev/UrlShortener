# 01 - ListLinksQuery extension, eval helper and ListLinksPage

**Parent task:** 03.0 Link and browse commands on the registry
**State:** ⬜ Not started
**Depends on:** none (storage layer only; does not need 01.0 or 02.0)
**Blocks:** 02, 12, 05.0/10
**Estimated size:** M
**Suggested agent:** cpp-expert

## Objective

Give the metadata port the ability to filter by free text and an application predicate, sort, page
**and report a total** without touching any backend's SQL. A single pure function evaluates the extended
query over candidate `LinkRecord`s and a new non-pure `IMetadataRepository::ListLinksPage` uses it, so
every backend (and every future one) behaves identically by construction. Lifecycle state, tag and campaign
filters are deliberately **not** repository concepts (see Dependencies); they arrive through the predicate.

## Dependencies

- `ListLinksQuery` and `IMetadataRepository` exist (verified):
  `include/url_shortener/storage/models/list_links_query.hpp`, `include/url_shortener/storage/i_metadata_repository.hpp`.
  `LinkRecord` has `id, short_code, target_url, created_at, updated_at, expires_at, deleted_at, owner, attributes, is_active, version`.
- Existing behavior to preserve: `ListLinks` default order is `created_at` ascending; `offset >= size`
  returns an empty vector; defaults `include_inactive=true`, `include_deleted=false`, `limit=100`
  (tests `tests/contract/metadata/06_list_filtering_pagination.cpp`,
  `tests/unit/storage/inmemory/07_list_filtering_and_pagination.cpp`).
- **Why state/tag/campaign are not repository filters (verified against Task 02.0):** the SQL backends do not persist
  `deleted_at`; [02.0/02](/docs/roadmap/0006-unified-command-model-and-tui/02.0-storage-path-unification/02-link-record-codec.md)
  packs deleted/enabled/tags/campaign into `attributes` (`lnk_deleted`, `lnk_enabled`, `lnk_tags`, `lnk_campaign`) and sets
  `is_active = enabled && !deleted`. So at record level "disabled" and "deleted" are indistinguishable; only the codec knows.
  The app-layer lister (subtask 12) decodes each candidate and applies `resolveLinkStatus`, tag and campaign through
  `extra_filter`.
- `SqlMetadataRepository::ListLinks` forwards only `owner`, `include_inactive`, `limit`, `offset` to `ISqlSession::List`;
  that signature has four implementations (`SqliteSession`, `PostgresSession`, stubs in
  `tests/unit/storage/sql/05_...` and `tests/unit/storage/psql/psql_test_common.hpp`) and is therefore left unchanged. The new fields are
  honored **only** by `ListLinksPage`.

## Files affected

| Path | Action | What changes |
|---|---|---|
| `include/url_shortener/storage/models/list_links_query.hpp` | modify | Add `LinkSortField`, `SortOrder` and the new fields (defaults preserve today's behavior). |
| `include/url_shortener/storage/list_links_eval.hpp` | create | `LinkPage`, `kListScanLimit`, `LinkRecordMatches`, `ApplyListQuery`. |
| `src/storage/list_links_eval.cpp` | create | Implementations plus the out-of-line default `IMetadataRepository::ListLinksPage`. Add to `COMMON_CPP_SOURCES` in `sources.cmake`. |
| `include/url_shortener/storage/i_metadata_repository.hpp` | modify | Declare non-pure `virtual bool ListLinksPage(...) const;`. |
| `tests/unit/storage/listing/01_list_query_eval.cpp` | create | Pure evaluation tests. |
| `tests/unit/storage/listing/02_list_links_page_default_impl.cpp` | create | Default `ListLinksPage` over a fake repository incl. scan-cap overflow. |
| `sources.cmake` | modify | New list `LISTING_UNIT_SOURCES`; `src/storage/list_links_eval.cpp` in `COMMON_CPP_SOURCES`. |
| `CMakeLists.txt` | modify | One line: `register_labeled_cpp_tests(LISTING_UNIT_SOURCES "unit")` next to the other registrations (~lines 173-192). |

## Classes and functions

Global namespace, matching the sibling storage headers.

```cpp
// list_links_query.hpp (additions; existing fields untouched)
struct LinkRecord;                        // forward declaration

enum class LinkSortField { created_at, updated_at, short_code };
enum class SortOrder     { asc, desc };

struct ListLinksQuery {
    // existing: owner, include_inactive, include_deleted, offset, limit
    std::optional<std::string> text;                 // case-insensitive (ASCII) substring of short_code OR target_url
    LinkSortField sort_by = LinkSortField::created_at;
    SortOrder order = SortOrder::asc;                // asc keeps today's behavior
    std::function<bool(const LinkRecord&)> extra_filter;   // optional app-layer predicate; must not throw
};
```

```cpp
// list_links_eval.hpp
struct LinkPage {
    std::vector<LinkRecord> items;   // the requested window
    std::size_t total = 0;           // matches before offset/limit
};

constexpr std::size_t kListScanLimit = 100000;

/// owner, include_inactive, include_deleted (legacy flags), text, extra_filter.
bool LinkRecordMatches(const LinkRecord& record, const ListLinksQuery& query);

/// Filter -> stable sort (tie-break short_code ascending) -> count -> window.
LinkPage ApplyListQuery(std::vector<LinkRecord> candidates, const ListLinksQuery& query);

// i_metadata_repository.hpp (non-pure; default implemented in list_links_eval.cpp)
virtual bool ListLinksPage(const ListLinksQuery& query, LinkPage* page,
                           RepoError* error = nullptr) const;
```

Rules (Doxygen on each):

- Legacy flags keep their meaning: `include_inactive == false` drops `!is_active`; `include_deleted == false` drops records with `deleted_at`.
- Default `ListLinksPage`: build `base = query` with `offset=0`, `limit=kListScanLimit`; call `ListLinks(base, error)`. Callers that need
  inactive/deleted records set the flags accordingly; the default implementation does not override them. If the result size equals
  `kListScanLimit`, set `*error = RepoError::permanent_failure`, return `false` (no silent truncation). On a `ListLinks` error return
  `false`. Otherwise `*page = ApplyListQuery(...)` (which re-applies every filter, making backends that ignore flags such as SQL's
  `include_deleted` consistent) and return `true`.
- Error handling follows the repo: `bool` + `RepoError*` out-param, no exceptions. `page == nullptr` -> `false`, `RepoError::invalid_argument`.

## Implementation steps

1. Add the enums/fields to `list_links_query.hpp` (include `<functional>`).
2. Create `list_links_eval.hpp/.cpp` (`std::stable_sort`; local ASCII lower-casing helper).
3. Declare and define the default `ListLinksPage`.
4. Append the .cpp to `COMMON_CPP_SOURCES`; add `LISTING_UNIT_SOURCES` and the registration line.
5. Write the two unit tests and run them, then the pre-existing list tests.

## Commands

```bash
cmake -S . -B cmake-build -G Ninja
cmake --build cmake-build --target listing__01_list_query_eval listing__02_list_links_page_default_impl
ctest --test-dir cmake-build -R "^listing__" --output-on-failure
# regression on existing list tests and the interface smoke test
cmake --build cmake-build --target metadata__06_list_filtering_pagination inmemory__07_list_filtering_and_pagination core__05_interface_smoke_compile
ctest --test-dir cmake-build -R "^(metadata__06_list_filtering_pagination|inmemory__07_list_filtering_and_pagination|core__05_interface_smoke_compile)$" --output-on-failure
```

Target names follow `<parent_dir>__<stem>`. On a multi-config generator (Visual Studio) append `--config Debug` to the build and `-C Debug` to `ctest`.

## Tests

| Test file | Target | Label | Cases |
|---|---|---|---|
| `tests/unit/storage/listing/01_list_query_eval.cpp` | `listing__01_list_query_eval` | unit | default query equals legacy order (created asc) and legacy include flags; `text` matches slug or URL, case-insensitive, no match -> empty and `total == 0`; sort by each field both orders with tie-break on `short_code`; `offset == total`, `offset > total` -> empty items, `total` correct; `limit == 0` -> empty items, `total` correct; `extra_filter` applied before counting and before windowing; `extra_filter` returning false for all -> empty; `owner` filter; `include_deleted=false` drops records with `deleted_at`. |
| `tests/unit/storage/listing/02_list_links_page_default_impl.cpp` | `listing__02_list_links_page_default_impl` | unit | fake repository returning N records -> page equals `ApplyListQuery`; fake that ignores `include_deleted` (like SQL) -> still filtered; fake returning exactly `kListScanLimit` -> `false` + `permanent_failure`; `ListLinks` error propagated; null `page` -> `invalid_argument`; a subclass that does not override `ListLinksPage` still compiles (guards `tests/unit/core/05_interface_smoke_compile.cpp`). |

Manual check: none (pure logic).

## Constraints

- C++17 only (no `std::span`, no designated initializers); naming per `.clang-tidy`.
- `ListLinks` behavior for existing callers must not change (the redirect path never calls `ListLinks`, C10 untouched).
- No SQL, SOCI or dialect change in this subtask; `ISqlSession::List` is frozen.
- `extra_filter` is called only with records that already passed the native filters; it must not throw (document; the helper does not catch).
- Every new `.cpp` in `COMMON_CPP_SOURCES`; tests in `sources.cmake`.

## Success criteria

1. `ListLinksQuery{}` evaluated through `ApplyListQuery` returns the same order and window as today's in-memory `ListLinks` (test 01, case 1).
2. All filter/sort/window cases in test 01 pass.
3. Scan overflow yields `false` + `RepoError::permanent_failure` (test 02).
4. `ctest -R "^(listing__|metadata__06|inmemory__07|core__05)"` is green and `git diff --stat` shows no change under
   `src/storage/sql`, `src/storage/sqlite`, `src/storage/postgres`.

## Out of scope / risks

- **What a native extension would require (answer to the original question):** SQL push-down of `text`/`sort`/`COUNT(*)` needs a new
  `ISqlSession` method (or a changed `List` signature) implemented in `SqliteSession` and `PostgresSession`, new statements in
  `SqliteSqlDialect` and `PostgresSqlDialect`, and updates to the two stub sessions in tests; filters on state/tag/campaign additionally need
  real columns (02.0 packs them in `attributes_json`, and Postgres `JSONB` vs SQLite `TEXT` differ). There is no Redis metadata repository, so Redis needs nothing.
  This is deferred; the scan cap makes the cost explicit.
- **Performance:** O(n) per call with a 100000-row cap; fine for an operator tool.
- `findById` (02.0/03) can reuse `ListLinksPage` + `extra_filter` instead of its own paging loop; not required here.
