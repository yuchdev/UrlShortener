# 0006 Unified Command Model and TUI

## Background

Milestone 0003 gave the service a CLI next to REST, sharing one
`LinkCommandService` ([ADR 0001](/docs/adr/0001-cli-rest-shared-command-layer.md)).
Two further interfaces are planned - a terminal UI (this milestone) and
graphical admin front ends (web admin [0002](/docs/roadmap/0002-admin_console/plan.md),
public web UI [0004](/docs/roadmap/0004-web-ui/plan.md), and a Tauri admin
panel that has no milestone yet). With ADR 0001's shape each new interface
would add its own hand-written argument/error mapping.

This milestone therefore does three things, in dependency order:

1. **Defines the Command Model** ([ADR 0002](/docs/adr/0002-unified-command-model.md),
   [docs/commands/README.md](/docs/commands/README.md)): every external
   operation is one JSON command (or job) with one descriptor and one
   handler; REST, CLI, TUI, stdio and a future IPC are interchangeable
   transports over it.
2. **Closes the storage-path gap** left open by ADR 0001. The code shows the
   gap is larger than ADR 0001 describes: the pluggable backend
   (`BuildStorageAdapters`, `core::LinkService`) is used only by tests and
   `main` never reads a `StorageConfig`; redirects, REST and CLI all run on the
   legacy in-memory singletons. Task 02.0 therefore *wires* one configured
   backend into the live path for the first time (selected by
   `--storage-config`), so redirects, REST, CLI, TUI and stdio read and write
   the same store; it is not a "reconnect" of the redirect path.
3. **Delivers the TUI** ([ADR 0003](/docs/adr/0003-tui-framework-and-architecture.md),
   FTXUI) with every command reachable, DB table browsing, `full_URL ->
   short_URL` pair insert/import/export, and jobs.

It must complete before the web and Tauri admin work starts, because those
clients consume `POST /api/v1/commands`, `link.list`, `db.*` and the job
commands rather than inventing their own admin endpoints.

Questions that a headless session could not ask are in
[decision-log.md](/docs/roadmap/0006-unified-command-model-and-tui/decision-log.md).

## Roadmap review and placement

| Existing milestone | State | Effect of this milestone | Action |
|---|---|---|---|
| 0001 REST refactor | complete | New routes (`/api/v1/commands`, `/api/v1/jobs*`, `GET /api/v1/links`) are added via `RouteRegistry` | none |
| 0003 CLI & REST | complete (stale in README; fixed) | Its adapters are re-pointed at the registry, behavior unchanged | Task 04.0 |
| 0002 Admin console | not started | Task 02.0 (query foundation) and 04.0 (URL-pair list) overlap `link.list`/`db.rows`/pagination | Build 0002 on the registry; see below |
| 0004 Public web UI | not started | Uses `POST /api/v1/links` unchanged; no blocker. May optionally use the generic endpoint | none required |
| 0005 Windows secret backends | not started | Independent | none |
| (unplanned) Tauri admin panel | no spec | Consumes REST or a `serve-stdio` sidecar | Add as a later milestone after 0006 |

**Execution order:** `0006` -> then `0002` and `0004` (parallelizable) -> Tauri.
`0005` is independent and may run anytime. Milestone numbers are identifiers,
not order (0006 runs before 0002/0004); the roadmap README states this.

### Impact on other milestones (applied in Task 08.0 subtasks 14-15)

- **0002 C3/C4 (pagination and error format):** the admin API
  (`/admin/api/v1/*`) becomes a *projection* of the registry. `page`/`page_size`
  map to `offset`/`limit`; command error codes map to 0002's vocabulary
  (`not_found`->`not_found`, `invalid_*`->`validation_error`,
  `slug_conflict`->`conflict`, `not_authorized`->`not_authorized`). 0002 keeps
  its own cookie/CSRF session model (C2) and its own analytics endpoints; only
  link/pair browsing and mutation reuse commands. 0002 Task 02.0 subtask 01
  ("backend query primitives") is reduced to the projection layer.
- **0002 Task 04.0 (URL pair list/analytics)** calls `link.list` / `link.stats`.
- **0002 Task 07.0** adds `auth.*`/`user.*` descriptors (permission
  `manage_users`) to the same registry.
- **0004** needs no change; its SC5 public-create rate limiting, if built, is
  applied in the **REST adapter** (the `POST /api/v1/links` resource route and,
  for `link.create`, the generic `POST /api/v1/commands` bridge), keyed by the
  HTTP client; it is not part of the `link.create` command handler, because the
  local transports (CLI, TUI, stdio) are the operator and must not be throttled
  (open decision, decision log).

## Tasks

| Task | Name | Category | Output |
|------|------|----------|--------|
| [01.0](/docs/roadmap/0006-unified-command-model-and-tui/01.0-command-model-core/README.md) | Command model core | Infrastructure | Envelope, `ArgSpec`/`CommandDescriptor`, `CommandRegistry::execute`, catalog generator, `command.list` |
| [02.0](/docs/roadmap/0006-unified-command-model-and-tui/02.0-storage-path-unification/README.md) | Storage path unification | Prerequisite | One configured backend for redirects, REST, CLI, TUI; cross-process CLI works |
| [03.0](/docs/roadmap/0006-unified-command-model-and-tui/03.0-link-and-browse-commands/README.md) | Link and browse commands | Commands | Nine link commands on the registry; `link.list`, `db.tables`, `db.rows`, `system.info`; extended list ports |
| [04.0](/docs/roadmap/0006-unified-command-model-and-tui/04.0-rest-and-cli-adapters/README.md) | REST and CLI adapters | Adapters | Resource routes via registry; `POST /api/v1/commands`; CLI `cmd`; flags compiled from `ArgSpec` |
| [05.0](/docs/roadmap/0006-unified-command-model-and-tui/05.0-jobs-pairs-and-stdio/README.md) | Jobs, pair import/export, stdio | Commands + adapter | `JobRunner`, `job.*`, `link.import/export`, `jobs` table, process-wide job runtime in `build_command_runtime`, `ITransportAdapter`, `serve-stdio`, IPC how-to |
| [06.0](/docs/roadmap/0006-unified-command-model-and-tui/06.0-tui/README.md) | Terminal UI | Interface | FTXUI TUI per ADR 0003 diagrams D1-D12 |
| [07.0](/docs/roadmap/0006-unified-command-model-and-tui/07.0-tests-and-conformance/README.md) | Tests and conformance | Testing | Cross-transport conformance corpus, TUI snapshots, security/regression tests |
| [08.0](/docs/roadmap/0006-unified-command-model-and-tui/08.0-docs-and-registry-sync/README.md) | Docs and registry sync | Cleanup | ADR acceptance, TUI guide, CLI/API docs, 0002/0004 reconciliation |

## Architecture

```text
 REST        CLI            TUI           serve-stdio       [IPC / gRPC ...]
  |           |              |                |                  |
  +---------- ITransportAdapter (frame in / call / frame out) ---+
                              |
                    CommandRequest / CommandResponse   (JSON envelope v1)
                              |
                     CommandRegistry::execute
              validate -> authorize (AccessGuard) -> ArgSpec check
                              |                 \
                      command handler          JobRunner (kind=job)
                              |
        LinkCommandService | BrowseService | PairService
                              |
   IMetadataRepository  ICacheStore  IClickEventRepository  (ports, one backend)
```

Full rationale, envelope, alternatives: [ADR 0002](/docs/adr/0002-unified-command-model.md).
TUI structure and all screen diagrams: [ADR 0003](/docs/adr/0003-tui-framework-and-architecture.md).

## Shared contracts (authoritative)

**C1. One definition, one implementation.** Each external operation exists as
exactly one `CommandDescriptor` + one handler. No transport may contain
per-command business logic, DTO construction or error mapping.

**C2. Envelope is the only wire shape.** `CommandRequest`/`CommandResponse`
v1 as in [docs/commands/README.md](/docs/commands/README.md). Additive changes
do not bump `v`.

**C3. Existing REST bodies and statuses do not change.** The nine link
commands' `result` equals today's REST bodies (same serializers); resource
routes keep per-field 400 codes. Characterization tests stay green throughout.

**C4. Existing CLI behavior does not change.** `link <verb>` flags, stdout and
exit codes are preserved; `cmd` is additive.

**C5. No listener in CLI/TUI/stdio modes.** None of `cmd`, `link`, `tui`,
`serve-stdio` constructs `HttpServer` or binds a socket; none touches
`uri.txt`/`UriMapSingleton`.

**C6. Secrets never leave through commands.** `db.*` is allowlist-based
(`links`, `click_events`, `jobs`); `system.info` emits only allowlisted
backend names and redacts anything else with `::observability::redactSecretValue`
(`storage_observability.hpp`); raw client identifiers are never returned
(`client_id_hash` only); audit lines carry no argument values.

**C7. Authorization lives in the registry.** Permission per descriptor,
enforced by `AccessGuard` inside `execute`; adapters only supply
`CallerContext`. Auth broker stays local-only. Until Milestone 0002 adds
sessions, the new HTTP surface (`/api/v1/commands`, `GET /api/v1/links`,
`/api/v1/jobs*`) is off by default (`--command-api-enabled`), runs with one
configured role (`--command-api-role`, default `user`; `admin` is honored
only for loopback peers, everyone else runs as `user`) and never runs
`manage_users` commands.

**C8. The TUI talks only to `ICommandClient`.** No service, repository or DTO
header under `src/tui/`.

**C9. No new JSON library, C++17 only, new `.cpp` in `sources.cmake`.** FTXUI
is the only new third-party dependency and is optional at build time. The one
sanctioned JSON tokenizer is the bounded top-level scanner of Task 01.0
subtask 01 (`commands/json_scan.hpp`); no other product code may add a JSON
parser (the TUI reuses it; the conformance `mini_json` is test-only).

**C10. Redirect fast path: behavior and complexity unchanged; the backing
object changes.** `GET /{slug}` and `GET /r/{slug}` stay cache lookup +
repository fallback + response, with the same responses. Task 02.0 changes
*which object* answers the lookup (`LinkBackend::store()`) and replaces the
per-redirect whole-record stats write with an in-memory counter update; any
persistence of counters runs on a background timer, never inside a request.

**C11. One composition root.** Every process mode (server, `cmd`, `link`
sugar, `tui`, `serve-stdio`) obtains its registry from
`composition::build_command_runtime` (Task 03.0 subtask 18, extended for jobs
by Task 05.0 subtask 20): one process-wide `JobRunner`, one audit sink, one
set of registered commands. No mode builds its own registry.

## Explicitly out of scope

| Item | Why |
|---|---|
| Web admin UI, public web UI, Tauri shell | Milestones 0002, 0004, future; they consume this milestone |
| Implementing IPC | Seam + worked example only (stdio proves the pattern) |
| Durable jobs, multi-user TUI sessions | Non-goals in ADR 0002/0003 |
| Auth/user-management commands | Reserved for 0002 Task 07.0 |
| Commands for redirects, observability, compatibility aliases, URI-store fallback | Same exclusions as ADR 0001 |
| Fingerprint/IP analytics screens in the TUI | Owned by 0002 |

## Dependency graph

```text
01.0 command-model-core ----+
                            +--> 03.0 link-and-browse-commands --+--> 04.0 rest-and-cli-adapters
02.0 storage-unification ---+                |                     |
                                             +--> 05.0 jobs-pairs-and-stdio
                                                         |
                       03.0 + 05.0 ----------------------+--> 06.0 tui
                                                         |
                    07.0 tests-and-conformance (incremental, final gate after 06.0)
                                                         |
                                                   08.0 docs-and-registry-sync
```

01.0 and 02.0 run in parallel. 04.0 and 05.0 can overlap after 03.0. The TUI
does not need 04.0. Cross-task edges inside this overlap: `04.0/13` (`cmd`
job waiting) and `06.0/19` (TUI launch) need `05.0/20` (process-wide job
runtime); `05.0/11` (REST job routes) needs `04.0/02`, `04.0/08` and `04.0/09`.
The subtask-level `Depends on` / `Blocks` headers are authoritative and are
kept symmetric (checked mechanically before merge).

## File map

```text
include/url_shortener/commands/   json_scan, envelope, arg_spec, command_registry, audit, job_runner, transport_adapter, link_commands
src/commands/                     registry, handlers (link, browse, system, pair, job)
src/composition/                  command_runtime (the one registry builder), job_runtime, transport_factory
src/transport/                    stdio_adapter (REST/CLI adapters stay in src/http, src/cli)
src/app/repository_link_store.cpp new ILinkStore over IMetadataRepository + ICacheStore
src/tui/                          app, router, in_process_client, async_caller, form_model/form_view, table_model/table_view, palette, screens/ (Task 06.0 README: 20 subtasks)
tests/unit/commands/  tests/unit/tui/  tests/conformance/commands/
docs/adr/0002-*.md  docs/adr/0003-*.md  docs/commands/  docs/tui/
```

## Test file prefixes and CMake list ownership

Test files keep the repo rule "numeric prefix, unique within the folder".
Each task owns a prefix range per folder so concurrent tasks never pick the
same number (existing files keep their numbers):

| Folder | Existing | 01.0 | 02.0 | 03.0 | 04.0 | 05.0 | 06.0 | 07.0 |
|---|---|---|---|---|---|---|---|---|
| `tests/unit/commands/` | - | 01-19 | - | 20-39 | 40-49 | 50-69 | - | 70-89 |
| `tests/unit/app/` | 01-18 | - | 19-39 | 40-59 | - | 60-69 | - | (uses existing) |
| `tests/unit/cli/` | 01-04 | - | 05 | - | 06-19 | 20-29 | 30-39 | 40-49 |
| `tests/unit/http/` | 01-14 | - | 30-39 | 10 (existing, extended) | 15-29 | 40-49 | - | (extends existing) |
| `tests/integration/cli/` | 01-16 | - | 17-19 | - | 20-29 | 40-49 | - | (extends existing) |
| `tests/unit/tui/` | - | - | - | - | - | - | 01-19 (= subtask number) | 20-69 |
| `tests/unit/docs/` | 01-02 | (extends 02) | - | - | - | - | - | 03-09 |
| `tests/unit/storage/*` | per folder | - | next free per folder | `listing/01-09`, next free per folder | - | - | - | - |
| `tests/conformance/` | - | - | - | - | - | - | - | all (`runners/`, `py/`) |

| CMake list (`sources.cmake`) | Owner (creates list + registration) | Registration | Target name |
|---|---|---|---|
| `COMMANDS_UNIT_SOURCES` | Task 01.0 subtask 01 | `register_labeled_cpp_tests(COMMANDS_UNIT_SOURCES "unit")` | `commands__<stem>` |
| `LISTING_UNIT_SOURCES` | Task 03.0 subtask 01 | `register_labeled_cpp_tests(... "unit")` | `listing__<stem>` |
| `TUI_MODEL_UNIT_SOURCES`, `TUI_RENDER_UNIT_SOURCES` | Task 06.0 subtask 01 | `register_tui_tests(...)` inside `if(URLSHORTENER_ENABLE_TUI)` | `tui__<stem>` |
| `CONFORMANCE_CPP_SOURCES`, `CONFORMANCE_PY_SOURCES` | Task 07.0 subtasks 03 and 05 | dedicated loops | `conformance__<stem>` / `conformance_<stem>` |
| `PERF_SOURCES` | Task 07.0 subtask 30 | `register_labeled_cpp_tests(PERF_SOURCES "perf")` + `RUN_SERIAL` | `<parent_dir>__<stem>` (e.g. `analytics__...`, `redirect__...`) |
| existing lists (`APP_UNIT_SOURCES`, `CLI_UNIT_SOURCES`, `HTTP_UNIT_SOURCES`, ...) | unchanged | unchanged | unchanged |

Other tasks only *append* to these lists. Note: test source lists live in
`sources.cmake` (not `CMakeLists.txt` as CLAUDE.md says; corrected by Task 08.0
subtask 06); `CMakeLists.txt` only registers them.
