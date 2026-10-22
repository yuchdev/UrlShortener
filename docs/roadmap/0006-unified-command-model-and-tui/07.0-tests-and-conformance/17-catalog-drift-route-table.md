# 17 - Catalog drift: route table

**Parent task:** 07.0 Tests and conformance
**State:** ⬜ Not started
**Depends on:** Task 04.0 (routes in `RouteRegistry`, static route -> command table)
**Blocks:** 34, 08.0/09
**Estimated size:** S
**Suggested agent:** testing-expert

## Objective

Keep three artifacts consistent: catalog section 5 (route -> command table),
the C++ `RouteRegistry` (source of truth for HTTP routes, generating
`docs/api/README.md`), and the route -> command mapping used by the REST
handlers. Also ensure `docs/api/README.md` is regenerated when routes change.

## Dependencies

- 04.0: new routes registered via `RouteRegistry` (`registeredRoutes()` in `include/url_shortener/http/route_registry.hpp`) and the static route -> command table (name/location per 04.0; **UNVERIFIED**).
- Existing: `tools/route_registry_dump.cpp` (target `route_registry_dump`, generates `docs/api/README.md`) and `tests/unit/http/01_route_registry.cpp`, `08_router_registry_consistency.cpp`.

## Files affected

| Path | create/modify | What changes |
|---|---|---|
| `tests/unit/docs/04_command_route_table_consistency.cpp` | create | Boost.Test consistency checks |
| `tests/unit/docs/05_api_reference_matches_registry.cpp` | create | `docs/api/README.md` equals `route_registry_dump` output |
| `sources.cmake` | modify | Append both to `DOCS_UNIT_SOURCES` |
| `CMakeLists.txt` | modify | `target_compile_definitions(... SOURCE_DIR=...)` for both new targets, next to the existing `docs__01_...` line |

Targets: `docs__04_command_route_table_consistency`, `docs__05_api_reference_matches_registry`, label `unit`.

## Classes and functions

`04_...` cases:

| Case | Assertion |
|---|---|
| `table_rows_have_registered_routes` | each catalog section 5 row (method + path pattern) exists in `registeredRoutes()` (except the "any (generic)" row's command column) |
| `registered_command_routes_have_rows` | each route registered for the command family (`/api/v1/links*`, `/api/v1/commands`, `/api/v1/jobs*`) that has an actual command mapping appears in section 5; exclude placeholder routes such as `/qr` and `/routing` (catalog section 3.6), rather than selecting routes by path prefix alone |
| `route_command_exists_in_registry` | the command named by the static route table exists in `CommandRegistry` |
| `operation_ids_unique_and_documented` | operation ids of the new routes are unique and follow the existing naming pattern (copy the rule from `01_route_registry.cpp`) |
| `compatibility_aliases_not_in_command_table` | `/api/v1/short-urls*` and `/r/{slug}` never appear in section 5 (plan: not commands) |
| `redirect_routes_not_commands` | `GET /{slug}` and `GET /r/{slug}` have no command mapping (C10) |

`05_...` case: `api_reference_is_current` - render the same markdown as
`tools/route_registry_dump.cpp` (extract its rendering into a shared header
only if 04.0 already did; otherwise duplicate the ~40-line renderer inside the
test, as `02_route_registry_docs.cpp` already does) and compare to
`SOURCE_DIR "/docs/api/README.md"` after `\r\n` normalization. Failure message:
`regenerate: cmake-build/route_registry_dump > docs/api/README.md`.

## Implementation steps

1. Parse catalog section 5 with a tiny markdown-table reader (shared with subtask 16's `extract` helpers; put in `tests/unit/docs/doc_table.hpp`).
2. Implement cases; assert row counts > 0.
3. Confirm the checked-in `docs/api/README.md` currently matches `route_registry_dump`; if it does not on master, regenerate in the same PR (call out in the description).

## Commands

```bash
cmake --build cmake-build --target route_registry_dump docs__04_command_route_table_consistency docs__05_api_reference_matches_registry
ctest --test-dir cmake-build -R "^docs__0[45]_" --output-on-failure
```

## Tests

| Test file | Target | CTest label | Cases |
|---|---|---|---|
| `tests/unit/docs/04_command_route_table_consistency.cpp` | `docs__04_command_route_table_consistency` | `unit` | 6 above |
| `tests/unit/docs/05_api_reference_matches_registry.cpp` | `docs__05_api_reference_matches_registry` | `unit` | 1 |

## Constraints

- No dependency on the HTTP server; pure registry/doc inspection.
- Must not weaken existing `http__*` characterization tests.

## Success criteria

1. Adding a route to `RouteRegistry` without updating `docs/api/README.md` fails `docs__05_`.
2. Adding a command route without a section 5 row fails `docs__04_`.
3. Both green on Windows and Linux.

## Out of scope / risks

- OpenAPI output content is not diffed (no generator test exists today).
- Risk: the existing checked-in `docs/api/README.md` may already be stale (unverified); fix by regeneration, not by loosening the test.
