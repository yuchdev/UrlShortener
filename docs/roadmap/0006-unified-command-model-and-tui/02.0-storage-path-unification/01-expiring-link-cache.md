# 01 - Expiring link cache

**Parent task:** 02.0 Storage path unification
**State:** ⬜ Not started
**Depends on:** none
**Blocks:** 03
**Estimated size:** S
**Suggested agent:** cpp-expert

## Objective

Provide the process-local, full-`Link` hot-path cache used by the repository
backed store: an `ILinkCacheStore` with a TTL and a size bound, driven by an
injected clock. The legacy `InMemoryLinkCacheStore` never expires entries, which
is fine inside one process with the in-memory backend but would serve stale
links forever once another process (CLI, TUI) can modify the same SQLite file.
The legacy class is left untouched; the new class lives in `app/`.

## Dependencies

- `url_shortener::ILinkCacheStore` (`include/url_shortener/storage/link_repository.h`,
  verified: `get(slug)`, `set(slug, link)`, `erase(slug)`; note `get` and `set`
  are non-const).
- `IClock` / `ManualClock` (`include/url_shortener/core/clock.hpp`, verified,
  global namespace).
- `url_shortener::Link` (`include/url_shortener/core/types.h`).

## Files affected

| Path | Create / modify | What changes |
|---|---|---|
| `include/url_shortener/app/expiring_link_cache.hpp` | create | Class declaration |
| `src/app/expiring_link_cache.cpp` | create | Implementation |
| `tests/unit/app/19_expiring_link_cache.cpp` | create | Unit tests (next free number after `18_...` in `APP_UNIT_SOURCES`) |
| `sources.cmake` | modify | `COMMON_CPP_SOURCES` += `src/app/expiring_link_cache.cpp`; `APP_UNIT_SOURCES` += the test |

(`CMakeLists.txt` needs no change: `APP_UNIT_SOURCES` has its own loop that adds
`tests/unit/app` to the include path and names targets `app__<stem>`.)

## Classes and functions

```cpp
namespace url_shortener::app {

/// Process-local cache of full Link objects with TTL and a size bound.
/// ttl == 0 means entries never expire (used for the single-process in-memory backend).
class ExpiringLinkCache final : public url_shortener::ILinkCacheStore
{
public:
    ExpiringLinkCache(const IClock& clock,
                      std::chrono::seconds ttl,
                      std::size_t max_entries = 10000);

    std::optional<Link> get(const std::string& slug) override;   // lazy expiry on read
    void set(const std::string& slug, const Link& link) override;
    void erase(const std::string& slug) override;
    void clear();
    std::size_t size() const;                                    // for tests / metrics

private:
    struct entry
    {
        Link link;
        std::chrono::system_clock::time_point stored_at;
    };
    const IClock& m_clock;
    std::chrono::seconds m_ttl;
    std::size_t m_max_entries;
    mutable std::mutex m_mutex;
    std::unordered_map<std::string, entry> m_entries;
};
}
```

Eviction on `set` when `size() >= max_entries`: first remove all expired
entries; if still full remove one arbitrary entry (map iteration order). No LRU:
the cache is a latency optimization, not a correctness mechanism, and the
bound only protects memory.

The clock is held by reference (same convention as `LinkService`,
`InMemoryCacheStore`); the owner (bootstrap, subtask 11) keeps a `SystemClock`
alive for the whole process.

## Implementation steps

1. Header with Doxygen on the class and the `ttl == 0` meaning.
2. Implement under one `std::mutex` (contention is acceptable; entries are
   copied out, the lock is held for a map lookup only).
3. Tests, sources, build, run.

## Commands

```powershell
cmake -S . -B cmake-build -G Ninja
cmake --build cmake-build --target app__19_expiring_link_cache
ctest --test-dir cmake-build -R "^app__19_expiring_link_cache$" --output-on-failure
```

(Multi-config: `--config Debug` / `-C Debug`.)

## Tests

| Test file | Target | Label | Cases |
|---|---|---|---|
| `tests/unit/app/19_expiring_link_cache.cpp` | `app__19_expiring_link_cache` | unit | below |

- set/get/erase round trip; get of unknown slug -> `nullopt`.
- TTL boundary with `ManualClock`: at `ttl - 1 s` hit; at exactly `ttl` miss
  (entry removed); `set` after expiry refreshes `stored_at`.
- `ttl == 0` never expires even after advancing 10 years.
- Size bound: with `max_entries = 3`, a 4th `set` keeps `size() <= 3`; expired
  entries are evicted before live ones (two expired + one live, add one new:
  the live one survives).
- `clear()` empties; `erase` of an absent key is a no-op.
- Overwrite: a second `set` for the same slug replaces the value.
- Thread-safety smoke: 4 threads x 1000 mixed set/get/erase; no crash, final
  `size() <= max_entries`.

## Constraints

- Hot path (C10): `get` is O(1) average and takes one short lock; no I/O, no
  allocation beyond copying the `Link`.
- Does not touch the legacy `InMemoryLinkCacheStore`/`linkCache()` singletons.
- C++17; naming per `.clang-tidy`: `m_` prefix for private members.
- Never stores or logs anything but the `Link` values passed in.

## Success criteria

1. `app__19_expiring_link_cache` passes.
2. TTL boundary test pins "expired at exactly `ttl`".
3. Grep shows `link_repository.cpp` unchanged by this subtask.

## Out of scope / risks

- Wiring into the store (03) and choosing TTL defaults (11).
- Cross-process invalidation (no pub/sub); staleness is bounded by the TTL only.
- Negative caching (not-found) is intentionally absent.
