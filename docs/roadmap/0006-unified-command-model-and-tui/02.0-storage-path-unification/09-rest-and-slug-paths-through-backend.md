# 09 - REST and slug paths through the backend

**Parent task:** 02.0 Storage path unification
**State:** ⬜ Not started
**Depends on:** 02.0/07
**Blocks:** 12
**Estimated size:** S
**Suggested agent:** cpp-expert

## Objective

Remove the last direct uses of the legacy singletons from the HTTP layer so
that, once a store is installed, *no* request path can read or write a different
store than the one the CLI uses. After subtask 07 the REST resource routes
already reach the backend through `makeCommandService` ->
`BuildLegacyLinkCommandService`; what remains are three direct calls.

## Dependencies

- 02.0/07 (`LinkBackend::store()` and the re-pointed factory).
- Verified remaining call sites:
  - `src/http/handlers/link_handlers.cpp:625` `url_shortener::getLinkForRead(slug)`
    in `handlePlaceholderFeature` (qr/routing 501 placeholders, only checks the
    link exists);
  - `src/http/handlers/link_handlers.cpp:664` `getLinkForRead(slug)` in
    `handleLinkStats` (legacy no-window stats response built from
    `Link::stats`);
  - `src/http/request_handlers.cpp:234` `linkRepository().slugExists(candidate)`
    inside `generateUniqueSlug` (the only definition of that function; its
    caller is `tests/unit/core/06_slug_collision_retry_test.cpp`; production
    callers: none found by grep - UNVERIFIED, may be dead code).
- Line 427 of `link_handlers.cpp` only contains a comment mentioning
  `getLinkForRead` (leave).

## Files affected

| Path | Create / modify | What changes |
|---|---|---|
| `src/http/handlers/link_handlers.cpp` | modify | Replace two `getLinkForRead` calls with `app::LinkBackend::store().findBySlug` |
| `src/http/request_handlers.cpp` | modify | Replace `linkRepository().slugExists(...)` with `app::LinkBackend::store().slugExists(...)` |
| `tests/unit/http/31_link_handlers_through_backend.cpp` | create | Fake-store tests through the real router |
| `sources.cmake` | modify | `HTTP_UNIT_SOURCES` += test |

## Classes and functions

No new symbols:

```cpp
const auto link = app::LinkBackend::store().findBySlug(slug);   // link_handlers.cpp x2
... !app::LinkBackend::store().slugExists(candidate) ...        // request_handlers.cpp
```

`#include <url_shortener/app/link_backend.hpp>` is added to both files; the
`link_repository.hpp` include is dropped from either file only if nothing else
from it is used (the compiler decides).

## Implementation steps

1. Substitute the three call sites.
2. Build; run `10_link_handlers`, `11_compatibility_handlers`,
   `02_endpoint_matrix_characterization`, `03_method_and_error_characterization`
   and `06_slug_collision_retry_test` unmodified (default mode).
3. Add the fake-store router test and register it.
4. Grep for leftovers (below).

## Commands

```powershell
cmake --build cmake-build --target 10_link_handlers 11_compatibility_handlers 02_endpoint_matrix_characterization 03_method_and_error_characterization 06_slug_collision_retry_test 31_link_handlers_through_backend
ctest --test-dir cmake-build -R "^(10_link_handlers|11_compatibility_handlers|02_endpoint_matrix_characterization|03_method_and_error_characterization|06_slug_collision_retry_test|31_link_handlers_through_backend)$" --output-on-failure
Select-String -Path src/http/*.cpp,src/http/handlers/*.cpp -Pattern "linkRepository\(|getLinkForRead|linkCache\(|updateLinkAndInvalidateCache"
```

The last command must print nothing.

## Tests

| Test file | Target | Label | Cases |
|---|---|---|---|
| `tests/unit/http/31_link_handlers_through_backend.cpp` | `31_link_handlers_through_backend` | unit | below |
| existing http/core unit tests listed above | unchanged | unit | must pass unmodified |

With a `RecordingLinkStore` installed (legacy repository left empty) and the
real `RouterBuilder::buildApplicationRouter()` as in
`tests/unit/http/12_prefixed_redirect_handler.cpp`:

- `POST /api/v1/links` creates through the fake (its `create` called once; the
  legacy `linkRepository().getBySlug` stays empty); the response body equals
  `serializeLinkViewJson` of the stored link (C3).
- `GET /api/v1/links/{slug}` and `PATCH` / `DELETE` / `.../enable|disable|restore`
  hit the fake's `findBySlug` + `update`.
- `GET /api/v1/links/{slug}/stats` without window parameters (legacy path) reads
  the link through the fake; missing slug -> 404 `not_found` as today.
- `GET /api/v1/links/{slug}/qr` placeholder: existing slug -> 501
  `feature_not_enabled`; missing -> 404.
- `generateUniqueSlug` with a fake that reports every candidate as existing
  gives up after 20 attempts (reuse the 06_slug_collision cases against the
  fake).
- After `LinkBackend::reset()` the legacy path works again (smoke).

## Constraints

- REST bodies and statuses unchanged (C3): this subtask changes where data
  comes from, never the mapping.
- Not on the redirect fast path, but keep the diff to the three substitutions.
- C++17, file style as is.

## Success criteria

1. All six test targets pass.
2. The grep command prints nothing for `src/http/**`.
3. `link_command_dispatch.cpp` and `cli_parser.cpp` still contain no
   `url_shortener/http/` include (architecture invariant of Milestone 0003
   C4/C5, checked with grep).

## Out of scope / risks

- Removing the `uri.txt` / `UriMapSingleton` fallback (C5 forbids touching it
  from CLI; server fallback routes keep their own store).
- The legacy no-window `handleLinkStats` response duplicates logic that Task
  03.0 moves into the registry; do not refactor it here.
- If `generateUniqueSlug` is dead code, deleting it is a separate cleanup (its
  test would go with it).
