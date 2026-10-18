# 08 - Redirect handlers through the backend

**Parent task:** 02.0 Storage path unification
**State:** ⬜ Not started
**Depends on:** 02.0/05, 02.0/07
**Blocks:** 12, 07.0/31 (the redirect include allowlist is captured after this change)
**Estimated size:** S
**Suggested agent:** cpp-expert (review: feature-reviewer, protected fast path)

## Objective

Make `GET /{slug}` and `GET /r/{slug}` obtain their `Link` and record their
statistics through `app::LinkBackend::store()` instead of the legacy free
functions. This is a substitution of the object that answers the same two
calls; the handler logic (validation, status classification, guards, response
building, analytics emission) is untouched. With nothing installed the legacy
store answers and behavior is identical.

## Dependencies

- 02.0/07 `LinkBackend::store()`; 02.0/05 `ILinkStore::recordRedirect`.
- Verified call sites in `src/http/handlers/redirect_handlers.cpp`:
  - line 109 `url_shortener::updateLinkAndInvalidateCache(updated)` inside
    `incrementRedirectStats`;
  - line 183 and line 226 `url_shortener::getLinkForRead(slug)` in
    `handlePrefixedRedirect` and `handleRootRedirect`.
- `LegacyLinkStore::findBySlug` is `getLinkForRead` (verified in
  `legacy_adapters.cpp`), so default-mode behavior is byte-identical.

## Files affected

| Path | Create / modify | What changes |
|---|---|---|
| `src/http/handlers/redirect_handlers.cpp` | modify | Three call-site substitutions plus one include |
| `tests/unit/http/30_redirect_through_backend.cpp` | create | Fake-store tests |
| `sources.cmake` | modify | `HTTP_UNIT_SOURCES` += test |

## Classes and functions

No new symbols. Changes:

```cpp
#include <url_shortener/app/link_backend.hpp>
...
void incrementRedirectStats(const Link& link)
{
    // The increment itself moved into ILinkStore::recordRedirect (02.0/05):
    // the legacy default does exactly what this function did before; the
    // repository store only records a delta (no I/O in the request thread).
    app::AppError ignored;
    (void)app::LinkBackend::store().recordRedirect(link, &ignored);   // best effort
}
...
const auto link = app::LinkBackend::store().findBySlug(slug);          // both handlers
```

The handlers keep calling `store()` per request (one atomic load); they do not
cache the reference. `linkStateError`, `passwordGuardCheck`,
`rateLimitGuardAllow`, `redirectStatusFor`, `emitClickEvent` are unchanged.
The `#include <url_shortener/storage/link_repository.h>` stays only if other
symbols from it are still used in the file (UNVERIFIED; `Link`/`LinkStatus`
come from `core/types.h`, so it can likely be dropped - remove it only if the
compiler allows, to keep the redirect file free of the legacy singleton header).

## Implementation steps

1. Add the include, change the three call sites.
2. Build; run the existing redirect characterization tests unchanged.
3. Add the fake-store test (below) and register it.
4. Grep: `redirect_handlers.cpp` must no longer contain `linkRepository`,
   `getLinkForRead` or `updateLinkAndInvalidateCache`.

## Commands

```powershell
cmake --build cmake-build --target 12_prefixed_redirect_handler 13_root_redirect_handler 04_redirect_fallback_characterization 30_redirect_through_backend
ctest --test-dir cmake-build -R "^(12_prefixed_redirect_handler|13_root_redirect_handler|04_redirect_fallback_characterization|30_redirect_through_backend)$" --output-on-failure
Select-String -Path src/http/handlers/redirect_handlers.cpp -Pattern "linkRepository|getLinkForRead|updateLinkAndInvalidateCache"
```

(`HTTP_UNIT_SOURCES` entries go through the `ALL_UNIT_SOURCES` loop: target =
plain file stem, label `unit`.)

## Tests

| Test file | Target | Label | Cases |
|---|---|---|---|
| `tests/unit/http/30_redirect_through_backend.cpp` | `30_redirect_through_backend` | unit | below |
| existing `tests/unit/http/12_*`, `13_*`, `04_*` | unchanged | unit | must pass unmodified |

Fake: `RecordingLinkStore : app::ILinkStore` holding a map of links, counting
`findBySlug`, `recordRedirect`, and failing the test if `update`/`create` are
called. Installed with `LinkBackend::install` in a fixture, `reset()` after.

- Active link only in the fake (legacy repository empty): `GET /{slug}` and
  `GET /r/{slug}` return the configured 302/301 with `Location`; `findBySlug`
  called exactly once; `recordRedirect` once, with the served link (the
  counters are incremented by the store, not the handler).
- Legacy parity: with the default `LegacyLinkStore`, the persisted counters
  after one redirect equal today's (covered by `07_stats_increment_test`).
- Missing slug: 404, `recordRedirect` 0 calls.
- Disabled (410), deleted (404 `link_deleted`), expired: error responses as
  today, `recordRedirect` 0 calls.
- Invalid slug shape: `findBySlug` not called.
- `recordRedirect` returning `false` does not change the response (best effort).
- A throwing `findBySlug` fake is not part of the contract (not asserted).
- Legacy default (no install) is covered by the unmodified 12/13 tests.

## Constraints

- **Protected fast path (C10):** the diff must be limited to the three
  substitutions; no new branches, allocations or logging in the handlers.
  Reviewer checks the diff size (a handful of lines).
- Analytics emission (`emitClickEvent`) stays exactly where it is and keeps
  using `analyticsClickRepository()` (README R-3).
- No change to response codes, headers or body shapes (C3).
- C++17, existing code style of the file.

## Success criteria

1. The four listed test targets pass.
2. The Select-String/grep command above returns no matches.
3. Performance sanity: the existing perf harness (`docs/benchmarking.md`)
   shows no regression for the in-memory default (UNVERIFIED baseline numbers;
   record before/after in the PR).

## Out of scope / risks

- REST link handlers and slug generation (09); installing a persistent store
  (11, 12).
- With a persistent store installed, a redirect for an uncached slug costs one
  repository read (R-2, the existing "repository fallback" of C10);
  `recordRedirect` never does I/O in the request thread (counters are flushed
  by the background flusher of 05).
- `rateLimitGuardAllow` / `passwordGuardCheck` remain the placeholder hooks they
  are today.
