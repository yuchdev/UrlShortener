# Task 02.0 - Storage path unification (prerequisite)

**Parent milestone:** [Milestone 0006 - Unified Command Model and TUI](/docs/roadmap/0006-unified-command-model-and-tui/plan.md)
**Status:** ⬜ Not started
**Depends on:** none (runs in parallel with [Task 01.0](/docs/roadmap/0006-unified-command-model-and-tui/01.0-command-model-core/README.md))
**Blocks:** Task 03.0 (all subtasks), and through it 04.0, 05.0, 06.0

## Scope

Make the redirect path, REST, CLI (and later TUI/stdio) read and write **one
configured backend**, selectable with `--storage-config <file>`, while the
default (no flag) stays byte-for-byte the current in-memory behavior.

### What the code actually does today (verified, differs from ADR 0001/0002 wording)

ADR 0001 and the earlier text of this task describe a duality: "`LinkService`
(redirect path) reads the pluggable repository, `LinkCommandService` uses the
in-memory singleton". Reading the code shows something different and larger:

- `core::LinkService` (`include/url_shortener/core/link_service.hpp`) and
  `BuildStorageAdapters` (`src/composition/storage_factory.cpp`) are used **only
  by test sources** (`tests/integration/storage/*`, `tests/unit/config/*`;
  **dormant tests:** `CONFIG_UNIT_SOURCES` (`tests/unit/config/01-07` plus two
  analytics round-trips) and `CONFIG_INTEGRATION_SOURCES`
  (`tests/integration/config/01-04`, `tests/integration/storage/14_backend_swap_by_config_only.cpp`)
  are defined in `sources.cmake` (lines ~249 and ~329) but **never registered**
  in `CMakeLists.txt`, so they are neither built nor run. Task 02.0 therefore
  puts its bootstrap test into the registered `STORAGE_INTEGRATION_SOURCES`
  (subtask 11, `storage__15_link_backend_bootstrap`) and does not rely on the
  dormant tests as evidence; registering them is a separate fix, because some
  may no longer compile or pass). No file
  under `src/` other than `storage_factory.cpp` itself calls
  `BuildStorageAdapters`, `ParseStorageConfigFile` is never called from
  `src/main.cpp`, and nothing in `src/http/` includes `link_service.hpp`.
- The redirect handlers (`src/http/handlers/redirect_handlers.cpp`), the REST
  link handlers (`link_handlers.cpp`), `request_handlers.cpp` and the CLI all go
  through the legacy global singletons `url_shortener::linkRepository()` /
  `linkCache()` declared in `include/url_shortener/storage/link_repository.h`
  (directly, via `getLinkForRead` / `updateLinkAndInvalidateCache`, or via
  `LegacyLinkStore`). Inside one server process REST and redirects therefore
  already share one (in-memory) store; the CLI is a separate process with its
  own empty singleton, which is why create-then-get across CLI invocations fails.
- Two unrelated types share names: `::IMetadataRepository` (global namespace,
  `CreateLink/GetByShortCode/...`, over `LinkRecord`) versus
  `url_shortener::IMetadataRepository` (legacy, `create/getBySlug/...`, over
  `Link`); likewise `::RepoError` vs `url_shortener::RepoError`. New code in
  namespace `url_shortener::app` must qualify the global ones as `::...`.
- The pluggable port cannot represent a `Link` as is: `LinkRecord` has no
  tags/campaign/redirect_type/stats, and the SQL backend does not persist
  `deleted_at` or `version` at all (schema columns: id, short_code, target_url,
  created_at, updated_at, expires_at, is_active, owner_id, attributes_json).
  There is no `GetById` on the port. `ListLinksQuery` has only the booleans
  `include_inactive` / `include_deleted` (no disabled-vs-deleted-vs-expired
  status, no tag/campaign/text filter), and the SQL path forwards only
  `owner`, `include_inactive`, `limit`, `offset` (it ignores `include_deleted`).

So the task is "wire the pluggable backend into the live path and teach it to
carry a `Link`", not "reconnect the redirect path" (ADR 0002 and plan.md were
corrected accordingly). The mapping is done in one codec (subtask 02) so a
later schema change can replace the packing with real columns without touching
callers; no subtask of this milestone owns that schema change (Task 03.0
deliberately filters in the app layer instead, 03.0/01 and 03.0/12).

### Decisions made by this task (flag for review)

1. **One shared object, one seam.** A process-wide `app::LinkBackend` holder
   returns an `app::ILinkStore`. Default = `LegacyLinkStore` (unchanged
   behavior, all existing tests keep seeding `linkRepository()` directly).
   With `--storage-config`, startup installs a `RepositoryLinkStore` built from
   `BuildStorageAdapters`. Redirect handlers, REST handlers and the CLI call the
   same `LinkBackend::store()`.
2. **Cache.** `ICacheStore`/`CacheValue` (target, expiry, active flag,
   version) cannot carry what the redirect handlers need (deleted vs disabled
   responses, redirect type, password/rate-limit guards need the full `Link`).
   Redis serialization is a fixed v1 format (`redis_cache_serialization.cpp`).
   Therefore: the full-`Link` hot-path cache stays process-local
   (`ExpiringLinkCache`, subtask 01, an `ILinkCacheStore` with TTL), invalidated
   on every write made through the store; the configured `ICacheStore` is
   invalidated best-effort (`Delete(slug)`) so any consumer of it (core
   `LinkService`, tests) stays coherent, and its failures never fail a request.
   Honoring `cache.backend: redis` for Link reads requires extending
   `CacheValue`; deferred and recorded as a risk. Cross-process staleness
   (CLI changes a link while a server runs) is bounded by the cache TTL
   (default 5 s on persistent backends, none for in-memory).
3. **Redirect statistics are write-behind.** `incrementRedirectStats` today does
   a whole-record read-modify-write on every redirect and erases the cache
   entry each time. Against SQL that would add a DB write to the fast path and,
   worse, let a redirect overwrite a concurrent CLI `link disable` with a stale
   snapshot. Subtask 05 introduces `ILinkStore::recordRedirect`: the request
   thread only adds a **delta** to an in-memory pending map (and patches the L1
   copy); pending deltas are merged into every record loaded from the
   repository, so an L1 expiry or eviction never loses counts; a **background
   flusher thread** (owned by the store, started by the bootstrap, subtask 11)
   persists them on an interval with a narrow write of the stats attribute.
   No repository I/O ever happens inside a redirect request (plan C10).
4. **`findById` on persistent backends is a bounded scan** (no port method);
   an architect decision is needed before Task 03.0 if this is too slow.

## Subtasks

| # | Document | Status | Depends on | Blocks |
|---|----------|--------|------------|--------|
| 01 | [Expiring link cache](01-expiring-link-cache.md) | ⬜ Not started | none | 03 |
| 02 | [Link <-> record codec](02-link-record-codec.md) | ⬜ Not started | none | 03, 03.0/12 |
| 03 | [RepositoryLinkStore: reads and cache](03-repository-link-store-reads.md) | ⬜ Not started | 01, 02 | 04 |
| 04 | [RepositoryLinkStore: writes](04-repository-link-store-writes.md) | ⬜ Not started | 03 | 05, 06, 11 |
| 05 | [recordRedirect and write-behind stats](05-record-redirect-write-behind.md) | ⬜ Not started | 04 | 08, 11 |
| 06 | [Store tests on in-memory and SQLite](06-repository-link-store-backend-tests.md) | ⬜ Not started | 04 | none |
| 07 | [LinkBackend holder and factory](07-link-backend-holder.md) | ⬜ Not started | none | 08, 09, 11, 03.0/11 |
| 08 | [Redirect handlers through the backend](08-redirect-handlers-through-backend.md) | ⬜ Not started | 05, 07 | 12, 07.0/31 |
| 09 | [REST and slug paths through the backend](09-rest-and-slug-paths-through-backend.md) | ⬜ Not started | 07 | 12 |
| 10 | [--storage-config flag and env](10-storage-config-flag-and-env.md) | ⬜ Not started | none | 11, 03.0/17, 04.0/12, 06.0/19 |
| 11 | [Backend bootstrap](11-backend-bootstrap.md) | ⬜ Not started | 04, 05, 07, 10 | 12 |
| 12 | [main.cpp wiring order](12-main-wiring-order.md) | ⬜ Not started | 08, 09, 11 | 13, 14, 03.0/18, 05.0/20, 06.0/19 |
| 13 | [CLI persistence tests](13-cli-persistence-tests.md) | ⬜ Not started | 12 | 15 |
| 14 | [Server and CLI on one SQLite file](14-server-and-cli-shared-sqlite-test.md) | ⬜ Not started | 12 | 15 |
| 15 | [Docs sync](15-docs-sync.md) | ⬜ Not started | 13, 14 | none |

### Dependency graph

```text
01 expiring cache --+
                    +--> 03 store reads --> 04 store writes --+--> 05 recordRedirect --+
02 link codec ------+                                         |                        |
                                                              +--> 06 backend tests    |
07 LinkBackend ---------------+---------------+---------------------------------------+
                              |               |                                       |
                              v               v                                       v
                        09 REST/slug     08 redirect handlers <-- (05, 07)      11 bootstrap <-- 10 flag/env
                              |               |                                       |
                              +---------------+------------------+--------------------+
                                                                 v
                                                          12 main.cpp wiring
                                                            |            |
                                                            v            v
                                                  13 CLI persistence   14 server+CLI one file
                                                            \            /
                                                             15 docs sync
```

01, 02, 07, 10 can start immediately and in parallel.

## Key constraints

- Redirect fast path (C10, "behavior and complexity unchanged; backing object
  changes"): `GET /{slug}` and `GET /r/{slug}` stay cache lookup + repository
  fallback + response. The only change is *which object* answers
  `findBySlug`/`recordRedirect`; no management logic and no synchronous
  counter persistence is added there.
- Default behavior unchanged: with no `--storage-config` (and no
  `URL_SHORTENER_STORAGE_CONFIG`), nothing is installed and every pre-existing
  test passes unmodified (they seed `linkRepository()` directly).
- Cache-aside contract unchanged: invalidate on update/delete, cache failures
  never fail a request.
- No new JSON library; the codec uses string encodings in `attributes` (keys
  without `.`, because the SQL row mapper uses `boost::property_tree` paths).
- Every new `.cpp` in `COMMON_CPP_SOURCES`; new tests in the matching list
  (`APP_UNIT_SOURCES` for `tests/unit/app/`, `STORAGE_INTEGRATION_SOURCES`,
  `CLI_INTEGRATION_SOURCES`).
- CLI/TUI/stdio modes never bind a socket or touch `uri.txt` (C5); the
  bootstrap runs before that decision point and opens no listener.
- Never log DSNs or secrets (`::observability::redactSecretValue`, global namespace).

## Exit criteria

1. With `--storage-config` pointing at a SQLite file, `link create` in one
   process is visible to `link get` in the next (new CLI integration test).
2. A running server and CLI invocations against the same SQLite file agree on
   create, disable, delete and get (subtask 14).
3. Without the flag, `cli_03_link_create_then_get_persists_state` and
   `e2e_12_cli_link_get` still pass unmodified in their isolation assertions,
   and the full pre-existing `unit`/`integration` suites are green.
4. `ctest --test-dir cmake-build -L "unit|contract|integration"` shows no new
   failures; redirect handler unit tests (`tests/unit/http/12_*`, `13_*`) pass.
5. ADR 0001 Non-goals text, `docs/cli/README.md` "Storage limitation" and
   `docs/storage/configuration.md` describe the new behavior and flag.

## Risks

- **R-1 lost update across processes** (CLI `disable` racing a redirect stats
  write): narrowed by subtask 05, not eliminated; needs a repository-level
  atomic counter or compare-and-swap (port change, architect decision).
- **R-2 SQL cost on the hot path**: `SqlMetadataRepository` opens a session per
  call (`session_factory_->Create`) and the L1 cache absorbs most reads; an
  uncached redirect costs one connection + query. Measure with the existing perf
  harness before declaring done (`docs/benchmarking.md`).
- **R-3 `click_events` stay in-process**: `analyticsClickRepository()` is an
  in-memory singleton; `StorageAdapters` has no click-event repository, so
  `link stats` across processes is empty and Task 03.0's `db.rows click_events`
  must read that singleton. Out of scope here, recorded for Task 03.0.
- **R-4 `findById` scan** and **`deleted_at`/tags packed in `attributes`** are
  stop-gaps pending port/schema changes. Task 03.0 subtask 01 extends the list
  port (`ListLinksPage`) but deliberately keeps the packing; real columns
  (schema + Postgres migration) are unowned in this milestone and need an
  architect decision.
