# 05 - recordRedirect and write-behind stats

**Parent task:** 02.0 Storage path unification
**State:** ⬜ Not started
**Depends on:** 02.0/04
**Blocks:** 08, 11
**Estimated size:** M
**Suggested agent:** cpp-expert (review: security-auditor for the concurrency/overwrite reasoning)

## Objective

Give the redirect path a counter-update operation that is safe on a shared
persistent backend and adds **no repository I/O to a redirect request**. Today
`incrementRedirectStats` (`src/http/handlers/redirect_handlers.cpp`) copies the
`Link` it just read, bumps `stats.total_redirects/redirects_24h/redirects_7d/last_accessed_at`
and writes the **whole record** back through `updateLinkAndInvalidateCache`,
which also erases the cache entry. Against SQLite/Postgres that is (a) a
database write plus a cache miss on every redirect and (b) a lost-update hazard:
a redirect can overwrite a concurrent CLI `link disable`/`delete` with its stale
snapshot.

Add `ILinkStore::recordRedirect`, whose default implementation is the current
behavior (so the legacy store and every existing test are unchanged) and whose
`RepositoryLinkStore` override:

- records a **delta** (not an absolute snapshot) in an in-memory pending map,
- patches the L1 copy in place,
- merges pending deltas into every record it loads from the repository (so an
  L1 TTL expiry or size eviction never loses or regresses counts), and
- leaves persistence to a **background flusher thread** that writes only the
  stats attribute, coalesced, on an interval. Nothing in the request thread
  ever touches the repository (plan C10).

## Dependencies

- 02.0/04 (complete store), 02.0/02 `set_stats_attribute` / `get_stats_attribute`.
- `app::ILinkStore` in `include/url_shortener/app/link_command_service.hpp`.
- `IClock` / `ManualClock` (`core/clock.hpp`) for deterministic tests.
- Existing behavior that must stay: `tests/unit/link_management/07_stats_increment_test.cpp`
  (counters increment only for successful redirects) and
  `tests/unit/http/12_*`/`13_*` redirect handler tests - they run against the
  default `LegacyLinkStore`, which keeps `update` semantics.

## Files affected

| Path | Create / modify | What changes |
|---|---|---|
| `include/url_shortener/app/link_command_service.hpp` | modify | Add non-pure `virtual bool recordRedirect(const Link& served, AppError* = nullptr)` to `ILinkStore`, default body increments the counters on a copy and calls `update(copy, error)` (today's behavior moved out of the handler) |
| `include/url_shortener/app/repository_link_store.hpp` | modify | `StatsDelta`, pending map, `flush_stats()`, `start_stats_flusher()`, `stop_stats_flusher()` |
| `src/app/repository_link_store.cpp` | modify | Implement override, merge-on-load, flusher thread |
| `tests/unit/app/23_record_redirect_write_behind.cpp` | create | Tests |
| `sources.cmake` | modify | `APP_UNIT_SOURCES` += test |

`FakeLinkStore` in `tests/unit/app/app_test_fakes.hpp` needs no change because
the new virtual is not pure.

## Classes and functions

```cpp
// ILinkStore (addition, non-pure so existing implementers compile unchanged)
/// Record one successful redirect of @p served (the record the redirect read).
/// Best effort: the caller ignores failure; must not block on I/O in overrides.
virtual bool recordRedirect(const Link& served, AppError* error = nullptr);
// default: copy = served; increment total/24h/7d, last_accessed_at = now;
//          return update(copy, error);   (legacy behavior, unchanged)

// RepositoryLinkStore
struct StatsDelta
{
    std::uint64_t redirects = 0;                                   // to add to all three counters
    std::optional<std::chrono::system_clock::time_point> last_accessed_at;   // max seen
};
bool recordRedirect(const Link& served, AppError* error = nullptr) override;
/// Persist every pending delta now (shutdown, tests). Never called from a request.
void flush_stats();
/// Start/stop the background flusher (interval = options.stats_flush_interval).
void start_stats_flusher();
void stop_stats_flusher();   // joins; also run by the destructor
```

`RepositoryLinkStore::recordRedirect(served)` (request thread, O(1), no I/O):

1. Under `m_stats_mutex`: `m_pending[slug].redirects += 1`;
   `last_accessed_at = max(old, clock.now())`.
2. L1: if `m_l1->get(slug)` is present, apply the same +1 / timestamp to that
   copy and `m_l1->set(slug, copy)`. The L1 entry is **not** erased, so the next
   redirect is a cache hit. If absent, do nothing more.
3. Return `true`. Never throws, never touches `ICacheStore` or `m_repo`.

Merge-on-load (every path that loads a `Link` from `m_repo`: `findBySlug` miss,
`findById`, list helpers): after decoding the record, under `m_stats_mutex`,
add `m_pending[slug]` (if any) to the decoded stats before caching/returning
it. Persisted counters plus pending deltas is therefore always the true value;
an L1 TTL expiry (5 s) shorter than the flush interval (30 s), or a size
eviction, cannot regress counters.

`flush_stats()` (flusher thread or shutdown only):

1. Swap `m_pending` out under the lock (new deltas accumulate in a fresh map).
2. Per slug: `fresh = m_repo->GetByShortCode(slug)` (bypassing L1); absent ->
   drop the delta; else read `get_stats_attribute(fresh->attributes)`, **add**
   the delta, `set_stats_attribute(...)`, `m_repo->UpdateLink(*fresh)` (a narrow
   read-modify-write of one attribute on a *fresh* record: it cannot revert
   `enabled`, `deleted_at`, tags etc. that changed since the redirect read its
   snapshot, except inside the window between the fresh read and the write -
   risk R-1). On failure, add the delta back into `m_pending` (no loss, no
   double count) and continue with the next slug.
3. No logging of link data on failure (slug + error name only).

Flusher thread: `start_stats_flusher()` starts one `std::thread` that waits on
a condition variable with timeout `stats_flush_interval`, then calls
`flush_stats()`; `stop_stats_flusher()` sets the stop flag, notifies, joins and
runs a final `flush_stats()`. With `stats_flush_interval == 0` no thread is
started and `recordRedirect` writes through **from the caller's thread** - this
mode is used only for the in-memory metadata backend (single process,
in-memory "I/O", subtask 11), never for SQL.

Counter semantics across processes: two server processes against one database
both add their own deltas to the freshly read value, so counts are summed, not
last-writer-wins (except the R-1 window). `link.stats` range reports and click
events (analytics) remain the authoritative metric (README R-3).

Defaults: `stats_flush_interval = 30 s` on persistent backends; the bootstrap
(subtask 11) sets it to `0` for the in-memory backend and starts/stops the
flusher.

## Implementation steps

1. Move today's increment logic into the default `ILinkStore::recordRedirect`
   (header only); the redirect handlers call it in subtask 08.
2. Add `StatsDelta`, `m_stats_mutex`, `m_pending`, the flusher thread members
   and merge-on-load in the repository read paths of `RepositoryLinkStore`.
3. Implement the override, `flush_stats`, `start_stats_flusher`, `stop_stats_flusher`.
4. Tests with a `CountingMetadataRepository` wrapper (counts `GetByShortCode`
   and `UpdateLink`) and `ManualClock`.
5. Build, run, and re-run the pre-existing suites that exercise redirects.

## Commands

```powershell
cmake --build cmake-build --target app__23_record_redirect_write_behind 07_stats_increment_test 12_prefixed_redirect_handler 13_root_redirect_handler
ctest --test-dir cmake-build -R "^app__23_" --output-on-failure
# regression: legacy default path unchanged
ctest --test-dir cmake-build -R "^(07_stats_increment_test|12_prefixed_redirect_handler|13_root_redirect_handler)$" --output-on-failure
```

(The `app__` prefix applies to `APP_UNIT_SOURCES`. The link_management and http
unit tests are registered through `ALL_UNIT_SOURCES`, whose loop in
`CMakeLists.txt` lines 131-145 names the target by the plain file stem. Run
`ctest --test-dir cmake-build -N` to confirm names before filtering.)

## Tests

| Test file | Target | Label | Cases |
|---|---|---|---|
| `tests/unit/app/23_record_redirect_write_behind.cpp` | `app__23_record_redirect_write_behind` | unit | below |

- Default method: a minimal `ILinkStore` fake that only implements `update`
  receives one `update` call per `recordRedirect` with counters +1 (legacy parity).
- **No I/O in the request path:** with interval 30 s, 1000 `recordRedirect`
  calls cause **0** `GetByShortCode`/`UpdateLink` calls on the counting repository.
- Coalescing: 5 redirects, then `flush_stats()` -> exactly 1 `UpdateLink`
  carrying `+5`; 1 more redirect and a second flush -> persisted total `6`.
- **L1 expiry does not regress counts:** `ExpiringLinkCache(ttl = 5 s)`, flush
  interval 30 s: 3 redirects, advance 6 s (L1 expired), `findBySlug` returns
  persisted + 3, 2 more redirects, flush -> persisted total = original + 5.
- Same with a size eviction (cache `max_entries = 1`, alternate two slugs).
- **Lost-update regression**: `store.create(link)`; redirect; `store.update(link with enabled=false)`;
  `flush_stats()` -> the persisted record is still `enabled == false` and
  carries the new counters. Same for soft delete (`lnk_deleted` survives).
- L1 not invalidated: after `recordRedirect`, `findBySlug` causes 0 repository
  reads and returns the incremented counters.
- Repository failure on flush: the delta is re-queued, a later `flush_stats()`
  after the repo recovers persists it exactly once.
- Deleted-from-repo link: delta dropped without error.
- `flush_stats()` with nothing pending is a no-op; `stop_stats_flusher()`
  flushes once and joins (destructor too).
- Flusher thread: with a short real interval (50 ms) and a counting repo, a
  pending delta is persisted without any explicit `flush_stats()` call.
- Thread-safety smoke: 4 threads x 500 `recordRedirect` on 10 slugs, then
  `flush_stats()`; persisted totals sum to 2000, no crash.

## Constraints

- Redirect fast path (C10): per redirect the store does one mutex-protected map
  update and one L1 lookup/replace; **no repository I/O in the request thread**
  on persistent backends.
- Failure of stats persistence never affects the redirect response or its
  latency (analytics/stat best-effort rule in CLAUDE.md).
- Do not modify the whole record from the redirect path; only `lnk_stats`, and
  only from the flusher.
- No logging of link data on flush failure (slug + error name only).
- C++17: `std::thread`, `std::condition_variable`; `m_` member prefix.

## Success criteria

1. `app__23_record_redirect_write_behind` passes, including the lost-update,
   L1-expiry and no-I/O-in-request cases.
2. With the legacy store, `07_stats_increment_test` and the redirect handler
   tests pass unchanged.
3. A counting test shows at most `ceil(duration / interval) + 1` repository
   writes per slug, all from the flusher thread.

## Out of scope / risks

- **R-1** residual race inside a flush (read then write of one attribute):
  needs a repository-level atomic counter (`IncrementCounter(short_code, ...)`)
  or compare-and-swap on `version`; port change for architect decision.
- Counter loss on crash between flushes (up to `stats_flush_interval`);
  acceptable because stats are provisional (see 07_stats_increment_test notes
  "Stage 2 provisional stats").
- Replacing `redirects_24h` / `redirects_7d` rolling-window semantics - they are
  monotonic increments today and stay so.
