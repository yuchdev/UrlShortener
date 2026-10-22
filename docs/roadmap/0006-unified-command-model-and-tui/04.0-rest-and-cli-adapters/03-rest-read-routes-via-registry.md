# 03 - REST read routes through the registry

**Parent task:** 04.0 REST and CLI adapters
**State:** ⬜ Not started
**Depends on:** 02 (bridge), 01 (args builder), 03.0/11 (registered link commands + parity gate), 03.0/08 (`serializeLinkLifetimeStatsJson`)
**Blocks:** 06 (shared cleanup of the old mapping functions)
**Estimated size:** M
**Suggested agent:** cpp-expert

## Objective

Re-point the four read routes - `GET /api/v1/links/{slug}`, `GET /api/v1/links/id/{id}`,
`GET /api/v1/links/{slug}/preview`, `GET /api/v1/links/{slug}/stats` - at the registry
(`link.get`, `link.get{id}`, `link.preview`, `link.stats`) with **zero change** to status codes,
bodies or error codes. The handlers shrink to "build args, execute, respond".

## Dependencies

- Subtask 02 (`execute_resource_command`, `resource_response`), subtask 01 (`args_builder`).
- Verified handler behavior to preserve (`src/http/handlers/link_handlers.cpp`):
  - `handleGetLinkBySlug` / `handleGetLinkById`: `GetLink` -> 200 `serializeLinkViewJson`; unknown -> 404 `{"error":{"code":"not_found","message":"Link not found",...}}`.
  - `handlePreviewLink`: `PreviewLink` -> 200 `serializeLinkPreviewJson`.
  - `handleLinkStats`: query values are read with the file-local `getQueryParam(query_string, key)` (naive substring search for `key=`, first match, no percent-decoding). If `from`, `to` and `bucket` are **all** non-empty -> `GetLinkStats` and 200 `serializeLinkStatsJson` (errors `invalid_timestamp` / `invalid_bucket` as 400 codes); otherwise the lifetime body built from `getLinkForRead` (404 `not_found` when unknown). This migration keeps `getQueryParam` exactly as is (including its quirks).
- 03.0/08 defines `link.stats` with `bucket` as a free `string` precisely so `invalid_bucket` survives; `from`/`to` are decimal epoch-second strings.
- Unchanged routes: `qr`/`routing` placeholders (`handlePlaceholderFeature`, 501) keep reading `getLinkForRead`; they are not commands (catalog 3.6). After Task 02.0/09 they must read the same backend - that change belongs to 02.0, and this subtask must not reintroduce direct `getLinkForRead` calls in the four migrated handlers.

## Files affected

| Path | Action | What changes |
|---|---|---|
| `src/http/handlers/link_handlers.cpp` | modify | Replace the bodies of `handleGetLinkById`, `handleGetLinkBySlug`, `handlePreviewLink`, `handleLinkStats`. |
| `tests/unit/http/16_rest_read_routes_via_registry.cpp` | create | Characterization + edge cases (written first against the old code). |
| `sources.cmake` | modify | `HTTP_UNIT_SOURCES` += the test. |

`include/url_shortener/http/handlers/link_handlers.hpp` is unchanged (same function signatures).

## Classes and functions

No new public symbols. File-local helper (anonymous namespace):

```cpp
BeastResponse run_read_command(const BeastRequest& req, const ::ServerConfig& config, bool is_tls,
                               std::string_view command, commands::args_builder& args);
// execute_resource_command(config, command, args.str(), requestId(req)) -> resource_response(..., 200)
```

Mapping table:

| Route | Command | `args` |
|---|---|---|
| `GET /api/v1/links/{slug}` | `link.get` | `{"slug": <path slug>}` |
| `GET /api/v1/links/id/{id}` | `link.get` | `{"id": <path id>}` |
| `GET /api/v1/links/{slug}/preview` | `link.preview` | `{"slug": ...}` |
| `GET /api/v1/links/{slug}/stats` | `link.stats` | `{"slug"}` plus `from`,`to`,`bucket` **only** when all three query values are non-empty; otherwise `{"slug"}` only (lifetime mode) |

(The partial-range case must send only `slug`, matching the old `if (!from.empty() && !to.empty() && !bucket.empty())` branch.)

## Implementation steps

1. **Characterize first:** write `16_rest_read_routes_via_registry.cpp` and run it against the *unmodified* handlers; it must be green.
2. Add `run_read_command`; rewrite the four handlers; delete their now-unused locals.
3. Re-run the new test, `10_link_handlers`, `02_endpoint_matrix_characterization`, `03_method_and_error_characterization`.
4. `getQueryParam` stays (still used); `linkNotFound` stays (used by placeholders).

## Commands

```powershell
cmake --build cmake-build --target 16_rest_read_routes_via_registry 10_link_handlers 02_endpoint_matrix_characterization 03_method_and_error_characterization
ctest --test-dir cmake-build -R "^(16_rest_read_routes_via_registry|10_link_handlers|02_endpoint_matrix_characterization|03_method_and_error_characterization)$" --output-on-failure
```

## Tests

| Test file | Target | Label | Cases |
|---|---|---|---|
| `tests/unit/http/16_rest_read_routes_via_registry.cpp` | `16_rest_read_routes_via_registry` | unit | get by slug / by id: 200 body equals `serializeLinkViewJson`; unknown slug and unknown id -> 404 `not_found` `Link not found`; preview for enabled, disabled, expired, soft-deleted link -> 200 reduced body with the right `status`; preview unknown -> 404; stats range mode with seeded `analyticsClickRepository()` events -> 200 `serializeLinkStatsJson`; stats `from` after `to`, non-numeric `from`, `bucket=year` -> 400 with codes `invalid_timestamp` / `invalid_timestamp` / `invalid_bucket`; stats with no query -> 200 six-key lifetime body; stats with only `from` -> lifetime body; stats lifetime unknown slug -> 404 `not_found`; response always has `X-Request-Id`; `HEAD`/`POST` on these paths unchanged (405/404 as in `03_method_and_error_characterization`); a request with `ShortenerConfig` flags (`shortener_base_domain`) shows the configured base in `short_url` (proves the per-request config still flows). |

Manual check: start the server, `curl -i localhost:8000/api/v1/links/<slug>/stats` and compare against the same call on `master` (identical headers except `Date`).

## Constraints

- C3: byte-identical bodies; the existing characterization tests are not edited.
- C1: the four handlers contain no `app::` DTO type and no `LinkCommandService` call (grep in success criteria).
- C10: redirect handlers untouched.
- The query-string quirks of `getQueryParam` are preserved on purpose; fixing them is a separate, visible behavior change.
- C++17; naming per `.clang-tidy`; no new `.cpp`.

## Success criteria

1. All four test targets above pass.
2. `grep -n "GetLink\|PreviewLink\|GetLinkStats\|getLinkForRead" src/http/handlers/link_handlers.cpp` matches only `handlePlaceholderFeature` and the (unchanged) legacy helper lines scheduled for removal in subtask 06.
3. `git diff` shows no change in the expected output files of the existing characterization tests.

## Out of scope / risks

- `X-Request-Id` handling and `makeResponse` headers are untouched.
- The partial-range lifetime fallback is odd API design but is today's contract; documenting it as intentional is Task 08.0.
