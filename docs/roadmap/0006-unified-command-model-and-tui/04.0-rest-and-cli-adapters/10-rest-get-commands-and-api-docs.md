# 10 - GET /api/v1/commands and API reference regeneration

**Parent task:** 04.0 REST and CLI adapters
**State:** ⬜ Not started
**Depends on:** 09 (route label), 08 (gate, caller), 07 (list route, so one regeneration covers all new routes), 01.0/10 (`command.list` built-in)
**Blocks:** Task 08.0 subtask 09 (final API reference check), Task 07.0 subtask 17 (route-table drift test)
**Estimated size:** S
**Suggested agent:** cpp-expert, then docs-updater for the regenerated file

## Objective

Expose the descriptor set to HTTP clients with `GET /api/v1/commands` (the catalog's `command.list`: name, summary, permission, kind, mutating, dry-run flag, argument
schema), behind the same opt-in gate as the generic endpoint, and regenerate `docs/api/README.md` from `registeredRoutes()` so the three new routes
(`GET /api/v1/links`, `GET` and `POST /api/v1/commands`) are documented from the code.

## Dependencies

- Subtask 09 (`api_commands` label), subtask 08 (`command_api_enabled`, `command_api_caller`), subtask 07 (list route). The `/api/v1/jobs*`
  rows are added later by Task 05.0 subtask 11 (one more regeneration).
- `command.list` built-in (01.0/10): result `{"commands":[{name,summary,perm,kind,mutating,dry_run,args:[{name,type,required,enum?,doc}]}]}`; permission `read`.
- Verified documentation tooling: `tools/route_registry_dump.cpp` (target `route_registry_dump`, built in `CMakeLists.txt`) renders `registeredRoutes()` to markdown; `docs/api/README.md`
  is its output ("Generated from `url_shortener::http::registeredRoutes()`"). The existing file begins with a UTF-8 BOM; keep that byte if the generator emits it, and
  review the diff so existing rows stay byte-identical (contract C3).
- Decision: unlike `POST` (envelope in, envelope out), this route is resource-style like `GET /api/v1/links`: success body = the command `result`, failures use the legacy
  `{"error":{"code","message","request_id"}}` shape produced by the bridge.

## Files affected

| Path | Action | What changes |
|---|---|---|
| `include/url_shortener/http/handlers/command_handlers.hpp` / `src/http/handlers/command_handlers.cpp` | modify | Add `handleListCommands`. |
| `src/http/router_builder.cpp` | modify | `router.add(get, "/api/v1/commands", "api_commands", handleListCommands)`. |
| `src/http/route_registry.cpp` | modify | `route("GET", "/api/v1/commands", "api_commands", "List the registered commands and their argument schemas.")` plus response docs (200, 403, 404). |
| `tests/unit/http/23_rest_get_commands_endpoint.cpp` | create | Endpoint tests. |
| `docs/api/README.md` | modify (regenerate) | Rows for the three new routes. |
| `sources.cmake` | modify | `HTTP_UNIT_SOURCES` += the test. |

## Classes and functions

```cpp
BeastResponse handleListCommands(const BeastRequest& req, const ::ServerConfig& config, bool is_tls, const RouteContext& context);
```

Algorithm: gate (404 as in 09, no content-type requirement for GET) -> parse the query string with `parse_query_string` (any parameter is forwarded as an argument so the
registry rejects it: `command.list` takes none) -> `execute_resource_command(config, "command.list", args, request_id, command_api_caller(config, context.peer))` (subtasks 02 and 08) ->
`resource_response(..., 200)`. Permission failures map to `403 not_authorized`.

## Implementation steps

1. Add the handler, router line, route descriptor.
2. Write the endpoint test; run the consistency tests.
3. Regenerate the API reference:
   ```powershell
   cmake --build cmake-build --target route_registry_dump
   cmake-build\route_registry_dump.exe | Out-File -Encoding utf8 docs\api\README.md   # Windows (single-config Ninja)
   # POSIX: ./cmake-build/route_registry_dump > docs/api/README.md
   ```
   (Multi-config generators place the binary under `cmake-build\Debug\`.) Then `git diff docs/api/README.md`: only added rows, existing rows byte-identical.
4. Hand off the regenerated file to docs-updater only if prose needs a note about the opt-in gate (the generator has no place for it; the note goes in the commands guide, Task 08.0).

## Commands

```powershell
cmake --build cmake-build --target 23_rest_get_commands_endpoint 08_router_registry_consistency 01_route_registry docs__02_route_registry_docs route_registry_dump
ctest --test-dir cmake-build -R "^(23_rest_get_commands_endpoint|08_router_registry_consistency|01_route_registry|docs__02_route_registry_docs)$" --output-on-failure
```

## Tests

| Test file | Target | Label | Cases |
|---|---|---|---|
| `tests/unit/http/23_rest_get_commands_endpoint.cpp` | `23_rest_get_commands_endpoint` | unit | disabled -> 404 identical to an unmatched route; enabled (role `user`) -> 200, body `{"commands":[...]}` lists every registered command, sorted by name, each with `args`; body equals the `result` of `command.list` executed directly on the same runtime; each of the nine link commands, `link.list`, `db.tables`, `db.rows`, `system.info`, `command.list` present with correct `perm`/`mutating`; any query parameter (`?x=1`) -> 400 `invalid_request` `x: unknown`; `PUT` -> 400 `invalid_method`; `POST` on the same path still returns an envelope (route coexistence); descriptor and router both contain `GET /api/v1/commands`; label of the target is `api_commands`; no argument default or doc string contains an internal path or secret (substring checks for `uri.txt`, `password`, `dsn`). |

Manual check: `curl -s localhost:8000/api/v1/commands | head` with the gate on; diff `docs/api/README.md` as above.

## Constraints

- C3: existing rows of `docs/api/README.md` unchanged; C2/C7: gate and caller as in 09; C6: descriptor docs contain no operational secrets.
- Generated documentation is not edited by hand; re-run the tool instead.
- C++17; naming per `.clang-tidy`; no new `.cpp` (the handler joins `command_handlers.cpp`).

## Success criteria

1. `23_rest_get_commands_endpoint` and the route consistency/doc tests pass.
2. `git diff docs/api/README.md` shows exactly three added rows (plus only generator-prose changes, if any).
3. The count of `registeredRoutes()` equals the router's route count (existing consistency test).

## Out of scope / risks

- The `/api/v1/jobs*` routes are added by Task 05.0 subtask 11 and will need one more regeneration.
- OpenAPI output beyond the markdown table does not exist yet in the repository.
