# Milestone 0001 - REST API Refactoring - Status

Tracks progress against [plan.md](/docs/roadmap/0001-rest-api-refactoring/plan.md).

## Current status

| Task | Name | Status | Tests |
|------|------|--------|-------|
| 01.0 | Baseline characterization | ✅ Complete | 3 characterization suites |
| 02.0 | Router infrastructure | ✅ Complete | 4 suites (route context, matcher, dispatch, registry consistency) |
| 03.0 | Observability routes | ✅ Complete | 1 suite (`09_observability_handlers`) |
| 04.0 | Link management routes | ✅ Complete | 1 suite (`10_link_handlers`) |
| 05.0 | Compatibility routes | ✅ Complete | 1 suite (`11_compatibility_handlers`) |
| 06.0 | Redirect routes | ✅ Complete | 2 suites (`12_prefixed_redirect_handler`, `13_root_redirect_handler`) |
| 07.0 | Fallback routes | ✅ Complete | 1 suite (`14_fallback_handler`) |
| 08.0 | OpenAPI/docs cleanup | ✅ Complete | Route registry + docs generation tests |

**Legend:** ✅ Complete · 🔶 In progress / partial · ⬜ Not started

**Current gate status:** Milestone 0001 is 100% complete. `handleShortenerRequest()`
in `src/http/request_handlers.cpp` is a one-line delegation to
`applicationRouter().dispatch(req, config, is_tls)`, confirming the old
branch-chain dispatch was fully removed (Task 08.0 cleanup). All target source
files exist and the project builds cleanly against the current `cmake-build/`
build. Full unit suite: **148/148 passing** (`ctest -L unit` in `cmake-build/`).
HTTP/router-specific subset (`01_route_registry` through `14_fallback_handler`,
29 test binaries): **29/29 passing**
(`ctest -R "^0[1-9]_|^1[0-4]_" -L unit`). The full repository has **254 tests**
configured across the unit/contract/integration/e2e/cli labels (`ctest -N`).
Both governing security reviews passed with a clean **PASS** verdict.

## Notes & decisions

- **Filenames shipped in snake_case, not the PascalCase originally proposed.**
  Every per-task spec in this milestone (e.g. `RouteRegistry.cpp`,
  `Router.hpp`, `handlers/LinkHandlers.cpp`) proposed PascalCase filenames.
  Commit `3f4c170 Rename files to snake_style` renamed the entire HTTP router
  tree to snake_case after initial implementation (e.g.
  `ObservabilityHandlers.cpp` -> `observability_handlers.cpp`). Each subtask
  file in this milestone has been annotated with an Implementation note
  pointing at the real, current path.
- **Handler extraction was consolidated relative to the per-task test-file
  plan.** Tasks 04.0 (`01`-`03`) and 05.0 (`01`-`02`) each proposed one test
  file per handler group (e.g. `07_link_read_preview_handlers.cpp`,
  `08_link_mutation_handlers.cpp`, `09_link_action_handlers.cpp`); the shipped
  suite consolidates these into single per-domain files:
  `tests/unit/http/10_link_handlers.cpp` and
  `tests/unit/http/11_compatibility_handlers.cpp`. Test numbering also shifted
  because `tests/unit/http/05_route_context.cpp` through
  `tests/unit/http/08_router_registry_consistency.cpp` were renumbered
  relative to the numbers suggested in the original Task 02.0 specs (which
  proposed `02_route_context.cpp` through `05_router_registry_consistency.cpp`,
  colliding with the Task 01.0 characterization file numbers). No test
  coverage was dropped; case lists were preserved and now live in the
  consolidated files.
- **`handleApplicationRequest()` boundary (Task 07.0, subtask 03):** kept as
  Option A - `handleApplicationRequest()` remains the fallback implementation,
  and `handleFallbackUriStore()` in
  `src/http/handlers/fallback_handlers.cpp` wraps it rather than duplicating
  the `UriMapSingleton` logic.
- **Documentation generation (Task 08.0, subtask 02)** shipped as the
  preferred option: a C++ tool (`tools/route_registry_dump.cpp`) that links
  the route registry and emits Markdown, checked in at `docs/api/README.md`.
  No runtime `GET /api/v1/openapi.json` endpoint was added, consistent with
  the task's restriction against exposing one without an explicit follow-up
  decision.

## Decomposition tree (as built)

```text
docs/roadmap/0001-rest-api-refactoring/
  plan.md
  status.md
  rest-api-refactoring.md
  01.0-baseline-characterization/
    README.md
    01-endpoint-matrix-characterization.md
    02-error-and-method-characterization.md
    03-redirect-and-fallback-characterization.md
  02.0-router-infrastructure/
    README.md
    01-route-context-and-handler-types.md
    02-router-matcher.md
    03-router-dispatch-and-builder.md
    04-registry-consistency-tests.md
  03.0-observability-routes/
    README.md
    01-extract-observability-handlers.md
    02-register-observability-routes.md
    03-switch-observability-dispatch.md
  04.0-link-management-routes/
    README.md
    01-extract-read-preview-handlers.md
    02-extract-create-patch-delete-handlers.md
    03-extract-actions-stats-placeholders.md
    04-register-and-switch-canonical-routes.md
  05.0-compatibility-routes/
    README.md
    01-extract-compat-create-handler.md
    02-extract-compat-read-handler.md
    03-register-and-switch-compat-routes.md
  06.0-redirect-routes/
    README.md
    01-extract-prefixed-redirect-handler.md
    02-extract-root-redirect-handler.md
    03-register-redirect-routes-guarded.md
  07.0-fallback-routes/
    README.md
    01-extract-fallback-handler.md
    02-register-and-switch-fallback-routes.md
    03-cleanup-application-request-boundary.md
  08.0-openapi-docs-cleanup/
    README.md
    01-extend-route-metadata.md
    02-generate-api-reference-from-registry.md
    03-remove-obsolete-branch-chain.md
    04-update-public-docs-and-review.md
```

## Per-task detail

### Task 01.0 - Baseline characterization (✅)

**Delivered:**
- Endpoint matrix characterization covering every route in the inventory:
  `tests/unit/http/02_endpoint_matrix_characterization.cpp`.
- Wrong-method and validation-error characterization:
  `tests/unit/http/03_method_and_error_characterization.cpp`.
- Redirect and fallback characterization (active/disabled/expired/deleted
  links, root-redirect fallthrough, URI-store fallback):
  `tests/unit/http/04_redirect_fallback_characterization.cpp`.

**Tests / gate:**
- Included in the 148/148 passing full unit suite and the 29/29 passing
  HTTP/router subset.

### Task 02.0 - Router infrastructure (✅)

**Delivered:**
- `RouteContext` and `pathParam()`: `include/url_shortener/http/route_context.hpp`.
- Handler type aliases (`BeastRequest`, `BeastResponse`, `HandlerFn`):
  `include/url_shortener/http/handler_types.hpp`.
- Path-pattern matcher and dispatch: `include/url_shortener/http/router.hpp`,
  `src/http/router.cpp`.
- Route registration/build: `include/url_shortener/http/router_builder.hpp`,
  `src/http/router_builder.cpp`.
- Registry/router consistency checks exposing `Router::routes()` for test and
  documentation tooling.

**Tests / gate:**
- `tests/unit/http/05_route_context.cpp`, `06_router_matcher.cpp`,
  `07_router_dispatch.cpp`, `08_router_registry_consistency.cpp` - all part of
  the 29/29 passing HTTP/router subset.

### Task 03.0 - Observability routes (✅)

**Delivered:**
- `handleHealthz`, `handleReadyz`, `handleMetrics`:
  `include/url_shortener/http/handlers/observability_handlers.hpp`,
  `src/http/handlers/observability_handlers.cpp`.
- Registered in `src/http/router_builder.cpp`; `handleShortenerRequest()`
  dispatches `/healthz`, `/readyz`, `/metrics` through the router (first
  production router usage).

**Tests / gate:**
- `tests/unit/http/09_observability_handlers.cpp`; characterization suites
  from Task 01.0 continue to pass unchanged.

### Task 04.0 - Link management routes (✅)

**Delivered:**
- Canonical `/api/v1/links` handlers (read by id/slug, preview, create,
  patch, delete, enable/disable/restore, stats, QR/routing placeholders):
  `include/url_shortener/http/handlers/link_handlers.hpp`,
  `src/http/handlers/link_handlers.cpp`.
- All canonical routes registered in `src/http/router_builder.cpp` and
  dispatched through `Router`.

**Tests / gate:**
- `tests/unit/http/10_link_handlers.cpp` (consolidates the case lists
  originally proposed across three separate subtask test files - see Notes &
  decisions above).

### Task 05.0 - Compatibility routes (✅)

**Delivered:**
- `handleCompatCreateLink`, `handleCompatGetLinkBySlug`:
  `include/url_shortener/http/handlers/compatibility_handlers.hpp`,
  `src/http/handlers/compatibility_handlers.cpp`.
- `/api/v1/short-urls` create/read registered in `src/http/router_builder.cpp`
  and dispatched through `Router`.

**Tests / gate:**
- `tests/unit/http/11_compatibility_handlers.cpp`.

### Task 06.0 - Redirect routes (✅)

**Delivered:**
- `handlePrefixedRedirect`, `handleRootRedirect`:
  `include/url_shortener/http/handlers/redirect_handlers.hpp`,
  `src/http/handlers/redirect_handlers.cpp`.
- `GET /r/{slug}` and `GET /{slug}` registered and dispatched through
  `Router`; route labels `redirect_prefixed`/`redirect_root` preserved.

**Tests / gate:**
- `tests/unit/http/12_prefixed_redirect_handler.cpp`,
  `tests/unit/http/13_root_redirect_handler.cpp`.
- Security review:
  [docs/security/2026-07-17-redirect-route-refactor.md](/docs/security/2026-07-17-redirect-route-refactor.md)
  - Verdict: PASS.

### Task 07.0 - Fallback routes (✅)

**Delivered:**
- `handleFallbackUriStore`: `include/url_shortener/http/handlers/fallback_handlers.hpp`,
  `src/http/handlers/fallback_handlers.cpp` - wraps `handleApplicationRequest()`
  (Option A boundary, see Notes & decisions).
- Fallback `GET`/`POST`/`DELETE` for `/{path}` and `/` registered last in
  `src/http/router_builder.cpp`, after all more-specific routes.

**Tests / gate:**
- `tests/unit/http/14_fallback_handler.cpp`; redirect/fallback
  characterization suite continues to pass, proving fallback does not steal
  API or redirect targets.

### Task 08.0 - OpenAPI/docs cleanup (✅)

**Delivered:**
- Extended `RouteDescriptor` with `tags`, `operation_id`, `path_parameters`,
  `query_parameters`, `request_body_description`, `responses`,
  `compatibility_alias`, `placeholder`:
  `include/url_shortener/http/route_registry.hpp`,
  `src/http/route_registry.cpp`.
- Documentation generator: `tools/route_registry_dump.cpp`, producing
  `docs/api/README.md` from `registeredRoutes()`.
- Obsolete branch-chain removal:
  `handleShortenerRequest()` in `src/http/request_handlers.cpp` is now a
  one-line delegation to `applicationRouter().dispatch(req, config, is_tls)`.
- Security review passed for the overall route refactor:
  [docs/security/2026-07-17-rest-api-route-refactor.md](/docs/security/2026-07-17-rest-api-route-refactor.md)
  - Verdict: PASS.

**Tests / gate:**
- `tests/unit/http/01_route_registry.cpp`,
  `tests/unit/docs/02_route_registry_docs.cpp`.
- Full unit suite: 148/148 passing (`ctest -L unit`). HTTP/router subset:
  29/29 passing (`ctest -R "^0[1-9]_|^1[0-4]_" -L unit`). 254 tests configured
  overall (`ctest -N`).
