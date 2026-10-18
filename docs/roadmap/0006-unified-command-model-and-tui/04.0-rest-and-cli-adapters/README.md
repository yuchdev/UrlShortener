# Task 04.0 - REST and CLI adapters

**Parent milestone:** [Milestone 0006 - Unified Command Model and TUI](/docs/roadmap/0006-unified-command-model-and-tui/plan.md)
**Status:** ⬜ Not started
**Depends on:** [03.0](/docs/roadmap/0006-unified-command-model-and-tui/03.0-link-and-browse-commands/README.md) - specifically 03.0/11 (nine link commands registered, REST parity gate), 03.0/14 (`link.list`) and 03.0/18 (`build_command_runtime`); [01.0](/docs/roadmap/0006-unified-command-model-and-tui/01.0-command-model-core/README.md) (envelope, error table with legacy REST columns, `execute_json`); soft: [02.0](/docs/roadmap/0006-unified-command-model-and-tui/02.0-storage-path-unification/README.md) (`--storage-config` for cross-process CLI) and [05.0](/docs/roadmap/0006-unified-command-model-and-tui/05.0-jobs-pairs-and-stdio/README.md) subtask 20 (process-wide job runtime; job waiting in `cmd`, subtask 13)

## Scope

Re-point the two existing transports at the registry and add the generic entry points:

- **REST:** the ten resource routes of `/api/v1/links` call the registry through one bridge (statuses, bodies and per-field error codes unchanged); new
  `GET /api/v1/links` (list), `POST /api/v1/commands` (envelope in / envelope out) and `GET /api/v1/commands` (descriptor list), all three behind the opt-in
  switch `--command-api-enabled` and running with `--command-api-role` (subtask 08).
- **CLI:** new `url_shortener cmd <name> [--json|--arg ...]`; the nine `link <verb>` sugar commands execute through the registry with identical output and exit codes.
- The `/api/v1/jobs*` routes belong to Task 05.0 (subtask 11) and reuse this task's bridge and gate.

### Verified findings that shaped the specs

| Finding | Consequence |
|---|---|
| `src/http` has no authentication or `AccessGuard`; resource routes are open. | Pre-existing resource routes run as `ControlSet::Admin` (status quo, C3). All **new** HTTP surface (`GET /api/v1/links`, `/api/v1/commands`, `/api/v1/jobs*`) is **disabled by default**, runs with the role from `--command-api-role` (default read-only `user`; `admin` is honored only for loopback peers because the listeners bind all IPv4 interfaces, other peers run as `user`), and the bridge refuses `manage_users` commands; `POST /api/v1/commands` also requires `Content-Type: application/json` (blocks drive-by cross-origin form posts). Subtasks 02, 07, 08, 09. Needs a security review; flagged in the decision log. |
| `makeCommandService(config)` builds a bundle per request around the request's `ServerConfig&`; unit tests use differing configs. | The REST bridge builds a runtime per request (subtask 02); caching is a later optimization. The `JobRunner` is therefore process-wide: Task 05.0 subtask 20 holds it outside the per-request runtime. **Follow-up risk:** every management-plane request then pays for building a `LinkCommandService`, lister, `TableCatalog` and a registry of ~20 descriptors (heap allocations, `std::function` copies). It never touches the redirect fast path (C10), but it is a latency/allocation regression for `/api/v1/links*`; no 0006 subtask measures it (07.0/32 guards only the redirect path), so measure it with the `docs/benchmarking.md` tooling before the generic API is used at high rates, and if it matters a follow-up caches one runtime per distinct config (keyed on the fields `build_command_runtime` reads). Not in 0006 scope. |
| REST strictness differs from the registry: unknown JSON keys are ignored, unknown keys inside `campaign` are ignored, `extractJson*` scan text loosely, per-field 400 codes (`invalid_redirect_type`, ...) precede the service, PATCH checks existence first. | The REST handlers keep their ordered per-field checks and rebuild `args` from the known keys only (subtasks 04-05). |
| No session reads `remote_endpoint()`; `RouteContext` has no peer; nothing logs the peer address. | Subtask 09 threads `std::optional<boost::asio::ip::address> peer` (default `nullopt` = non-loopback) through `handleShortenerRequest`, `Router::dispatch` and `RouteContext`; existing call sites compile unchanged. Behind a reverse proxy every peer is loopback; `X-Forwarded-For` is not honoured (D4). |
| One-shot modes never initialize logging, and an uninitialized logger writes to stderr; the runtime's audit sink would add stderr lines (C4). | `cmd` and `link <verb>` initialize logging explicitly as disabled unless `cmd --log-file`/`--verbose` (subtasks 12, 13, 15; decision log D14). |
| `routeLabelForTarget` would label `/api/v1/commands` as `redirect_root`; test `01_route_registry` hard-codes the label set. | New label `api_commands` (also used by `/api/v1/jobs*`), one-line test edit (subtask 09). |
| `ArgSpec` caps (2048 chars / 64 items by default) are tighter than what REST accepts today (no URL length limit). | Caps for the nine commands are raised in 03.0/07 so the service stays the validator and REST bodies do not change. |
| `link stats`: REST has range mode and a lifetime mode, `from`/`to` are epoch seconds, `bucket` errors use the codes `invalid_timestamp` / `invalid_bucket`. | `link.stats` keeps `bucket` a free string whose documented values are `hour|day|week` (03.0/08); the bridge re-promotes those two details to REST codes (subtask 02). |
| `link <verb>` flags have quirks (`--expires-at clear`, `--clear-campaign`, comma lists, six `--campaign-*`) that `ArgSpec` cannot express. | Flags stay hand-parsed into the existing DTOs; DTOs are converted to `args`, and an ArgSpec coverage test prevents drift (subtask 14). ADR 0002's wording "flags compiled from ArgSpec" applies fully only to `cmd --arg` (subtask 11). |
| `docs/api/README.md` is generated by `route_registry_dump` from `registeredRoutes()`. | Regenerated once in subtask 10; no hand edits. |

### Previous numbering (for references in other documents)

| Old (before the split) | New |
|---|---|
| 04.0/01 Resource routes through the registry | 02 (bridge), 03 (reads), 04 (create), 05 (patch), 06 (delete + lifecycle + cleanup) |
| 04.0/02 Generic command endpoints (incl. `GET /api/v1/links`) | 07 (list route), 08 (gate), 09 (POST commands), 10 (GET commands + docs) |
| 04.0/03 CLI `cmd` and flag compilation | 11 (`--arg` coercion), 12 (parsing), 13 (dispatch), 14 (converters), 15 (`link` via registry) |

Other tasks that cited "04.0/02" (Task 05.0/11, REST job routes) need the bridge and gate: subtasks 02, 08, 09. Citations of "04.0/03" for the `cmd` wait helper and the shared registry
factory mean subtask 13 and `03.0/18` (`build_command_runtime`) respectively.

## Subtasks

| # | Document | Status | Depends on | Blocks |
|---|----------|--------|------------|--------|
| 01 | [Args object builder](01-args-object-builder.md) | ⬜ Not started | 01.0/01 | 02, 03, 04, 05, 06, 07, 11, 14 |
| 02 | [REST command bridge](02-rest-command-bridge.md) | ⬜ Not started | 01, 01.0/02, 01.0/07, 03.0/18 | 03, 04, 05, 06, 07, 09, 05.0/11 |
| 03 | [REST read routes through the registry](03-rest-read-routes-via-registry.md) | ⬜ Not started | 01, 02, 03.0/08, 03.0/11 | 06 |
| 04 | [REST create through the registry](04-rest-create-via-registry.md) | ⬜ Not started | 01, 02, 03.0/11 | 06 |
| 05 | [REST PATCH through the registry](05-rest-patch-via-registry.md) | ⬜ Not started | 01, 02, 03.0/09, 03.0/11 | 06 |
| 06 | [REST delete and lifecycle through the registry, handler cleanup](06-rest-delete-and-lifecycle-via-registry.md) | ⬜ Not started | 01, 02, 03, 04, 05, 03.0/10 | 07, 09 |
| 07 | [GET /api/v1/links route (gated)](07-rest-list-links-route.md) | ⬜ Not started | 01, 02, 06, 08, 03.0/14, 03.0/18 | 10 |
| 08 | [Command API configuration gate](08-command-api-config-gate.md) | ⬜ Not started | 01.0/06 | 07, 09, 10, 05.0/11 |
| 09 | [POST /api/v1/commands](09-rest-post-commands-endpoint.md) | ⬜ Not started | 02, 06, 08, 01.0/03, 01.0/04, 01.0/07, 03.0/18 | 10, 05.0/11, 07.0/08 |
| 10 | [GET /api/v1/commands and API reference regeneration](10-rest-get-commands-and-api-docs.md) | ⬜ Not started | 07, 08, 09, 01.0/10 | 07.0/17, 08.0/09 |
| 11 | [CLI `--arg` coercion](11-cli-arg-coercion.md) | ⬜ Not started | 01, 01.0/05 | 13 |
| 12 | [`cmd` parsing](12-cli-cmd-parsing.md) | ⬜ Not started | 01.0/03, 02.0/10 | 13 |
| 13 | [`cmd` dispatch](13-cli-cmd-dispatch.md) | ⬜ Not started | 11, 12, 01.0/04, 01.0/07, 01.0/11, 03.0/18 | 15 |
| 14 | [`link` flags to `args` converters and coverage test](14-cli-link-args-json-converters.md) | ⬜ Not started | 01, 03.0/11 | 15 |
| 15 | [`link <verb>` through the registry](15-cli-link-verbs-via-registry.md) | ⬜ Not started | 13, 14, 01.0/02, 01.0/04, 03.0/18 | none |

Dependency mini-graph (`->` = must finish first):

```text
01 args builder -+--------------------------------------------+
                 |                                            |
02 bridge -------+-> 03 reads --+                              |
   (03.0/18)     +-> 04 create -+-> 06 delete/lifecycle+cleanup-+-> 07 list route --+
                 +-> 05 patch --+              |                                    |
                                               +-> 09 POST /commands <- 08 gate     +-> 10 GET /commands + docs
                                                       (08 -> 10 too) -------------+

11 --arg coercion -+
12 cmd parsing ----+-> 13 cmd dispatch -+
01 -> 14 converters ---------------------+-> 15 link verbs via registry
```

REST (01-10) and CLI (11-15) halves are independent after 01 and can be developed in parallel. 03/04/05 can run in parallel after 02; 06 closes them.

## Key constraints

- C3: existing resource routes keep their exact statuses, bodies and per-field 400 codes. Each REST subtask writes its characterization test **before** editing the handler and leaves the pre-existing tests unedited (the only edit to an existing test is the one-string label addition in `01_route_registry.cpp`, subtask 09).
- C4: `link <verb>` flags, stdout, stderr text and exit codes are unchanged; `cmd` is additive.
- C5: CLI modes never construct `HttpServer`, bind a socket or touch `uri.txt`/`UriMapSingleton` (grep checks in 13 and 15).
- C7: adapters only supply `CallerContext`; permissions are enforced inside `execute`. Generic REST endpoints are opt-in and read-only by default.
- C1/C9: no JSON library; request `args` are assembled only with `commands::args_builder`; all HTTP error mapping lives in `command_bridge.cpp`; all exit codes come from the 01.0/02 table.
- New routes are registered in both `RouterBuilder` and `registeredRoutes()` (consistency test), and `docs/api/README.md` is regenerated, not hand-edited.
- Every new `.cpp` is added to `COMMON_CPP_SOURCES` in `sources.cmake`; new tests go into the matching source list (`HTTP_UNIT_SOURCES` for plain-stem targets such as `17_rest_create_via_registry`, `CLI_UNIT_SOURCES` for `cli__NN_*`, `COMMANDS_UNIT_SOURCES` for `commands__NN_*`, `CLI_INTEGRATION_SOURCES` for `cli_NN_*`).
- Test numbering (plan.md "Test file prefixes"): `tests/unit/http` 15-29 (uses 15-23), `tests/unit/cli` 06-19 (uses 06-11), `tests/integration/cli` 20-29 (uses 20-21), `tests/unit/commands` 40-49 (uses 40).

## Exit criteria

1. `ctest --test-dir cmake-build -L unit --output-on-failure` and `-L integration` are green; all pre-existing HTTP and CLI characterization tests pass unmodified (apart from the single label edit).
2. `grep -n "AppErrorCode\|LinkCommandService\|makeCommandService" src/http/handlers/link_handlers.cpp src/cli/link_command_dispatch.cpp` is empty.
3. `url_shortener cmd link.create --json '{"target_url":"https://example.com/a","slug":"a"}'` prints an envelope whose `result` equals the body of `POST /api/v1/links` for the same input (modulo id/timestamps), and `link create` prints the same `result`.
4. `POST /api/v1/commands` is a 404 by default, 415 without a JSON content type, 403 for writes under the default role, and works end to end under `--command-api-enabled true --command-api-role admin`.
5. `docs/api/README.md` lists `GET /api/v1/links`, `GET /api/v1/commands`, `POST /api/v1/commands`, and its existing rows are byte-identical.
