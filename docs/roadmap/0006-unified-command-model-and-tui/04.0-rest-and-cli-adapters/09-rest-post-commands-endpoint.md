# 09 - POST /api/v1/commands (generic command endpoint)

**Parent task:** 04.0 REST and CLI adapters
**State:** ⬜ Not started
**Depends on:** 08 (config gate), 02 (bridge: per-request runtime), 06 (clean link handlers), 01.0/03-04, 01.0/07 (`parse_request`, `serialize_response`, `http_status_for`, `execute_json`), 03.0/18 (runtime)
**Blocks:** 10, 05.0/11 (job routes reuse the route label and gate), 07.0/08 (REST generic shim)
**Estimated size:** M
**Suggested agent:** cpp-expert (security-auditor reviews)

## Objective

Add the generic transport entry point of ADR 0002: `POST /api/v1/commands` takes one request envelope as the body and returns one response
envelope, with the HTTP status taken from the shared error table. It is the only way HTTP clients (TUI-remote, web admin, Tauri) reach commands that have no
dedicated resource route. It is disabled unless `--command-api-enabled true`, and when enabled it demands `Content-Type: application/json`.

## Dependencies

- Subtask 08 (`ServerConfig::command_api_enabled`, `command_api_role`).
- 01.0 (specified): `execute_json(const CallerContext&, std::string_view request_text, const envelope_limits&)`, `serialize_response`, `http_status_for(const CommandResponse&)`
  (200 for `ok` including an accepted job, else `error_info_for(code).http_status`), `envelope_limits {max_bytes, max_id_len, ...}`.
- Verified facts:
  - request-size hardening happens before routing in `http_session.cpp` (`413 payload_too_large` when `body > max_request_body_bytes`, `414` for long targets); the envelope parser re-checks `max_bytes`.
  - Unmatched paths answer `404 not_found "Route not found"`; known path with wrong method answers `400 invalid_method` (`Router::dispatch`).
  - `routeLabelForTarget` (`src/http/route_registry.cpp`) would label `/api/v1/commands` as `redirect_root` (falls to the generic root rule) and test
    `tests/unit/http/01_route_registry.cpp` hard-codes the set of known labels; a new label `api_commands` is required (below).
  - There is no authentication in `src/http`; browsers can send "simple" cross-origin `POST` requests (`Content-Type: text/plain`) to a local server without a preflight. Requiring
    `application/json` forces a preflight the server never grants, which blocks drive-by requests from web pages. (DNS-rebinding defenses, e.g. `Host` allow-listing, are out of scope.)
- The per-request runtime uses the **process-wide** `JobRunner` held by Task 05.0 subtask 20 (a per-request runner would lose every job); until 05.0/20
  lands, job-kind commands answer `unsupported` here.
- Peer address: needed by `command_api_caller(config, peer)` (subtask 08). **Verified: nothing carries it today.** The sessions never
  read `remote_endpoint()` and no log line records the peer (the logger only knows how to sanitize a field named `remote_addr`; no caller
  sets it). `RouteContext` (`route_context.hpp`) has only `path_params`, `query_string`, `route_label`. This subtask owns the plumbing
  (step 0 below); 04.0/07 and 05.0/11 read the same field.

## Files affected

| Path | Action | What changes |
|---|---|---|
| `include/url_shortener/http/handlers/command_handlers.hpp` | create | `handleExecuteCommand` declaration. |
| `src/http/handlers/command_handlers.cpp` | create | Implementation. Add to `COMMON_CPP_SOURCES`. |
| `include/url_shortener/http/route_context.hpp` | modify | `RouteContext` += `std::optional<boost::asio::ip::address> peer;` (default empty). |
| `include/url_shortener/http/request_handlers.h`, `src/http/request_handlers.cpp` (line ~465) | modify | `handleShortenerRequest(req, config, is_tls, std::optional<boost::asio::ip::address> peer = std::nullopt)` forwards `peer` to `dispatch`. |
| `include/url_shortener/http/router.hpp` (line ~39), `src/http/router.cpp` (line ~119) | modify | `Router::dispatch(req, config, is_tls, std::optional<boost::asio::ip::address> peer = std::nullopt) const`; sets `context.peer = peer` on both `RouteContext` instances it builds. |
| `src/http/http_session.cpp` (lines ~67 and ~156) | modify | Plain: `socket_.socket().remote_endpoint(ec)`; TLS: `boost::beast::get_lowest_layer(stream_).socket().remote_endpoint(ec)`; error-code overload, `ec` -> `std::nullopt`; pass `.address()` to `handleShortenerRequest`. Not logged. |
| `src/http/router_builder.cpp` | modify | `router.add(post, "/api/v1/commands", "api_commands", handleExecuteCommand)` in the API block, before the `/{path}` fallbacks. |
| `src/http/route_registry.cpp` | modify | New `route("POST", "/api/v1/commands", "api_commands", ...)`, request body and responses documentation, tag `Commands`; `routeLabelForTarget`: `/api/v1/commands` and `/api/v1/jobs` -> `api_commands`; `tagFor`: those prefixes -> `Commands`. |
| `tests/unit/http/01_route_registry.cpp` | modify (minimal) | Add `api_commands` to the "known labels" list and two classifier assertions. |
| `tests/unit/http/22_rest_commands_endpoint.cpp` | create | Endpoint tests. |
| `sources.cmake` | modify | `COMMON_CPP_SOURCES` += `command_handlers.cpp`; `HTTP_UNIT_SOURCES` += the test. |

## Classes and functions

```cpp
// command_handlers.hpp  namespace url_shortener::http
BeastResponse handleExecuteCommand(const BeastRequest& req, const ::ServerConfig& config, bool is_tls, const RouteContext& context);

// uses from subtask 08: command_api_enabled(config), command_api_caller(config, peer)
// uses from subtask 02: rest_policy_refusal(registry, command, id)
```

Peer plumbing (step 0, lands first, behavior-neutral): the defaulted parameter `peer = std::nullopt` means **non-loopback**, so every
existing caller compiles and behaves unchanged. Existing call sites (verified; none edited): `handleShortenerRequest` in
`tests/unit/http/02_endpoint_matrix_characterization.cpp`, `03_method_and_error_characterization.cpp`,
`04_redirect_fallback_characterization.cpp`, `13_root_redirect_handler.cpp`, `tests/unit/link_management/07_stats_increment_test.cpp`;
`Router::dispatch` in `tests/unit/http/07_router_dispatch.cpp`, `10_link_handlers.cpp`, `11_compatibility_handlers.cpp`,
`12_prefixed_redirect_handler.cpp`, `13_root_redirect_handler.cpp`, `14_fallback_handler.cpp`; production: `http_session.cpp` (2) and
`request_handlers.cpp` (`applicationRouter().dispatch`). The redirect handlers ignore `context.peer` (C10: one optional copy per request,
no lookup). New tests pass an explicit `boost::asio::ip::make_address("127.0.0.1")` or `"10.0.0.5"`.

Algorithm:

1. `!command_api_enabled(config)` -> `makeApiErrorResponse(404, "not_found", "Route not found")` (byte-identical to the unmatched-route reply).
2. Content type: the request's `Content-Type` media type (ignore parameters, case-insensitive) must be `application/json`, else `415 unsupported_media_type` "Content-Type must be application/json".
3. `runtime = make_request_runtime(config)`; `limits.max_bytes = config.max_request_body_bytes`, `limits.max_id_len = config.request_id_max_length`.
4. `request = parse_request(req.body(), limits)`; on error answer that envelope. Then `rest_policy_refusal(*runtime.registry, request.command, request.id)`
   (refuses `manage_users` commands); otherwise `response = runtime.registry->execute(command_api_caller(config, context.peer), request)`.
5. `makeResponse(req, config, is_tls, http_status_for(response), serialize_response(response), "application/json")`.

The body is the envelope, not the legacy `{"error":{...}}` shape; `X-Request-Id` (HTTP header) and the envelope `id` are independent.

## Implementation steps

0. Peer plumbing (above); build and run the whole `-L unit` set to prove it is behavior-neutral before adding the route.
1. Add the label and classifier branch and update test 01 (smallest possible edit: one string in the known list plus classifier cases).
2. Implement the handler and `command_api_caller`; register route and descriptor.
3. Write the endpoint test; run it with the route-consistency tests.

## Commands

```powershell
cmake --build cmake-build --target 22_rest_commands_endpoint 01_route_registry 08_router_registry_consistency docs__02_route_registry_docs
ctest --test-dir cmake-build -R "^(22_rest_commands_endpoint|01_route_registry|08_router_registry_consistency|docs__02_route_registry_docs)$" --output-on-failure
```

## Tests

| Test file | Target | Label | Cases |
|---|---|---|---|
| `tests/unit/http/22_rest_commands_endpoint.cpp` | `22_rest_commands_endpoint` | unit | disabled (default) -> 404 body byte-equal to the unmatched-route response; enabled + role `user`: `link.list` -> 200 envelope `ok:true,result`; `link.create` -> 403 `not_authorized` envelope (`ok:false`), store unchanged; enabled + role `admin` from a loopback peer: `link.create` -> 200 with `result` equal to `serializeLinkViewJson`, then `link.get`, `link.update`, `link.disable`, `link.delete`; enabled + role `admin` from a non-loopback peer: `link.create` -> 403 `not_authorized` (runs as `user`); enabled + role `admin` with **no** peer (`handleShortenerRequest` called without the new argument) -> 403 (absent = non-loopback); a `RouteContext` built by `dispatch` carries the passed peer; a stub `manage_users` descriptor registered in a test registry -> 403 `not_authorized` for every role and peer, handler never called; envelope errors: not JSON -> 400 `invalid_envelope`, missing `command` -> 400, `v:2` -> 400, unknown command -> 404 `unknown_command`, bad args -> 400 `invalid_field` (`<arg>: <reason>`), `slug_conflict` -> 409, `reserved_slug` -> 409, `not_found` -> 404, `dry_run` on a non-dry-run command -> 501 `unsupported`; id echoed; absent id -> `srv-<n>`; id longer than `request_id_max_length` -> 400; `Content-Type` missing / `text/plain` / `application/x-www-form-urlencoded` -> 415; `application/json; charset=utf-8` and `Application/JSON` accepted; body larger than `max_request_body_bytes` -> 400 `invalid_envelope` (the 413 happens earlier in the session layer, covered by existing session tests); unknown top-level envelope key -> 400; `db.rows {"table":"users"}` -> 404 `not_found`; metric label of the path is `api_commands`; no response ever contains the raw request body. |

Manual check: start with `--command-api-enabled true --command-api-role admin`; `curl -s -X POST localhost:8000/api/v1/commands -H 'Content-Type: application/json' -d '{"v":1,"command":"link.create","args":{"target_url":"https://example.com/docs","slug":"docs"}}'`; then the same without the header (expect 415) and without the flag (expect 404).

## Constraints

- C2: the envelope is the only wire shape on this route; C7: authorization happens inside `execute`, the adapter only supplies the caller; C6: responses never echo the request body.
- Secure defaults: disabled unless explicitly enabled; read-only role unless explicitly `admin` **and** the peer is loopback; JSON content type
  required; `manage_users` never reachable over REST before Milestone 0002.
- C3: no existing route, label or response changes; the only edit to an existing test is the added label.
- C10: redirect fast path untouched; the new route is registered before the fallback routes only to match first, not to affect `/{slug}`.
- C++17; naming per `.clang-tidy`; new `.cpp` in `COMMON_CPP_SOURCES`.

## Success criteria

1. `22_rest_commands_endpoint` passes; `01_route_registry`, `08_router_registry_consistency`, docs route test pass.
2. With defaults, `POST /api/v1/commands` is indistinguishable from an unknown route (test compares bodies).
3. Every error code reachable without jobs maps to the HTTP status in the 01.0/02 table (test enumerates them).

## Out of scope / risks

- Host-header allow-listing / DNS-rebinding protection; TLS-only enforcement; rate limiting of the endpoint.
- **Reverse proxy (D4):** the peer is the TCP peer. Behind a proxy on the same host every request is loopback, so `--command-api-role admin`
  would grant admin to every client. `X-Forwarded-For`/`Forwarded` are deliberately not honoured (spoofable); such deployments keep role `user`.
- Accepted-job HTTP status is 200 (01.0 `http_status_for`); a client wanting 202 would need a catalog change. Task 07.0 shim 08 was waiting on this decision: it is 200.
- Per-request registry construction cost; measure with `docs/benchmarking.md` tooling if the endpoint is polled at high rates.
