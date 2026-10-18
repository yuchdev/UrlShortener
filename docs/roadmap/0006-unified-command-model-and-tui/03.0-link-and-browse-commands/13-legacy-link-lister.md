# 13 - Lister for the default (legacy) store

**Parent task:** 03.0 Link and browse commands on the registry
**State:** ⬜ Not started
**Depends on:** 03.0/12
**Blocks:** 18
**Estimated size:** S
**Suggested agent:** cpp-expert

## Objective

Make `link.list` work when no `--storage-config` is given. In that default mode Task 02.0 keeps `LegacyLinkStore`
over the process-global `url_shortener::linkRepository()`, which has **no list operation**. Add a read-only
`snapshot()` to that legacy repository and a `LegacyLinkLister` that reuses the exact query semantics of subtask 12 by
round-tripping the snapshot through the 02.0 codec, so there is still one implementation of "what a filter means".

## Dependencies

- Subtask 12 (`ILinkLister`, `LinkListQuery`, `build_list_links_query`, `RepositoryLinkLister` internals).
- 02.0/02 codec: `link_to_record(const Link&, ::LinkRecord*, std::string* error)` and `record_to_link`.
- Verified legacy facts (`include/url_shortener/storage/link_repository.h`, `src/storage/link_repository.cpp`):
  `url_shortener::IMetadataRepository` (Link-based; `create/getBySlug/getById/slugExists/update`, no list) has exactly **one**
  implementer, `url_shortener::InMemoryMetadataRepository`, which stores `by_id_` (`unordered_map<string, Link>`) behind a
  `std::shared_mutex`. The global `::IMetadataRepository` (record-based) is a different type - do not confuse them.
- Default-mode rationale (02.0 README, decision 1): with no flag nothing is installed and all existing tests seed
  `linkRepository()` directly; the TUI/CLI default experience (create then list inside one process) must work.

## Files affected

| Path | Action | What changes |
|---|---|---|
| `include/url_shortener/storage/link_repository.h` | modify | Add pure `virtual std::vector<Link> snapshot() const = 0;` to `url_shortener::IMetadataRepository`; declare the override on the in-memory class. |
| `src/storage/link_repository.cpp` | modify | Implement `snapshot()` under a shared lock (copy all `by_id_` values). |
| `include/url_shortener/app/legacy_link_lister.hpp` | create | `LegacyLinkLister`. |
| `src/app/legacy_link_lister.cpp` | create | Implementation. Add to `COMMON_CPP_SOURCES`. |
| `tests/unit/app/44_legacy_link_lister.cpp` | create | Lister over `linkRepository()`-style instance. |
| `sources.cmake` | modify | `APP_UNIT_SOURCES` += 44; `COMMON_CPP_SOURCES` += .cpp. |

## Classes and functions

```cpp
// legacy repository (namespace url_shortener)
virtual std::vector<Link> snapshot() const = 0;   // all links incl. soft-deleted/disabled; order unspecified

// legacy_link_lister.hpp (namespace url_shortener::app)
class LegacyLinkLister final : public ILinkLister {
public:
    LegacyLinkLister(const url_shortener::IMetadataRepository& repository, const LinkCommandService& views);
    Result<LinkListPage> list(const LinkListQuery& query) const override;
private:
    const url_shortener::IMetadataRepository& m_repository;
    const LinkCommandService& m_views;
};
```

`list`: `validate_link_list_query` -> `snapshot()` -> `link_to_record` each (failure -> skip and count nothing; cannot happen
for links produced by `LinkCommandService`, which validates timestamps) -> `ApplyListQuery(std::move(records), build_list_links_query(query))`
-> `record_to_link` on the window -> `m_views.ViewOf`. Same `LinkListPage` shape and ordering semantics as the repository lister.

## Implementation steps

1. Add `snapshot()` to the legacy interface and its single implementer (the interface has no other implementers; verified by grep).
2. Implement `LegacyLinkLister`.
3. Write the test; because the legacy repository is a process singleton, the test constructs its **own**
   `url_shortener::InMemoryMetadataRepository` instance (the class is public) instead of using `linkRepository()`.
4. Register, build, run. Run `10_link_handlers` to prove the singleton behavior is unchanged.

## Commands

```powershell
cmake --build cmake-build --target app__44_legacy_link_lister 10_link_handlers
ctest --test-dir cmake-build -R "^(app__44_legacy_link_lister|10_link_handlers)$" --output-on-failure
```

## Tests

| Test file | Target | Label | Cases |
|---|---|---|---|
| `tests/unit/app/44_legacy_link_lister.cpp` | `app__44_legacy_link_lister` | unit | empty repository -> empty page; the full expectation table of `43_repository_link_lister` (status members, `all`, default active, `q`, `tag`, `campaign`, sorts, windows, `total`, `limit` 0/501, `q` length) runs against the legacy repository so both listers are proven equivalent (expectation data from `tests/unit/app/link_lister_cases.hpp`, created in subtask 12); `snapshot()` returns soft-deleted and disabled links; `snapshot()` is a copy (mutating the repository afterwards does not change an already returned vector); concurrent `create` while listing does not crash (two threads, bounded iterations). |

Manual check: none.

## Constraints

- Legacy repository stays Link-based and in-memory; `snapshot()` is read-only and must take only a shared lock.
- Do not add a list method to the redirect-path interfaces (`getBySlug` etc. unchanged); C10 untouched.
- One query implementation: this lister must call `build_list_links_query`/`ApplyListQuery`, not re-implement filters.
- C++17; naming per `.clang-tidy`; new `.cpp` in `COMMON_CPP_SOURCES`.

## Success criteria

1. `app__44_legacy_link_lister` passes and its case table equals the one used for the repository lister.
2. `ctest -R "^10_link_handlers$"` and the other `HTTP_UNIT_SOURCES` tests still pass.
3. `git grep -n "snapshot()" src/http` is empty (not used on request paths).

## Out of scope / risks

- Interface break: any out-of-tree implementer of the legacy interface would stop compiling (none in this repository).
- Memory: `snapshot()` copies every `Link`; acceptable for the default in-memory mode (data is already in RAM).
- The codec round trip drops nothing that `ViewOf` needs; if 02.0 changes the codec, the case-table test catches drift.
