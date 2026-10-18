# 11 - REST job routes (/api/v1/jobs*)

**Parent task:** 05.0 Jobs, pair import/export and stdio transport
**State:** ⬜ Not started
**Depends on:** 05.0/05 (`job.*` commands), 05.0/20 (process-wide runner, so a job submitted in one request is visible in the next), 04.0/02 (bridge: `execute_resource_command` with an explicit caller, `resource_response`, `rest_policy_refusal`), 04.0/08 (gate `command_api_enabled`, `command_api_caller`), 04.0/09 (route label `api_commands`, peer address in the route context)
**Blocks:** none
**Estimated size:** S
**Suggested agent:** cpp-expert (docs sync: docs-updater; review: security-auditor for the gate)

## Objective

Expose the job commands as the three resource routes the catalog lists in section 5: `GET /api/v1/jobs`, `GET /api/v1/jobs/{id}`, `POST /api/v1/jobs/{id}/cancel`, as thin projections onto `job.list` / `job.get` / `job.cancel` and rows in `RouteRegistry`. No new handler logic: each route maps path/query parameters to command args and returns the response envelope's `result` with the status from the shared table.

These routes are **new HTTP surface on an unauthenticated server** and job results can contain a complete inline export of the link table. They are therefore gated exactly like the generic endpoint (catalog section 2 "REST access control", decision log D4): disabled unless `--command-api-enabled true` (then they answer like an unmatched route), and when enabled they run with `command_api_caller(config, peer)` - read for `user`, `job.cancel` (write) only for `admin` from a loopback peer. They never run with the resource-route `Admin` caller.

## Dependencies

- 05.0/05 (commands exist), 05.0/20 (`process_job_runner()`; per-request runtimes share it), 04.0/02 (projection mechanism), 04.0/08 (gate and caller), 04.0/09 (label and peer plumbing).
- `RouteRegistry` (`include/url_shortener/http/route_registry.hpp`, `src/http/route_registry.cpp`) generates `docs/api/README.md`; `tools/route_registry_dump.cpp` and `tests/unit/docs/02_route_registry_docs.cpp` guard it (verified present). Rows are added with the `route(method, path, label, summary)` helper (verified shape of the `/api/v1/links` rows).

## Files affected

| Path | Action | What changes |
|---|---|---|
| `src/http/route_registry.cpp` | modify | three `route(...)` rows (copy the shape of the `/api/v1/links` rows), label `api_commands`, tag `Commands` |
| `src/http/router_builder.cpp` | modify | register the three handlers in the API block |
| `include/url_shortener/http/handlers/command_handlers.hpp`, `src/http/handlers/command_handlers.cpp` | modify | `handleListJobs`, `handleGetJob`, `handleCancelJob` (gate check -> args -> bridge) |
| `docs/api/README.md` | modify | regenerate via `route_registry_dump`; do not hand-edit |
| `tests/unit/http/40_jobs_routes.cpp` | create | routes, gate, roles, status mapping (Task 05.0 owns `tests/unit/http/40-49`) |
| `sources.cmake` | modify | add test to `HTTP_UNIT_SOURCES` (already part of `ALL_UNIT_SOURCES`) |

Also review `tests/unit/http/01_route_registry.cpp`, `02_endpoint_matrix_characterization.cpp` and `08_router_registry_consistency.cpp`: they may assert the exact route count or matrix; extend them for the new rows rather than weakening them.

## Classes and functions

No new classes. Route rows (catalog section 5):

| Method + path | Command | Arg mapping | Permission |
|---|---|---|---|
| `GET /api/v1/jobs` | `job.list` | query `state`, `limit`, `offset` | read |
| `GET /api/v1/jobs/{id}` | `job.get` | path `id` | read |
| `POST /api/v1/jobs/{id}/cancel` | `job.cancel` | path `id`; empty body accepted | write |

Each handler: `if (!command_api_enabled(config)) return <unmatched-route response>;` build args with `args_builder`;
`execute_resource_command(config, name, args, request_id, command_api_caller(config, peer))`; `resource_response(...)`.
Status codes come from the shared table in 01.0/02: `ok` -> 200 with the job object as body (same JSON as `result`),
`not_found` -> 404, `invalid_field` -> 400, `not_authorized` -> 403, `busy` not applicable. A cancelled job reads 200 with
`error.code = job_cancelled` inside the job object (catalog error table).

## Implementation steps

1. Add the three handlers next to `handleExecuteCommand` (04.0/09) and the router lines.
2. Register the three routes in `RouteRegistry` with summaries, params and response codes; keep ordering consistent with `/api/v1/links`.
3. Run `route_registry_dump` to regenerate `docs/api/README.md`, commit the diff.
4. Add tests: gate, roles, path-param extraction (`j-9`), unknown job -> 404, bad `state` -> 400, `POST` cancel of finished job -> 200 no-op, wrong method -> consistent with the existing matrix.
5. Run characterization suites to confirm nothing else moved.

## Commands

```text
cmake --build cmake-build --target route_registry_dump 40_jobs_routes
ctest --test-dir cmake-build -R "^40_jobs_routes$" --output-on-failure
ctest --test-dir cmake-build -R "^(0[1-9]|1[0-9]|2[0-9])_.*(route|handlers|endpoint|method|router|fallback|redirect|observability|compat)" --output-on-failure
```
Target naming: `tests/unit/http/*` tests live in `HTTP_UNIT_SOURCES`, which feeds `ALL_UNIT_SOURCES` and is built by the plain `foreach` loop in `CMakeLists.txt`, so the target and CTest name is the bare filename stem (`40_jobs_routes`), not `http__...` (verified in `sources.cmake` lines 98 and 438-442). Stems must therefore be unique across `CORE_BASIC`, `LINK_MANAGEMENT` and `HTTP` lists. Windows/multi-config: `--config Debug` / `-C Debug`.

## Tests

| Test file | Target | Label | Cases |
|---|---|---|---|
| `tests/unit/http/40_jobs_routes.cpp` | `40_jobs_routes` | unit | **gate:** default config -> all three routes answer byte-equal to the unmatched-route response and no job is touched; enabled: three routes present in `RouteRegistry` with correct methods; `GET /api/v1/jobs?state=running&limit=10` maps to `job.list` args; `GET /api/v1/jobs/j-1` -> `job.get{id}`; `POST .../cancel` -> `job.cancel`; unknown id 404; `limit=0` and bad `state` 400; **roles:** role `user` (any peer): GET 200, cancel 403 and the job keeps running; role `admin` from a non-loopback peer: cancel 403; role `admin` from `127.0.0.1`: cancel 200; a job submitted through `POST /api/v1/commands` in one request is returned by `GET /api/v1/jobs/{id}` in a later request (process-wide runner); `DELETE /api/v1/jobs/j-1` answers as the existing method matrix does; generated docs contain the three routes (existing docs test extended) |

Manual check: with the server running with `--command-api-enabled true --command-api-role admin` and a long import started via `POST /api/v1/commands` from localhost, `curl` the job routes and watch progress; confirm `GET /{slug}` latency/behavior unchanged; restart without the flag and confirm the routes are gone.

## Constraints

- C1/C3: no per-route business logic, no DTOs; existing routes untouched (the characterization tests are the guard).
- C7 / decision D4: gated, never the resource-route `Admin` caller; `rest_policy_refusal` applies.
- C10: no change to `redirect_handlers` or the redirect routes; new routes live under `/api/v1/jobs` only and must be registered AFTER observability/links per the priority order in CLAUDE.md.
- Do not hand-edit generated `docs/api/README.md`.
- C++17, naming/format, new code in files already listed in `sources.cmake`.

## Success criteria

1. All `tests/unit/http` targets (`ctest --test-dir cmake-build -L unit`) green, including the route-registry docs test `02_route_registry_docs`.
2. `docs/api/README.md` regenerated and contains exactly the three new job routes.
3. With defaults, the three routes are indistinguishable from unknown routes (test compares bodies).

## Out of scope / risks

- SSE/WebSocket progress streaming.
- Per-principal job visibility (Milestone 0002; jobs carry no owner in V1).
