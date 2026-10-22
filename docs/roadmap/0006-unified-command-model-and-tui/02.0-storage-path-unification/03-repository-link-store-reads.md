# 03 - RepositoryLinkStore: reads and cache

**Parent task:** 02.0 Storage path unification
**State:** ⬜ Not started
**Depends on:** 02.0/01, 02.0/02
**Blocks:** 04
**Estimated size:** M
**Suggested agent:** cpp-expert

## Objective

Create `app::RepositoryLinkStore`, an `app::ILinkStore` over the pluggable
`::IMetadataRepository` and `::ICacheStore`, and implement its read side:
`findBySlug` (L1 cache, then repository, populate L1), `findById` (bounded
scan, see below), `slugExists`, `invalidateCache`. Write methods are stubbed to
return `internal` until subtask 04 so this subtask is independently reviewable.

## Dependencies

- 02.0/01 `ExpiringLinkCache`; 02.0/02 `record_to_link`.
- Ports (all verified): `::IMetadataRepository` (`GetByShortCode`, `Exists`,
  `ListLinks`, ...), `::ICacheStore::Delete`, `::ListLinksQuery
  {owner, include_inactive, include_deleted, offset, limit}`.
- `app::ILinkStore` (`include/url_shortener/app/link_command_service.hpp`,
  verified: `create, findBySlug, findById, slugExists, invalidateCache, update`).

## Files affected

| Path | Create / modify | What changes |
|---|---|---|
| `include/url_shortener/app/repository_link_store.hpp` | create | Class and options (all `ILinkStore` overrides declared; write bodies filled in 04/05) |
| `src/app/repository_link_store.cpp` | create | Read-side implementation, write stubs |
| `tests/unit/app/21_repository_link_store_reads.cpp` | create | Read tests with fake ports |
| `sources.cmake` | modify | `COMMON_CPP_SOURCES` += `src/app/repository_link_store.cpp`; `APP_UNIT_SOURCES` += test |

## Classes and functions

```cpp
namespace url_shortener::app {

struct RepositoryLinkStoreOptions
{
    std::chrono::seconds stats_flush_interval{30};   // used by 05; 0 = write-through
    std::size_t id_scan_page = 200;                  // ListLinks page size for findById
    std::size_t id_scan_max  = 100000;               // hard cap of scanned records
};

class RepositoryLinkStore final : public ILinkStore
{
public:
    RepositoryLinkStore(std::shared_ptr<::IMetadataRepository> repo,
                        std::shared_ptr<::ICacheStore> cache,           // may be null
                        std::shared_ptr<url_shortener::ILinkCacheStore> l1,
                        const IClock& clock,
                        RepositoryLinkStoreOptions options = {});

    bool create(const Link& link, AppError* error = nullptr) override;          // 04
    std::optional<Link> findBySlug(const std::string& slug) const override;
    std::optional<Link> findById(const std::string& id) const override;
    bool slugExists(const std::string& slug) const override;
    void invalidateCache(const std::string& slug) override;
    bool update(const Link& link, AppError* error = nullptr) override;          // 04
    bool recordRedirect(const Link& link, AppError* error = nullptr) override;  // 05

private:
    std::shared_ptr<::IMetadataRepository> m_repo;
    std::shared_ptr<::ICacheStore> m_cache;
    std::shared_ptr<url_shortener::ILinkCacheStore> m_l1;
    const IClock& m_clock;
    RepositoryLinkStoreOptions m_options;
};
}
```

Read semantics:

- `findBySlug`: `m_l1->get(slug)` hit -> return. Miss -> `m_repo->GetByShortCode(slug,
  &err)`; absent -> `nullopt` (negative results are not cached). Present ->
  `record_to_link`; codec failure -> `nullopt` and a `warn` log line
  (`link_store_decode_failed`, slug only); success -> `m_l1->set(slug, link)`.
- Repository failure (`RepoError::transient_failure` / `permanent_failure`) is
  logged (`link_store_read_failed`, error name only) and returns `nullopt`.
  **Known limitation:** `ILinkStore::findBySlug` returns `optional`, so an
  outage looks like "not found" (redirect 404/410 instead of 5xx). Changing the
  interface to carry an error is deferred (risk below).
- `findById`: no `GetById` exists on the port. Scan
  `ListLinks({.owner=nullopt, include_inactive=true, include_deleted=true,
  offset, limit=id_scan_page})` page by page, decode each record, stop at the
  first match or after `id_scan_max` records. A match is also written to L1.
  Cost is O(n); acceptable for admin paths (`link get --id`), never used by
  redirects.
- `slugExists`: `m_repo->Exists(slug, &err)`; on error returns `false` (a later
  `create` still fails on the uniqueness constraint and maps to
  `slug_conflict`).
- `invalidateCache(slug)`: `m_l1->erase(slug)` and, when `m_cache` is set,
  `m_cache->Delete(slug, &cache_error)` ignoring the result.

`ILinkStore` methods are non-`const` for writes; reads are `const` and call
non-const members of shared collaborators through the `shared_ptr`s (allowed).

## Implementation steps

1. Header and constructor (reject null `repo` / null `l1` with
   `std::invalid_argument` - programming error at startup).
2. Implement the read methods with a file-local `decode(const ::LinkRecord&)`.
3. Stub write methods returning `false` and setting `AppError{internal,
   "not implemented"}`.
4. Fake ports for tests: `FakeMetadataRepository` (map-backed, counts calls,
   injectable `RepoError`), `RecordingCacheStore`.
5. Tests, sources, build, run.

## Commands

```powershell
cmake --build cmake-build --target app__21_repository_link_store_reads
ctest --test-dir cmake-build -R "^app__21_repository_link_store_reads$" --output-on-failure
```

## Tests

| Test file | Target | Label | Cases |
|---|---|---|---|
| `tests/unit/app/21_repository_link_store_reads.cpp` | `app__21_repository_link_store_reads` | unit | below |

- `findBySlug` miss -> repository read once -> L1 populated -> second call does
  not touch the repository (call counter).
- Absent slug returns `nullopt` and is not cached (second call reads again).
- L1 TTL: with `ManualClock` and `ExpiringLinkCache(ttl=5s)`, a read after 6 s
  re-reads the repository and sees a record changed behind the store's back
  (the cross-process staleness bound).
- Transient and permanent repository errors -> `nullopt`, nothing cached.
- Undecodable record (bad `lnk_stats`) -> `nullopt`, nothing cached.
- `findById`: found on the 1st page, on the 3rd page (`id_scan_page = 2`),
  absent; stops at `id_scan_max` (assert page count); soft-deleted record is
  found (listing includes deleted).
- `slugExists` true/false/error.
- `invalidateCache` erases L1 and calls `ICacheStore::Delete` once; with a null
  `m_cache` no crash; `ICacheStore` failure does not throw.
- Constructor rejects null repo / null L1.

## Constraints

- Redirect fast path (C10): `findBySlug` hit path is exactly one L1 lookup; the
  miss path is one repository read; nothing else.
- Cache failures never fail a request (cache-aside contract, CLAUDE.md).
- Qualify global types (`::IMetadataRepository`, `::ICacheStore`, `::RepoError`)
  - the unqualified names inside `url_shortener` refer to the legacy
  `link_repository.h` types.
- Log only slug and error names; never target URLs, tags or metadata.
- C++17; `m_` members.

## Success criteria

1. `app__21_repository_link_store_reads` passes.
2. The L1-hit test proves zero repository calls on the second read.
3. The staleness test proves a change made behind the store becomes visible
   after the L1 TTL.

## Out of scope / risks

- **R-5** outage looks like not-found (interface limitation); fix = change
  `findBySlug` to return a `Result<std::optional<Link>>`, touching
  `LinkCommandService` and its tests; propose in Task 03.0 or a separate ADR.
- `findById` scan cost; add `GetById` to `::IMetadataRepository` (three
  adapters + contract suite) if admin usage shows it matters.
- `ICacheStore` is only invalidated, never read, for Link data (see README
  decision 2).
