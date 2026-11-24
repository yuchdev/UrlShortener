# 07 - GET /api/v1/links (list) route

**Parent task:** 04.0 REST and CLI adapters
**State:** ⬜ Not started
**Depends on:** 02 (bridge), 01 (args builder), 06 (handler cleanup done, so this adds to a clean file), 08 (gate and `command_api_caller` — this subtask's **only** auth mechanism in 0006, see Access control), 03.0/14, 03.0/18 (`link.list` registered in the runtime)
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
- Access control (2026-11-16 user decision, threat-model T-71): the existing link routes are unauthenticated (status quo, C3, T-60),
  but this route is **new** and exposes bulk enumeration of every link (including disabled/deleted with `status=all|deleted`) and a
  scan of up to `kListScanLimit` rows per call. It is therefore **gated** like the generic endpoint (subtask 08, decision log D4)
  **and** `status=all|deleted` additionally require write/admin. "Authenticated" for 0006 (no sessions exist until Milestone 0002) is
  defined **entirely by the config gate and `command_api_caller(config, context.peer)`** (subtask 08):
  - **Gate off (default `command_api_enabled == false`):** the route answers exactly like an unmatched route (byte-equal to today's
    `GET /api/v1/links` response). No caller - authenticated or not - can reach `link.list`. This 404 is strictly stronger than the
    user's "unauthenticated -> 401": the anonymous case is collapsed into "route does not exist", so **no `401` is ever emitted in
    0006**. Milestone 0002 introduces real sessions and a genuine `401` for the gate-on-but-no-session case.
  - **Gate on:** `command_api_caller` yields **Admin** only when `command_api_role == "admin"` **and** the peer is loopback
    (`is_loopback_peer`); every other case (role `user`, or role `admin` with a non-loopback or absent peer) yields **User**. Both Admin
    and User count as "authenticated" for 0006 - the gate is the stand-in for authentication; there is no anonymous caller once the gate
    is on. (`RouteContext::peer` is added by subtask 09; if this subtask lands first it passes `std::nullopt`, which is non-loopback,
    i.e. `User` - the safe default.)
  - **`status=all|deleted` require write/admin:** a **User** caller may list only **non-deleted** links (`status` in
    {`active` (default), `disabled`, `expired`}); `status=all` or `status=deleted` from a User caller is rejected with `not_authorized`
    -> **403** (the canonical code/status for `not_authorized` in the 01.0/02 table; no other code is canonical) and the store is not
    enumerated for deleted/disabled rows. An **Admin** caller (loopback + role `admin`) may use every `status`. This is enforced on the
    `link.list` **descriptor** via a permission-raising `permission_for` hook (see Files affected), **not** in the adapter, so the same
    rule holds for `POST /api/v1/commands` -> `link.list` (subtask 09) and `cmd link.list` (defense in depth, one enforcement point in
    `execute`).
  - **Interaction with T-60:** the other `/api/v1/links/...` resource routes (single-link GET, POST, PATCH, DELETE) stay unauthenticated
    and run as `Admin`/`rest-resource` (status quo) until Milestone 0002. Only this new bulk-enumeration route is gated. The asymmetry is
    intentional: single-link read was already open; bulk + soft-deleted enumeration is the new exposure this decision closes.
  - **T-72 (100k-row scan):** `kListScanLimit` (100000) is reachable only by an authenticated caller now that the route is gated, so the
    SQL push-down stays deferred (`milestone_carry_forward[18]`); scan overflow surfaces as `permanent_failure`/`storage_failure`, never
    silent truncation (03.0 T-56).

## Files affected

| Path | Action | What changes |
|---|---|---|
| `include/url_shortener/http/query_params.hpp` | create | `parse_query_string`, `percent_decode`. |
| `src/http/query_params.cpp` | create | Implementation. Add to `COMMON_CPP_SOURCES`. |
| `include/url_shortener/http/handlers/link_handlers.hpp` | modify | Declare `handleListLinks`. |
| `src/http/handlers/link_handlers.cpp` | modify | Implement it. |
| `src/commands/link_list_command.cpp` | modify | Add a `permission_for` hook to the `link.list` descriptor (`make_link_list_descriptor`, 03.0/14): it returns `Permission::write` when the validated `status` arg is `all` or `deleted`, else `Permission::read` (the base). The hook only **raises** (01.0 T-09); `execute` already enforces it (a `User` caller -> `not_authorized`). This is the single enforcement point shared by this route, `POST /api/v1/commands` and `cmd link.list`. The 03.0/11 REST parity gate and `commands__*` tests that touch `link.list` permissions are updated accordingly (T-71). |
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
Then `execute_resource_command(config, "link.list", args.str(), request_id, command_api_caller(config, context.peer))` -> `resource_response(..., 200)`
(the bridge overload that takes an explicit caller, subtask 02); registry errors surface as
`400 invalid_request` with message `<arg>: <reason>` (the bridge maps `invalid_field`).
The adapter forwards `status` **verbatim** (it never inspects it): the write/admin requirement for `status=all|deleted` is enforced by the
`link.list` descriptor's `permission_for` hook **inside `execute`** and surfaces here as a `403 not_authorized` envelope for a `User`
caller (the bridge maps `not_authorized` -> 403, 01.0/02 table).

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
| `tests/unit/http/21_rest_list_links_route.cpp` | `21_rest_list_links_route` | unit | **gate:** default config (`command_api_enabled == false`) -> response byte-equal to today's unmatched `GET /api/v1/links` and the store is not read. **Auth model (T-71, gate on):** a non-loopback/`user` caller with no query -> 200 lists only **active** links (read allowed for `user`); the **same `user` caller with `status=all` or `status=deleted` -> `403 not_authorized`** and the store is **not** enumerated for deleted/disabled rows; a `user` caller with `status=disabled` or `status=expired` -> 200 (non-deleted, allowed); a **loopback caller under `--command-api-role admin`** with `status=all` -> 200 full list incl. disabled/deleted/expired, and with `status=deleted` -> only soft-deleted; role `admin` from a **non-loopback** peer with `status=all` -> 403 (runs as `user`). Then, with an **admin** caller and the gate enabled: `GET /api/v1/links` with no query -> 200, only active links, newest first, default limit 50; `q=ex%20ample` decoded; `tag=`, `campaign=`; `sort=slug&order=asc`; `limit=2&offset=2` windows and `total`; `limit=501`, `limit=0`, `offset=-1`, `limit=abc`, `status=bogus` -> `400 invalid_request` with message `<arg>: <reason>`; unknown param `foo=1` -> 400 `foo: unknown`; duplicate `limit=1&limit=2` -> 400; malformed `%` -> 400; response items equal `GET /api/v1/links/{slug}` bodies; `POST /api/v1/links` still creates (route coexistence) and `GET /api/v1/links/id/x` / `GET /api/v1/links/abc` still route to their handlers; `HEAD`/`DELETE` on `/api/v1/links` unchanged; `X-Request-Id` present; metric label of the target is `api_links`. |

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
