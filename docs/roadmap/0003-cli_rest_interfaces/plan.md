# 0003 CLI & REST Command Interfaces

## Background

The service today exposes exactly one interface: HTTP, dispatched through
`Router`/`RouterBuilder` against the route inventory in
`include/url_shortener/http/route_registry.hpp` /
`src/http/route_registry.cpp` (`registeredRoutes()`), generated to
[`docs/api/README.md`](/docs/api/README.md). That generated file is the
single, authoritative list of every REST endpoint and is used below as the
reference list of commands this milestone must also expose over a CLI.

Two gaps block a CLI mode today:

1. **No CLI entrypoint exists.** `CliParser`/`ParseResult`
   (`include/url_shortener/cli/cli_parser.h`, `src/cli_parser.cpp`) parses
   only server-startup flags (ports, TLS, storage, analytics, request
   limits) into `ServerConfig`. `src/main.cpp` unconditionally constructs
   `HttpServer` and calls `io_context.run()` - there is no branch that runs a
   single command and exits.
2. **The shared command layer is incomplete.** `app::LinkCommandService`
   (`include/url_shortener/app/link_command_service.hpp`,
   `src/app/link_command_service.cpp`) already gives `CreateLink`, `GetLink`,
   and `GetLinkStats` a transport-agnostic `Config in -> Result<View> out`
   shape, and `src/http/handlers/link_handlers.cpp` already calls it for
   those three operations via `BuildLegacyLinkCommandService()`
   (`include/url_shortener/composition/link_command_service_factory.hpp`).
   But `handlePatchLink`, `handleDeleteLink`, `handleLifecycleAction`
   (enable/disable/restore), and `handlePreviewLink` in that same file bypass
   the command service entirely and mutate links directly via
   `url_shortener::getLinkForRead()` /
   `url_shortener::updateLinkAndInvalidateCache()`
   (`include/url_shortener/storage/link_repository.h`). A CLI adapter cannot
   reuse that inline logic without duplicating it.

**Correction to prior CLI notes.** `docs/testing/cli_link_commands.md` and
`docs/testing/testplans/cli.md` describe an unimplemented CLI design that
assumes CLI mode loads/saves `uri.txt`. That is incorrect: `uri.txt` is
`UriMapSingleton`'s legacy generic key-value store, read/written only by
`src/main.cpp` for the **Fallback** routes (`GET`/`POST`/`DELETE /{path}` and
`/`) - it has nothing to do with `Link` records, which live behind
`linkRepository()` (the process-global `InMemoryMetadataRepository` singleton
in `include/url_shortener/storage/link_repository.h`). Task 06.0 below
corrects these documents; no CLI code in this milestone touches `uri.txt`.

**Known pre-existing inconsistency, out of scope.** `LinkService::Resolve`
(redirect fast path, `include/url_shortener/core/link_service.hpp`) reads
through the *pluggable* `IMetadataRepository`
(`include/url_shortener/storage/i_metadata_repository.hpp`, selected via
`StorageConfig`/`BuildStorageAdapters` - SQL/Redis/in-memory), while
`LinkCommandService`'s `LegacyLinkStore`
(`include/url_shortener/app/legacy_adapters.hpp`) reads/writes the older,
always-in-memory `linkRepository()` global singleton. These are two
different storage paths under the same interface name. This milestone does
**not** unify them - it only completes and mirrors the command surface that
already sits on the legacy singleton, exactly as `link_handlers.cpp` does
today. Reconciling the two storage paths is a separate, pre-existing
architectural question tracked outside this milestone.

## Architecture

Every command becomes exactly three pieces, cleanly separated by
responsibility:

```text
argv  --CliParser-->   \                                    /--> HTTP status + JSON body  (REST response)
                         >-- {Command|Query} DTO --> LinkCommandService::<Verb> --<
HTTP request --handler-> /                                    \--> JSON stdout + exit code  (CLI output)
```

- **Config DTOs** (`app::CreateLinkCommand`, `app::GetLinkQuery`,
  `app::GetLinkStatsQuery`, and the four new DTOs added in Task 01.0) are
  plain structs with no Beast, Boost.ProgramOptions, or I/O dependency. The
  REST adapter and the CLI adapter both parse their respective input format
  into the *same* DTO type - this is what "identically designed config"
  means concretely: one struct definition, two parsers.
- **Command handlers** (`app::LinkCommandService` methods) contain all
  business logic - validation, storage calls, cache invalidation - and are
  the only place a new rule is implemented once for both interfaces. They
  already return `Result<T>` (value or `AppError`), never throw for expected
  conditions, matching the storage-layer convention in `CLAUDE.md`.
- **Transport adapters** are thin: the REST adapter (existing
  `src/http/handlers/link_handlers.cpp`) maps DTO results to HTTP
  status/JSON via the existing `statusForAppError`/`codeForAppError`
  pattern; the new CLI adapter maps the same `Result<T>` to a JSON-stdout /
  exit-code / stderr-message contract (Task 04.0).

This is the same ports-and-adapters discipline already used for storage
backends (`IMetadataRepository`, `ICacheStore`, ...) in `CLAUDE.md`, applied
one layer up between HTTP and argv.

This DTO-sharing decision - including the explicit non-goals restated in the
Explicitly-out-of-scope table below - is formalized in
[ADR 0001 - CLI/REST Shared Command Layer](/docs/adr/0001-cli-rest-shared-command-layer.md).

## Command inventory (reference: `docs/api/README.md`, generated from `route_registry.cpp`)

| REST endpoint | `operation_id` | Shared command today? | CLI command (this milestone) |
|---|---|---|---|
| `POST /api/v1/links` | `post_api_v1_links` | ✅ `LinkCommandService::CreateLink` | `link create` |
| `GET /api/v1/links/{slug}` | `get_api_v1_links_slug` | ✅ `LinkCommandService::GetLink` | `link get --slug <slug>` |
| `GET /api/v1/links/id/{id}` | `get_api_v1_links_id_id` | ✅ `LinkCommandService::GetLink` | `link get --id <id>` |
| `PATCH /api/v1/links/{slug}` | `patch_api_v1_links_slug` | ❌ inline in `handlePatchLink` | `link update` |
| `DELETE /api/v1/links/{slug}` | `delete_api_v1_links_slug` | ❌ inline in `handleDeleteLink` | `link delete` |
| `POST /api/v1/links/{slug}/enable` | `post_api_v1_links_slug_enable` | ❌ inline in `handleLifecycleAction` | `link enable` |
| `POST /api/v1/links/{slug}/disable` | `post_api_v1_links_slug_disable` | ❌ inline in `handleLifecycleAction` | `link disable` |
| `POST /api/v1/links/{slug}/restore` | `post_api_v1_links_slug_restore` | ❌ inline in `handleLifecycleAction` | `link restore` |
| `GET /api/v1/links/{slug}/preview` | `get_api_v1_links_slug_preview` | ❌ inline in `handlePreviewLink` | `link preview` |
| `GET /api/v1/links/{slug}/stats` | `get_api_v1_links_slug_stats` | ✅ `LinkCommandService::GetLinkStats` | `link stats` |

**Explicitly out of scope**, with reasons, so the boundary is not
accidentally widened while implementing:

| REST group | Endpoints | Why no CLI form |
|---|---|---|
| Observability | `GET /healthz`, `/readyz`, `/metrics` | Report the state of a *running* process; CLI mode never starts one. |
| Compatibility | `POST /api/v1/short-urls`, `GET /api/v1/short-urls/{slug}` | HTTP-only aliases kept for legacy callers of the old API shape; CLI users get `link create`/`link get` directly, there is no legacy CLI to stay compatible with. |
| Redirects | `GET /r/{slug}`, `GET /{slug}` | The protected fast path (`CLAUDE.md`: "Redirect fast path is protected"); not a management command. |
| Fallback | `GET`/`POST`/`DELETE /{path}`, `/` | Legacy generic URI-store, unrelated to the `Link` domain model; a documented current limitation, not part of the command surface being unified here. |
| Placeholders | `GET /api/v1/links/{slug}/qr`, `/routing` | Return `501 feature_not_enabled` today; nothing to expose over a second interface until the feature itself ships. |

## Tasks

| Task | Name | Category | Output |
|------|------|----------|--------|
| 01.0 | [Command layer completion](/docs/roadmap/0003-cli_rest_interfaces/01.0-command-layer-completion/README.md) | Refactor | `UpdateLinkCommand`, `DeleteLinkCommand`, `SetLinkEnabledCommand`, `RestoreLinkCommand` DTOs and matching `LinkCommandService` methods; `link_handlers.cpp` migrated onto them with zero response-shape change. |
| 02.0 | [CLI argument parsing](/docs/roadmap/0003-cli_rest_interfaces/02.0-cli-argument-parsing/README.md) | Infrastructure | `ParseResult` carries an optional command variant; `CliParser` recognizes `link <verb> [flags]` and produces the same DTOs as Task 01.0/existing REST handlers, without changing any existing server-flag parsing. |
| 03.0 | [CLI dispatch and process lifecycle](/docs/roadmap/0003-cli_rest_interfaces/03.0-cli-dispatch-and-lifecycle/README.md) | Infrastructure | `main.cpp` branches to a one-shot command path before constructing `HttpServer`/`io_context`; no listener is ever bound in CLI mode. |
| 04.0 | [CLI output and error contract](/docs/roadmap/0003-cli_rest_interfaces/04.0-cli-output-and-error-contract/README.md) | Infrastructure | One shared success-JSON and error/exit-code convention used by every `link <verb>` subcommand, reusing `serializeLinkViewJson`/`serializeLinkStatsJson` rather than re-serializing. |
| 05.0 | [Tests](/docs/roadmap/0003-cli_rest_interfaces/05.0-tests/README.md) | Testing | Unit coverage for the four new command methods and the CLI parser; integration/e2e coverage extended from the existing `link create`/`get`/`stats` suites to all ten commands; REST characterization tests proving Task 01.0 changed no observable behavior. |
| 06.0 | [Docs and registry sync](/docs/roadmap/0003-cli_rest_interfaces/06.0-docs-and-registry-sync/README.md) | Cleanup | `docs/testing/cli_link_commands.md`/`testplans/cli.md` corrected (no more `uri.txt`), a generated-or-maintained CLI command reference cross-linked with `docs/api/README.md`, and a design note recording the DTO-sharing decision plus the explicit non-goals above. |

## Shared contracts (authoritative)

Cross-cutting rules every task must honor:

**C1. One DTO per command, shared by both adapters.** No REST handler and no
CLI handler may hand-roll its own struct for a command already covered by a
`LinkCommandService` DTO; if a field is missing, add it to the DTO, not to a
one-off adapter-local type.

**C2. `LinkCommandService` remains transport-agnostic.** No Beast type
(`BeastRequest`/`BeastResponse`), no Boost.ProgramOptions type, and no
`argv`/`argc` may appear in `include/url_shortener/app/` or
`src/app/link_command_service.cpp`.

**C3. REST response shapes do not change.** Task 01.0 must not alter any
status code, JSON field, or error code currently produced by
`handlePatchLink`, `handleDeleteLink`, `handleLifecycleAction`, or
`handlePreviewLink` - it only relocates the logic behind
`LinkCommandService`. Prove this with characterization tests captured before
the migration (Task 05.0, subtask 01 of Task 01.0 in practice: write the
lock-in test, then migrate).

**C4. CLI mode never starts the server.** No code path reachable from a
recognized `link <verb>` command may construct `HttpServer`, bind a
listener, or call `io_context.run()`. Verified by the existing e2e pattern in
`13_cli_no_server_socket.sh`, extended to every new command.

**C5. `uri.txt` / `UriMapSingleton` is untouched by CLI mode.** Per the
Background correction above; CLI command dispatch happens before the
`uri.txt` load in `main.cpp` and does not call
`UriMapSingleton::getInstance()`.

**C6. No new JSON dependency, no C++20.** Same as `CLAUDE.md` / Milestone
0001 C6/C7 - manual JSON helpers in `core/utils.h`, C++17 only.

**C7. Register every new `.cpp` in `sources.cmake`** (and `CMakeLists.txt`
for new test targets), per `CLAUDE.md`'s Build section.

**C8. Existing server-flag CLI behavior is unchanged.** Every flag currently
accepted by `CliParser::parse()` (ports, TLS, shortener, analytics, request
limits) keeps parsing identically; `link <verb>` is strictly additive.

## Dependency graph

```text
01.0 command-layer-completion
   |  (new DTOs + LinkCommandService methods; handlers migrated, REST behavior locked)
   v
02.0 cli-argument-parsing
   |  (argv -> same DTOs as 01.0 and the pre-existing create/get/stats DTOs)
   v
03.0 cli-dispatch-and-lifecycle
   |  (main.cpp branches before HttpServer/io_context construction)
   v
04.0 cli-output-and-error-contract
   |  (every link <verb> subcommand gets consistent stdout/exit-code behavior)
   v
05.0 tests
   |  (unit + integration + e2e across all ten commands; REST non-regression)
   v
06.0 docs-and-registry-sync
      (corrected CLI docs, command reference, design note)
```

## File map

```text
include/url_shortener/app/
  link_command_service.hpp        (extend: 4 new DTOs + 4 new methods)
  legacy_adapters.hpp              (extend: LegacyLinkStore gains update/delete/setEnabled/restore)

src/app/
  link_command_service.cpp         (extend)
  legacy_adapters.cpp               (extend)

src/http/handlers/
  link_handlers.cpp                (migrate patch/delete/lifecycle/preview onto LinkCommandService)

include/url_shortener/cli/
  cli_parser.h                     (extend: ParseResult command variant)
  link_command_args.hpp            (new: argv -> DTO mapping per link verb)

src/
  cli_parser.cpp                    (extend: recognize `link <verb>`)
  cli/link_command_args.cpp         (new)
  cli/link_command_dispatch.cpp     (new: DTO -> LinkCommandService -> stdout/exit code)
  main.cpp                          (extend: branch before HttpServer construction)

tests/unit/app/
  13-16_link_command_service_{update,delete,set_enabled,restore}_*.cpp (new)

tests/unit/cli/
  01_cli_parser_link_commands.cpp   (new)

tests/integration/cli/
  11-20_link_{update,delete,enable,disable,restore,preview}_*.py (new; 01-10 already exist for create/get/stats)

tests/e2e/scripts/sections/
  14_cli_link_update.sh, 15_cli_link_delete.sh, 16_cli_link_enable_disable.sh,
  17_cli_link_restore.sh, 18_cli_link_preview.sh (new; 11-13 already exist)

docs/
  testing/cli_link_commands.md      (corrected)
  testing/testplans/cli.md          (corrected, extended to 10 commands)
  cli/README.md                     (new: CLI command reference)
```

## Validation commands

```bash
cmake --build cmake-build --target url_shortener
ctest --test-dir cmake-build -L unit --output-on-failure
ctest --test-dir cmake-build -L "unit|contract|integration|e2e" --output-on-failure
```

## Global acceptance criteria

- [ ] Every command in the inventory table above is reachable both as a REST
      endpoint (unchanged) and as a `link <verb>` CLI subcommand.
- [ ] `LinkCommandService` has one method per in-scope command, and every
      handler in `link_handlers.cpp` for those commands calls it - no command
      logic is duplicated between the REST handler and the CLI dispatch path.
- [ ] REST characterization tests (Task 05.0) prove Task 01.0's migration
      changed no observable HTTP response.
- [ ] CLI mode never binds a listening socket or calls `io_context.run()`
      (verified for all ten commands, not just create/get/stats).
- [ ] `CliParser`'s existing server-flag behavior is unchanged (Task 02.0
      regression coverage).
- [ ] `docs/testing/cli_link_commands.md` and `docs/testing/testplans/cli.md`
      no longer reference `uri.txt` and cover all ten commands.
- [ ] `docs/roadmap/README.md`'s milestone table and reserved-number note are
      updated to reflect Milestone 0003 (done as part of adding this plan).
