# 07 - GET /api/v1/links (list) route

**Parent task:** 04.0 REST and CLI adapters
**State:** ⬜ Not started
**Depends on:** 02 (bridge), 01 (args builder), 06 (handler cleanup done, so this adds to a clean file), 08 (gate and `command_api_caller`), 03.0/14, 03.0/18 (`link.list` registered in the runtime)
**Blocks:** 10 (route table documentation regeneration), Milestone 0002 task 04.0
**Estimated size:** M
**Suggested agent:** cpp-expert

## Objective

Add the only new link-management REST resource of this milestone: `GET /api/v1/links` -> `link.list`
(query parameters are the command arguments). It is a read-only projection of the command, requires
a real query-string parser (percent-decoding, repeated keys), and registers the route in both
`RouterBuilder` (dispatch) and `registeredRoutes()` (documentation / consistency test).

## Dependencies

- `link.list` and its arguments (03.0/14): `status`, `q`, `tag`, `campaign`, `sort`, `order`, `offset`, `limit`; result
  `{"items":[...],"offset":N,"limit":N,"total":N}`; limits `1..500`.
- Verified router facts (`src/http/router_builder.cpp`, `include/url_shortener/http/router.hpp`): first-match-wins, routes are registered with a
  label; `GET /api/v1/links` is currently **unrouted** (`POST` exists); `RouteContext::query_string` holds the raw query without `?`.
- Verified route-doc facts (`src/http/route_registry.cpp`): `registeredRoutes()` is documentation/consistency data and must contain exactly the
  routes the router has (`tests/unit/http/08_router_registry_consistency.cpp`); `queryParametersFor(path)`, `requestBodyFor`, `responsesFor`
  are keyed by path/method and need new branches; tags come from `tagFor` (`/api/v1/links...` -> `Links`).
- Existing `getQueryParam` (used by stats) does naive substring matching with no decoding; it is **not** reused for this route.
- Access control: the existing link routes are unauthenticated (status quo, C3), but this route is **new** and exposes bulk enumeration
  of every link (including disabled/deleted with `status=all`) and a scan of up to `kListScanLimit` rows per call. It is therefore
  **gated** like the generic endpoint (subtask 08, decision log D4): when `command_api_enabled(config)` is false it answers exactly like
  an unmatched route (byte-equal to today's `GET /api/v1/links` response); when enabled it runs with
  `command_api_caller(config, peer)` (default role `user`, which may read), not with the resource-route `Admin` caller.

## Files affected

| Path | Action | What changes |
|---|---|---|
| `include/url_shortener/http/query_params.hpp` | create | `parse_query_string`, `percent_decode`. |
| `src/http/query_params.cpp` | create | Implementation. Add to `COMMON_CPP_SOURCES`. |
| `include/url_shortener/http/handlers/link_handlers.hpp` | modify | Declare `handleListLinks`. |
| `src/http/handlers/link_handlers.cpp` | modify | Implement it. |
| `src/http/router_builder.cpp` | modify | `router.add(get, "/api/v1/links", "api_links", handleListLinks)` before `.../links/id/{id}`. |
| `src/http/route_registry.cpp` | modify | New `route("GET", "/api/v1/links", "api_links", "List links with filtering, sorting and paging.")`; `queryParametersFor` entries for the eight params; `responsesFor`: 200 and 400. |
| `tests/unit/http/20_query_params.cpp` | create | Parser tests. |
| `tests/unit/http/21_rest_list_links_route.cpp` | create | Route tests. |
| `sources.cmake` | modify | `COMMON_CPP_SOURCES` += `query_params.cpp`; `HTTP_UNIT_SOURCES` += both tests. |

`docs/api/README.md` is regenerated in subtask 10 (one regeneration for all new routes).

## Classes and functions

```cpp
// query_params.hpp  namespace url_shortener::http
struct query_param { std::string name; std::string value; };

/// Splits "a=1&b=%20x&c" (no leading '?'). '+' decodes to space, %XX to the byte; empty names and
/// malformed %XX set @p error to a fixed token ("malformed_query") and return false. Names/values
/// are limited by the caller (request-target length limit, 2048 by default, already applied).
bool parse_query_string(std::string_view query, std::vector<query_param>* out, std::string* error);

std::optional<std::string> percent_decode(std::string_view in);   // nullopt on malformed %XX or decoded NUL

// link_handlers.hpp
BeastResponse handleListLinks(const BeastRequest& req, const ::ServerConfig& config, bool is_tls, const RouteContext& context);
```

`handleListLinks`: if `!command_api_enabled(config)` return the unmatched-route response (the same function the router's
fallback uses; capture today's bytes in the test first). Otherwise parse the query; a malformed query -> `400 invalid_request` "malformed query string". A repeated name -> `400 invalid_request`
"<name>: duplicate". For each parameter, `integer` arguments (`offset`, `limit`) that look like `^[0-9]+$` are added with `add_raw` (a JSON integer
token, at most 18 digits so it fits `long long`); anything else is added with `add_string` so the registry rejects it as `<arg>: wrong_type`. All other names
(including unknown ones) are forwarded with `add_string` so the registry rejects unknown ones (`<name>: unknown`): the endpoint is strict by design.
Then `execute_resource_command(config, "link.list", args.str(), request_id, command_api_caller(config, peer))` -> `resource_response(..., 200)`
(the bridge overload that takes an explicit caller, subtask 02); registry errors surface as
`400 invalid_request` with message `<arg>: <reason>` (the bridge maps `invalid_field`).

## Implementation steps

1. Implement and unit-test `query_params` (pure).
2. Add `handleListLinks`, the router line and the registry descriptor + docs hooks.
3. Update `tests/unit/http/01_route_registry.cpp` minimally if needed (it asserts `hasRoute` for named routes only; no count assertion exists, verified) - no change expected.
4. Route tests against a seeded store; run `08_router_registry_consistency` and `05..07` router tests.

## Commands

```powershell
cmake --build cmake-build --target 20_query_params 21_rest_list_links_route 08_router_registry_consistency 01_route_registry
ctest --test-dir cmake-build -R "^(20_query_params|21_rest_list_links_route|08_router_registry_consistency|01_route_registry|02_route_registry_docs)$" --output-on-failure
```

## Tests

| Test file | Target | Label | Cases |
|---|---|---|---|
| `tests/unit/http/20_query_params.cpp` | `20_query_params` | unit | empty string -> no params; `a=1&b=2`; `a` without `=` (empty value); `a=`; `%20`, `+`, `%2B`, UTF-8 `%C3%A9`; `%zz`, `%2`, trailing `%` -> error; `%00` -> error; empty name (`=x`, `&&`) -> error; duplicate names are returned in order (caller decides); 2 KiB input; very long value does not crash. |
| `tests/unit/http/21_rest_list_links_route.cpp` | `21_rest_list_links_route` | unit | **gate:** default config (`command_api_enabled == false`) -> response byte-equal to today's unmatched `GET /api/v1/links` and the store is not read; enabled + default role from a non-loopback peer still lists (read is allowed for `user`); then, with the gate enabled: `GET /api/v1/links` with no query -> 200, only active links, newest first, default limit 50; `status=all` includes disabled/deleted/expired; `q=ex%20ample` decoded; `tag=`, `campaign=`; `sort=slug&order=asc`; `limit=2&offset=2` windows and `total`; `limit=501`, `limit=0`, `offset=-1`, `limit=abc`, `status=bogus` -> `400 invalid_request` with message `<arg>: <reason>`; unknown param `foo=1` -> 400 `foo: unknown`; duplicate `limit=1&limit=2` -> 400; malformed `%` -> 400; response items equal `GET /api/v1/links/{slug}` bodies; `POST /api/v1/links` still creates (route coexistence) and `GET /api/v1/links/id/x` / `GET /api/v1/links/abc` still route to their handlers; `HEAD`/`DELETE` on `/api/v1/links` unchanged; `X-Request-Id` present; metric label of the target is `api_links`. |

Manual check: against a SQLite-backed server (`--storage-config ... --command-api-enabled true`), create 3 links with the CLI, `curl "http://localhost:8000/api/v1/links?status=all&sort=created_at&order=asc"`.

## Constraints

- C3: only an addition; no existing route, body or status changes; `08_router_registry_consistency` stays green.
- C1: the handler builds args only; the filter semantics live in `link.list` / the lister.
- Request hardening applies as everywhere (`max_request_target_length` 2048 bounds the query); no unbounded allocation from the query.
- Untrusted input: `q`, `tag`, `campaign` are decoded and escaped by `args_builder`; the registry bounds their length (256).
- C10: no change to redirect routes; `GET /api/v1/links` must be registered before the `/{slug}`/`/{path}` fallbacks (it is, because the API block precedes them).
- C++17; naming per `.clang-tidy`; new `.cpp` in `COMMON_CPP_SOURCES`.

## Success criteria

1. `20_query_params` and `21_rest_list_links_route` pass; `08_router_registry_consistency`, `01_route_registry`, `02_route_registry_docs` stay green.
2. `registeredRoutes()` and the router both contain `GET /api/v1/links`.
3. All hostile-input cases return `400` JSON errors, never a crash or a `500`.

## Out of scope / risks

- Cursor pagination, `ETag`, `Link:` headers: not offered.
- Strictness on unknown parameters can break clients that append cache-busters; accepted for a new endpoint, documented in the API reference.
