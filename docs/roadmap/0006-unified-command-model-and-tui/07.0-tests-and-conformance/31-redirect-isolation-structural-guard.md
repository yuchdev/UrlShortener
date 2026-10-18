# 31 - Redirect isolation structural guard

**Parent task:** 07.0 Tests and conformance
**State:** ⬜ Not started
**Depends on:** Task 01.0 (new directories exist); can land with 01.0
**Blocks:** 34
**Estimated size:** S
**Suggested agent:** testing-expert (review: security-auditor)

## Objective

Hard gate for plan.md C10 and CLAUDE.md "Redirect fast path is protected":
`GET /{slug}` and `GET /r/{slug}` must stay cache lookup + repository fallback +
response. The test fails if redirect code starts depending on the command
layer, TUI, job runtime, or transport adapters, or if redirect routes become
commands.

## Dependencies

- `src/http/handlers/redirect_handlers.cpp` and `include/url_shortener/http/handlers/redirect_handlers.hpp`; `RouterBuilder` (`include/url_shortener/http/router_builder.hpp`); `RouteRegistry` (`registeredRoutes()`).
- Current includes of `redirect_handlers.cpp`: analytics (`click_event_queue.h`, in-memory click repository), `core/utils.h`, `http/request_handlers.h`, `storage/link_repository.h`. Baseline captured in the test as an allowlist.

## Files affected

| Path | create/modify | What changes |
|---|---|---|
| `tests/unit/commands/80_redirect_isolation_test.cpp` | create | structural + route checks |
| `sources.cmake` | modify | Append to `COMMANDS_UNIT_SOURCES` |
| `CMakeLists.txt` | modify | `target_compile_definitions(commands__80_redirect_isolation_test PRIVATE SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")` |

## Classes and functions

| Case | Assertion |
|---|---|
| `redirect_sources_include_allowlist_only` | each `#include` in `src/http/handlers/redirect_handlers.cpp` and its header is in an explicit allowlist (the current set above plus standard/Boost headers); any `url_shortener/commands/`, `tui/`, `transport/`, `jobs`, `composition/` include fails with file:line. Changing the allowlist requires editing this test (visible in review) |
| `redirect_sources_have_no_command_symbols` | text of those files contains none of: `CommandRegistry`, `CommandRequest`, `ICommandClient`, `JobRunner`, `ITransportAdapter`, `execute(` on a registry, `ArgSpec` |
| `redirect_routes_not_in_command_route_table` | `registeredRoutes()` entries for `GET /{slug}` and `GET /r/{slug}` carry no command mapping field/tag (use the 04.0 mapping accessor) and are absent from catalog section 5 |
| `redirect_route_priority_unchanged` | `RouterBuilder` registration order keeps redirects after the `/api/v1/*` families and before the URI-store fallback (assert via `registeredRoutes()` order or router introspection used by `tests/unit/http/08_router_registry_consistency.cpp`) |
| `redirect_handlers_do_not_construct_registry` | `redirect_handlers.cpp` does not call the registry builder function (token scan for `build_command_runtime`, Task 03.0 subtask 18, and `process_job_runner`, Task 05.0 subtask 20) |
| `command_layer_does_not_depend_on_redirect_handlers` | files under `src/commands/` do not include `http/handlers/redirect_handlers.hpp` (the dependency direction is registry -> services, never into HTTP redirect code) |
| `analytics_hook_remains_best_effort` | `redirect_handlers.cpp` still routes analytics through `RedirectAnalyticsHook`/`AnalyticsService` names present today (guards accidental replacement by a job or command) - assert via token presence, with a comment pointing to CLAUDE.md "analytics failures must never affect redirect response" |

Helper: `std::vector<IncludeLine> includesOf(const std::filesystem::path&)` (line number + text) and `std::string readFile(const std::filesystem::path&)`; paths from `SOURCE_DIR`.

## Implementation steps

1. Generate the allowlist from the current files with a one-off script; paste it into the test as data, reviewed by a human.
2. Write the cases; each failure message names the file, line and the rule.
3. When 02.0 changes the redirect path's storage access (shared backend), update the allowlist in that PR with an explicit justification (02.0 is the only task allowed to touch these includes).

## Commands

```bash
ctest --test-dir cmake-build -R "^commands__80_redirect_isolation_test$" --output-on-failure
```

## Tests

| Test file | Target | CTest label | Cases |
|---|---|---|---|
| `tests/unit/commands/80_redirect_isolation_test.cpp` | `commands__80_redirect_isolation_test` | `unit` | 7 |

## Constraints

- Pure text/route inspection; no network, no server.
- Hard gate (label `unit`), not advisory.
- Token scans ignore comments only if they start with `//`; do not try to parse C++.

## Success criteria

1. Adding `#include <url_shortener/commands/...>` to `redirect_handlers.cpp` fails the test with file:line.
2. Test is green on master-equivalent redirect code before any 0006 code lands.
3. Timing (subtask 32) is the complement; both documented in `tests/perf/README.md`.

## Out of scope / risks

- Does not prove performance, only isolation (32 covers timing).
- Risk: text scans are brittle against refactors; failures must be fixable by editing one table.
