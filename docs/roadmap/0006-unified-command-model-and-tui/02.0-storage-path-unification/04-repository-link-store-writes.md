# 04 - RepositoryLinkStore: writes

**Parent task:** 02.0 Storage path unification
**State:** ⬜ Not started
**Depends on:** 02.0/03
**Blocks:** 05, 06, 11
**Estimated size:** S
**Suggested agent:** cpp-expert

## Objective

Implement `RepositoryLinkStore::create` and `::update`: Link -> record mapping
through the codec, repository call, `RepoError` -> `AppError` mapping with the
same messages the legacy store uses, and cache invalidation (L1 plus the
configured `ICacheStore`, best effort). After this subtask the store is
functionally complete for everything except redirect statistics (05).

## Dependencies

- 02.0/03 class skeleton; 02.0/02 `link_to_create_request`, `link_to_record`.
- `app::AppError`, `AppErrorCode` (verified in `app_error.hpp`).
- Legacy behavior to match (verified in `src/app/legacy_adapters.cpp`):
  `LegacyLinkStore::create` maps `RepoError::already_exists` to
  `slug_conflict` / "slug already in use" and any other failure to
  `storage_failure` / "repository failure"; `update` maps failure to
  `storage_failure` / "repository failure".
- `LinkCommandService` expectations (`ILinkStore::update` doc): on failure
  return `false` and fill `error` - "typically `storage_failure`, or
  `not_found` if the record vanished".

## Files affected

| Path | Create / modify | What changes |
|---|---|---|
| `src/app/repository_link_store.cpp` | modify | Implement `create` and `update` |
| `tests/unit/app/22_repository_link_store_writes.cpp` | create | Write-path tests with fake ports |
| `sources.cmake` | modify | `APP_UNIT_SOURCES` += test |

## Classes and functions

No new public symbols. Behavior:

`bool create(const Link& link, AppError* error)`

1. `link_to_create_request(link, &req, &why)`; codec failure -> `internal`
   ("invalid link record") with `false`.
2. `now` = `parseRfc3339Zulu(link.created_at)` (the repositories store `now` as
   `created_at`/`updated_at`; verified for the in-memory repository, and the SQL
   mapper writes `ToCreateRow(request, id, now)`).
3. `m_repo->CreateLink(req, link.id, now, &repo_error)`:
   - `RepoError::already_exists` -> `slug_conflict`, "slug already in use";
   - `RepoError::invalid_argument` -> `invalid_field`, "invalid link";
   - anything else -> `storage_failure`, "repository failure".
4. On success: `m_l1->set(link.slug, link)` (fresh copy so the creator's next
   read is a hit) and `m_cache->Delete(link.slug)` best effort.

`bool update(const Link& link, AppError* error)`

1. `link_to_record(link, &record, &why)`; failure -> `internal`.
2. `m_repo->UpdateLink(record, &repo_error)`: `not_found` -> `not_found`,
   "Link not found"; other failure -> `storage_failure`, "repository failure".
3. Always (success or failure) `m_l1->erase(link.slug)`, and on success
   `m_cache->Delete(link.slug)`: the next read reloads, never serving a value
   older than this write. (Invalidate-on-failure is deliberate: a failed write
   leaves an uncertain state.)

Neither method takes a lock; atomicity is the repository's (uniqueness is a
primary key in SQL and a map check under a mutex in memory). Two concurrent
`create`s of the same slug: exactly one succeeds (covered in 06).

## Implementation steps

1. Implement the two methods with a file-local
   `map_repo_error(RepoError, AppError*)` helper.
2. Add `FailingMetadataRepository` (injectable `RepoError`) to the test fakes
   from 03 (move shared fakes to `tests/unit/app/repository_link_store_fakes.hpp`
   if both tests need them; include dir is already `tests/unit/app`).
3. Tests, build, run.

## Commands

```powershell
cmake --build cmake-build --target app__22_repository_link_store_writes
ctest --test-dir cmake-build -R "^app__2[12]_repository_link_store" --output-on-failure
```

## Tests

| Test file | Target | Label | Cases |
|---|---|---|---|
| `tests/unit/app/22_repository_link_store_writes.cpp` | `app__22_repository_link_store_writes` | unit | below |

- create happy path: record visible via the fake repo with attributes packed,
  `created_at == updated_at == link.created_at`, `is_active` per `enabled`.
- Duplicate slug -> `slug_conflict` / "slug already in use", L1 unchanged.
- Repo `invalid_argument` (e.g. 2-char slug) -> `invalid_field`.
- Repo `transient_failure` / `permanent_failure` -> `storage_failure`, message
  "repository failure".
- Bad `created_at` string -> `internal`, repository not called.
- update happy path: new tags/enabled/`deleted_at` reach the repository record
  (`lnk_deleted` present, `is_active == false`); L1 entry erased; `ICacheStore`
  `Delete` called once.
- update of a missing record -> `not_found` / "Link not found"; L1 still erased.
- update failure -> `storage_failure`, L1 erased, cache `Delete` not required.
- `error == nullptr` is accepted by both methods.
- Read-your-writes: `create` then `findBySlug` returns the link without a repo
  read; `update` then `findBySlug` re-reads and returns the updated link.
- A throwing `ICacheStore::Delete` fake does not escape (cache failures never
  fail a request).

## Constraints

- Error messages identical to `LegacyLinkStore` so REST/CLI output
  (characterization tests, C3/C4) does not change when the backend is swapped.
- Cache-aside contract: invalidate on update/delete (soft delete goes through
  `update`), failures ignored.
- No logging of link contents; error names only.
- Contracts: C3, C4 (via message parity), C10 (no hot-path code added).

## Success criteria

1. `app__22_repository_link_store_writes` passes.
2. The message-parity test pins "slug already in use" and "repository failure"
   (strings copied from `legacy_adapters.cpp`).
3. No write path leaves a stale L1 entry (verified by the read-your-writes
   cases).

## Out of scope / risks

- Redirect counter writes (05).
- Optimistic concurrency / version checks: `LinkRecord::version` is not
  persisted by SQL; two processes updating the same link concurrently are
  last-writer-wins (same as the legacy in-memory store within one process).
- Hard delete (`IMetadataRepository::DeleteLink`) is not used: `link.delete`
  is a soft delete via `update` (catalog 3.1).
