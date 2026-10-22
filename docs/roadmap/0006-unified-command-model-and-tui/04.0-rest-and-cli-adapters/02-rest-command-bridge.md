# 02 - REST command bridge

**Parent task:** 04.0 REST and CLI adapters
**State:** ⬜ Not started
**Depends on:** 04.0/01 (args builder), 03.0/18 (`build_command_runtime`), 01.0/02 (error table with legacy REST columns), 01.0/07 (`execute`)
**Blocks:** 03, 04, 05, 06, 07, 09, 05.0/11 (REST job routes reuse it)
**Estimated size:** M
**Suggested agent:** cpp-expert

## Objective

Create the single place in `src/http/` that knows how to (a) obtain a command registry for a request, (b) run one command on
behalf of a legacy resource route, and (c) turn the `CommandResponse` back into the **exact** HTTP status, JSON body and error
strings the route returns today. Every resource-route handler (subtasks 03-07) becomes: build `args` -> `execute_resource_command`
-> `resource_response`. No handler keeps DTO construction or its own status/code mapping.

## Dependencies

- Verified current mapping to reproduce (`src/http/handlers/link_handlers.cpp`): status by `AppErrorCode`
  (`reserved_slug`/`slug_conflict` 409, `not_found` 404, `storage_failure`/`internal` 500, `invalid_url`/`invalid_slug`/`invalid_field` 400);
  error code strings (`repository` for storage failure, `invalid_request` for `invalid_field`, `internal_error` for internal;
  detail `invalid_timestamp` / `invalid_bucket` promoted to the code); error body
  `{"error":{"code","message","request_id"}}` produced by `makeApiErrorResponse` (`src/http/request_handlers.cpp`); success via `makeResponse(..., "application/json")`.
  01.0/02's `error_info` already carries `legacy_rest_status` and `legacy_rest_code` for exactly this purpose.
- Verified: `makeCommandService(config)` in `link_handlers.cpp` builds a service bundle **per request** capturing the request's
  `const ServerConfig&`; the unit tests dispatch with different `ServerConfig` objects (for example `10_link_handlers` uses
  `shortener_allow_private_targets = true`). A runtime cached across requests would freeze one config and change behavior (C3).
  The bridge therefore also builds its runtime per request (management plane, not the redirect hot path). Caching per `HttpServer`
  is a later optimization.
- `03.0/18`: `url_shortener::composition::build_command_runtime(const ServerConfig&)` returns `CommandRuntime { registry, ... }`
  (process-wide holders supply the store, click repository and system info).
- `CallerContext {principal, control_set, transport}` (01.0/06). Today's resource routes are open: no authentication exists in
  `src/http` (verified: no `AccessGuard` use). To keep behavior (C3) they run as `ControlSet::Admin`. This is an explicit, documented
  carry-over of the status quo, not new trust; Milestone 0002 replaces it with session-derived contexts.

## Files affected

| Path | Action | What changes |
|---|---|---|
| `include/url_shortener/http/command_bridge.hpp` | create | Declarations below. May include Beast (it lives in `http/`). |
| `src/http/command_bridge.cpp` | create | Definitions. Add to `COMMON_CPP_SOURCES`. |
| `tests/unit/http/15_command_bridge.cpp` | create | Unit tests. |
| `sources.cmake` | modify | `COMMON_CPP_SOURCES` += .cpp; `HTTP_UNIT_SOURCES` += the test (target = file stem `15_command_bridge`, label `unit` via `ALL_UNIT_SOURCES`). |

## Classes and functions

`namespace url_shortener::http`:

```cpp
/// Caller for the legacy resource routes: principal "rest-resource", ControlSet::Admin, transport "rest".
commands::CallerContext resource_route_caller();

/// Builds the per-request runtime (see Dependencies). Throws std::runtime_error only on startup-class failure.
composition::CommandRuntime make_request_runtime(const ::ServerConfig& config);

/// Runs one command for a resource route. @p request_id is the X-Request-Id value (echoed as the envelope id).
commands::CommandResponse execute_resource_command(const ::ServerConfig& config,
                                                   std::string_view command,
                                                   std::string args_json,
                                                   std::string request_id);
/// Same, with an explicit caller (gated routes pass command_api_caller(config, peer), subtask 08).
commands::CommandResponse execute_resource_command(const ::ServerConfig& config,
                                                   std::string_view command,
                                                   std::string args_json,
                                                   std::string request_id,
                                                   const commands::CallerContext& caller);

/// REST transport policy, applied by every REST entry point (resource routes, gated routes, generic endpoint)
/// before execute: a descriptor whose permission is manage_users is refused with not_authorized until
/// Milestone 0002 provides session-derived callers (decision log D4). Returns the refusal response or nullopt.
std::optional<commands::CommandResponse> rest_policy_refusal(const commands::CommandRegistry& registry,
                                                             std::string_view command, std::string_view id);

/// Legacy error-code string for a failed command: 01.0/02 `legacy_rest_code`, except
/// invalid_field with message == "invalid_timestamp" | "invalid_bucket" -> that message (mirrors codeForAppError).
std::string legacy_error_code(const commands::CommandError& error);

/// Success: @p success_status (200 or 201), body = response.result_json, application/json.
/// Failure: status = legacy_rest_status, body via makeApiErrorResponse(code = legacy_error_code, message = error.message).
/// A response carrying a `job` (not produced by resource routes) maps to 500 internal.
BeastResponse resource_response(const BeastRequest& req, const ::ServerConfig& config, bool is_tls,
                                const commands::CommandResponse& response, unsigned success_status = 200);
```

`execute_resource_command` builds a `CommandRequest {v=1, id, command, args_json, options={}}` directly (no envelope parsing) and calls
`runtime.registry->execute(resource_route_caller(), request)` (or the explicit caller), after `rest_policy_refusal`;
the runtime lives only for the call. The `JobRunner` it references is process-wide (Task 05.0 subtask 20), so per-request
runtimes never lose jobs. `rest_policy_refusal` is a transport rule ("this transport never carries user management"),
not a permission check: authorization of every command it lets through still happens in `execute` (C7).

## Implementation steps

1. Write the header and implementation.
2. Add the unit test with a stub registry (a local `CommandRegistry` with fake descriptors, injected through an overload
   `execute_resource_command(CommandRegistry&, ...)` kept public for tests).
3. Register in `sources.cmake`; build; run.
4. Do not change any handler yet (subtasks 03-07).

## Commands

```powershell
cmake --build cmake-build --target 15_command_bridge
ctest --test-dir cmake-build -R "^15_command_bridge$" --output-on-failure
```

## Tests

| Test file | Target | Label | Cases |
|---|---|---|---|
| `tests/unit/http/15_command_bridge.cpp` | `15_command_bridge` | unit | for **every** `CommandErrorCode`: `resource_response` status equals `legacy_rest_status`, body `error.code` equals `legacy_error_code`, body has `message` and a non-empty `request_id` (when the request carries `X-Request-Id`); promotion of `invalid_timestamp` / `invalid_bucket` messages and non-promotion of any other `invalid_field` message (`slug: missing`); success with 200 and with 201; success body is byte-identical to `result_json` and content type is `application/json`; a response with `job` -> 500; `execute_resource_command` against a stub registry passes `command`, `args_json` and the request id through, with caller `Admin`/`rest`; against the real default runtime `link.get` for an unknown slug yields 404 `not_found` body identical to the current `linkNotFound` shape (`Link not found`); `rest_policy_refusal` with a stub `manage_users` descriptor -> `not_authorized` envelope and the handler is never called, even with an `Admin` caller; `read`/`write` descriptors -> nullopt; the explicit-caller overload passes `User` through (a write then yields 403 `not_authorized` from `execute`). |

Manual check: none.

## Constraints

- C1: this file is the only place in `src/http/` that maps command errors to HTTP; handlers never switch on error codes.
- C3: statuses and code strings come from the 01.0/02 legacy columns, which copy `statusForAppError` / `codeForAppError`; a parity test in subtask 03-06 guards the end result.
- C7: the adapter supplies a `CallerContext` only; no permission logic here.
- C10: the redirect handlers do not call the bridge.
- Never put `error.message` text built from request values into logs; messages are fixed strings or arg names.
- C++17; naming per `.clang-tidy`; `.cpp` in `COMMON_CPP_SOURCES`.

## Success criteria

1. `15_command_bridge` passes (every error code covered).
2. `grep -n "statusForAppError\|codeForAppError" src/http` still shows the old functions (they are deleted in subtask 06 after the last user migrates), and no new copy of that mapping exists.
3. The bridge compiles without including any `storage/` header (`grep -n "storage/" include/url_shortener/http/command_bridge.hpp` is empty).

## Out of scope / risks

- Per-request registry construction costs about one `CommandRegistry` of ~15 descriptors; acceptable for the management plane, measured in the benchmark note of subtask 09.
- The legacy Admin context means the resource routes stay unauthenticated, exactly as today; documented in `docs/security/`
  by Task 08.0, not changed here.
