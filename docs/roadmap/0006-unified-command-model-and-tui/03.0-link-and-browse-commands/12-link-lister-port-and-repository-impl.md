# 12 - ILinkLister port and repository-backed lister

**Parent task:** 03.0 Link and browse commands on the registry
**State:** ⬜ Not started
**Depends on:** 03.0/01 (`ListLinksPage`, `extra_filter`), 03.0/02 (contract proof), 02.0/02 (`record_to_link` codec)
**Blocks:** 13, 14, 16, 05.0/10
**Estimated size:** M
**Suggested agent:** cpp-expert

## Objective

Define the application-level browse port (`app::ILinkLister`: query -> page of `LinkView`) and its
implementation over the configured `::IMetadataRepository`. The lister owns the *meaning* of the
filters (lifecycle status, tag, campaign, free text) by decoding each record with the 02.0 codec and
applying `resolveLinkStatus`, so list results can never disagree with `link.get` about a link's status.

## Dependencies

- Subtasks 01-02: `ListLinksQuery {text, sort_by, order, extra_filter, offset, limit, include_*}`,
  `IMetadataRepository::ListLinksPage`, `LinkPage`.
- 02.0/02 (specified): `url_shortener::app::record_to_link(const ::LinkRecord&, Link*, std::string* error)` in
  `include/url_shortener/app/link_record_codec.hpp`. It maps `deleted` (attribute `lnk_deleted`), `enabled`
  (`lnk_enabled`), tags and campaign back into `Link`. Never re-implement that encoding here.
- Verified: `LinkCommandService::toView(const Link&)` is private; list items must equal `link.get` results, so a public
  wrapper is added. `resolveLinkStatus(const Link&)` precedence `deleted > disabled > expired > active` (`core/utils.cpp`);
  decision-log Q9: default status `active`.
- **Catalog correction:** 3.2 lists `status` as `all|active|disabled|deleted`; `LinkView.status` can also be `expired`, and
  `active` must exclude expired links. This spec adds `expired` to the enum (catalog update in Task 08.0; the descriptor is authoritative).
- Namespace caution (verified): `IMetadataRepository`, `RepoError`, `LinkRecord`, `ListLinksQuery` are **global**; code inside
  `url_shortener::app` must write `::IMetadataRepository` (same hazard noted in 02.0 README).

## Files affected

| Path | Action | What changes |
|---|---|---|
| `include/url_shortener/app/link_lister.hpp` | create | `list_status`, `list_sort`, `LinkListQuery`, `LinkListPage`, `ILinkLister`, `serializeLinkListJson`. No storage includes. |
| `src/app/link_lister.cpp` | create | `serializeLinkListJson` and `validate_link_list_query`. Add to `COMMON_CPP_SOURCES`. |
| `include/url_shortener/app/repository_link_lister.hpp` | create | `RepositoryLinkLister`, `build_list_links_query`. |
| `src/app/repository_link_lister.cpp` | create | Implementation. Add to `COMMON_CPP_SOURCES`. |
| `include/url_shortener/app/link_command_service.hpp` / `src/app/link_command_service.cpp` | modify | Public `LinkView ViewOf(const Link&) const` (one line over `toView`). |
| `tests/unit/app/link_lister_cases.hpp` | create | Shared case table (seed links + expected slug lists per query) reused by subtask 13 so both listers are proven equivalent. |
| `tests/unit/app/43_repository_link_lister.cpp` | create | Lister over in-memory and SQLite repositories. |
| `sources.cmake` | modify | `APP_UNIT_SOURCES` += 43; `COMMON_CPP_SOURCES` += two .cpp. |

## Classes and functions

```cpp
// link_lister.hpp  namespace url_shortener::app
enum class list_status { all, active, disabled, expired, deleted };
enum class list_sort   { created_at, slug, updated_at };

struct LinkListQuery {
    list_status status = list_status::active;      // Q9
    std::optional<std::string> q;                  // substring of slug or target URL, case-insensitive
    std::optional<std::string> tag;                // exact tag
    std::optional<std::string> campaign;           // exact campaign name
    list_sort sort = list_sort::created_at;
    bool descending = true;
    std::size_t offset = 0;
    std::size_t limit = 50;                        // 1..500
};

struct LinkListPage { std::vector<LinkView> items; std::size_t total = 0; std::size_t offset = 0; std::size_t limit = 0; };

constexpr std::size_t link_list_max_limit = 500;
constexpr std::size_t link_list_max_query_length = 256;

/// invalid_field for limit outside 1..500 or q longer than 256 (message names the field only).
AppError validate_link_list_query(const LinkListQuery& query);   // code none when valid

class ILinkLister {
public:
    virtual ~ILinkLister() = default;
    virtual Result<LinkListPage> list(const LinkListQuery& query) const = 0;
};

/// {"items":[<serializeLinkViewJson>...],"offset":N,"limit":N,"total":N}
std::string serializeLinkListJson(const LinkListPage& page);
```

```cpp
// repository_link_lister.hpp
/// Translation shared with the legacy lister (subtask 13): status/tag/campaign become `extra_filter`
/// (decode with record_to_link, then resolveLinkStatus / tags / campaign->name); q -> text;
/// sort/descending -> sort_by/order; include_inactive = include_deleted = true; offset/limit copied.
::ListLinksQuery build_list_links_query(const LinkListQuery& query);

class RepositoryLinkLister final : public ILinkLister {
public:
    RepositoryLinkLister(std::shared_ptr<::IMetadataRepository> repository, const LinkCommandService& views);
    Result<LinkListPage> list(const LinkListQuery& query) const override;
private:
    std::shared_ptr<::IMetadataRepository> m_repository;
    const LinkCommandService& m_views;
};
```

`list`: validate -> `build_list_links_query` -> `m_repository->ListLinksPage(q, &page, &err)`; failure ->
`AppError{storage_failure, "list failed"}` (never the driver message); decode each returned record with `record_to_link`
and map with `m_views.ViewOf`; a record that cannot be decoded cannot occur for items that passed `extra_filter`
(the same decode succeeded) - treat a late failure as `internal`.

## Implementation steps

1. Add `ViewOf` to `LinkCommandService`.
2. Implement `link_lister.hpp/.cpp` (types, validation, serializer).
3. Implement `build_list_links_query` with the predicate lambda: decode once per candidate; `status`
   `all` accepts everything, otherwise `resolveLinkStatus(link)` must equal the requested status; `tag` via
   `std::find` on `link.tags`; `campaign` via `link.campaign && link.campaign->name == campaign`.
4. Implement `RepositoryLinkLister::list`.
5. Tests (below); register; build; run.

## Commands

```powershell
cmake --build cmake-build --target app__43_repository_link_lister
ctest --test-dir cmake-build -R "^app__43_repository_link_lister$" --output-on-failure
```

## Tests

| Test file | Target | Label | Cases |
|---|---|---|---|
| `tests/unit/app/43_repository_link_lister.cpp` | `app__43_repository_link_lister` | unit | seed through the 02.0 codec (`link_to_create_request`/`link_to_record`) into (a) `InMemoryMetadataRepository` and (b) a SQLite repository built like `tests/contract/metadata/metadata_contract_common.hpp` (include it by relative path); links: active, disabled, expired (`expires_at` in the past), soft-deleted, tagged, campaign-named; **same expectations on both backends**: each `status` returns exactly its members and `all` returns every state; default query returns only active (Q9); `q` hits slug and URL case-insensitively; `tag`, `campaign`; sort every field both orders; windows and `total` independent of the window; `limit` 0 and 501 -> `invalid_field`; `q` of 257 chars -> `invalid_field`; empty repository -> `items: []`, `total: 0`; repository that fails (fake returning `permanent_failure`) -> `storage_failure` with fixed detail; a record without `lnk_*` attributes (written by other code) lists as enabled/active per codec tolerance; every item serialized equals `serializeLinkViewJson(LinkCommandService::GetLink(slug))` for the same store; `serializeLinkListJson` shape for 0, 1, 2 items. |

Manual check: none (command-level manual checks are in subtask 14).

## Constraints

- Extend ports, do not bypass them: only `::IMetadataRepository::ListLinksPage`; no SQL, SOCI or driver include.
- C1/C3: item JSON is `serializeLinkViewJson`; no list-specific item shape. C6: no `owner`/`attributes` exposure.
- The attribute encoding lives only in 02.0's codec; if it changes, this code keeps working.
- C10: the redirect path never uses the lister.
- C++17; `lower_case` type names for the new enums (matches 01.0 style), `m_` members; `.cpp` in `COMMON_CPP_SOURCES`.

## Success criteria

1. `app__43_repository_link_lister` passes on both in-memory and SQLite, with identical expected results.
2. For every listed item, serialized text equals the `GetLink` result (test asserts string equality).
3. `total` is stable across pages and unaffected by `offset`/`limit`.
4. `grep -rn "soci\|sqlite3\|libpq" src/app/repository_link_lister.cpp src/app/link_lister.cpp` is empty.

## Out of scope / risks

- Cost is O(n) decode per call under the 100000-row cap (subtask 01 risk). Real columns + SQL push-down are the follow-up.
- Cross-process staleness is not an issue here: lists bypass the L1 cache and read the repository.
- `LinkView.stats` is zero for listed items unless 02.0's codec restores stats from `lnk_stats` (it does, per the spec); then they equal `link.get`.
