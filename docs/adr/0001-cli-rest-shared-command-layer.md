# 0001 - CLI/REST Shared Command Layer

> **Status:** Accepted
>
> **Date:** 2026-09-23
>
> **Supersedes:** _(none)_
>
> **Superseded by:** _(none)_

## Context

Until Milestone 0003, the service exposed exactly one interface: HTTP. Every
link-management operation was reachable only as a REST endpoint dispatched
through `Router`/`RouterBuilder` against the route inventory in
`src/http/route_registry.cpp`. There was no CLI entrypoint at all -
`CliParser` parsed only server-startup flags into `ServerConfig`, and
`src/main.cpp` unconditionally constructed `HttpServer` and ran the
`io_context`.

The link-management surface is ten REST rows but nine distinct verbs
(`create`, `get`, `update`, `delete`, `enable`, `disable`, `restore`,
`preview`, `stats`); `GET /api/v1/links/{slug}` and
`GET /api/v1/links/id/{id}` both resolve to the single `link get --slug|--id`
verb. Of those, `create`, `get`, and `stats` already sat on a
transport-agnostic `app::LinkCommandService` (`Config in -> Result<View> out`),
but the business logic for two operation groups - the PATCH/update path
(`handlePatchLink`) and the lifecycle/delete path (`handleDeleteLink`,
`handleLifecycleAction`, `handlePreviewLink`) - lived **only inline in the
HTTP handlers**, mutating links directly via `getLinkForRead()` /
`updateLinkAndInvalidateCache()`. A second interface could not reuse that
logic without copy-pasting it, which would fork validation and cache
invalidation rules between transports.

We needed a second (CLI) interface over the same command surface without
duplicating business logic and without changing any existing REST response.

## Decision

Adopt a ports-and-adapters split for management commands, one layer above the
storage ports, with exactly three pieces per command:

1. **One Config DTO per command** - a plain struct in
   `include/url_shortener/app/link_command_service.hpp` with no Beast,
   Boost.ProgramOptions, or I/O dependency
   (e.g. `app::UpdateLinkCommand`, `app::DeleteLinkCommand`,
   `app::SetLinkEnabledCommand`, `app::RestoreLinkCommand`, alongside the
   pre-existing `CreateLinkCommand`/`GetLinkQuery`/`GetLinkStatsQuery`).
2. **One `LinkCommandService` method per command** - e.g.
   `LinkCommandService::UpdateLink`, which holds *all* business logic
   (validation, storage calls, cache invalidation) and returns `Result<T>`
   (value or `AppError`), never throwing for expected conditions.
3. **Two thin transport adapters**, neither of which contains business logic:
   - the REST adapter (`src/http/handlers/link_handlers.cpp`) parses the HTTP
     request into the DTO and maps `Result<T>` to HTTP status/JSON via the
     existing `statusForAppError`/`codeForAppError` pattern;
   - the CLI adapter (`src/cli/link_command_dispatch.cpp`, built on
     `src/cli/link_command_args.cpp`) parses `argv` into the *same* DTO and
     maps the *same* `Result<T>` to a JSON-stdout / exit-code / stderr
     contract.

Concretely, `app::UpdateLinkCommand` + `LinkCommandService::UpdateLink` is
called from **both** `src/http/handlers/link_handlers.cpp` and
`src/cli/link_command_dispatch.cpp`; the same pattern holds for all nine
verbs. Both adapters share one serializer per shape
(`app::serializeLinkViewJson` / `app::serializeLinkStatsJson`) - there is no
forked serialization path. This is the same discipline already used for
storage backends (`IMetadataRepository`, `ICacheStore`, ...), applied between
HTTP and `argv`.

## Alternatives Considered

| Alternative | Pros | Cons | Reason rejected |
|-------------|------|------|-----------------|
| Duplicate the inline handler logic into a separate CLI code path | No refactor of existing handlers up front | Two copies of validation/cache-invalidation rules per command; they drift silently | Violates the single-source-of-truth goal that motivated the milestone |
| CLI shells out to the running HTTP server (localhost client) | Zero new command layer; reuses REST verbatim | Requires a running server; couples CLI to network/TLS; contradicts "CLI mode never starts a listener" invariant | Defeats the purpose of an offline management CLI |
| Put shared logic in the storage layer (`IMetadataRepository`) instead of a new app service | No new layer | Storage ports are aggregate-CRUD, not command semantics; validation/campaign/expiry rules do not belong there | Wrong seam; would overload the storage contract |
| One shared Config DTO + one command-service method per command, two thin adapters (**chosen**) | Business logic written once; adapters stay thin; REST responses provably unchanged | Requires migrating the four inline handlers behind the service first | Accepted - the migration is bounded and covered by characterization tests |

## Consequences

### Positive

- Business logic for every in-scope command lives in exactly one place
  (`LinkCommandService`); a new rule is implemented once for both interfaces.
- Future commands, if the REST surface grows, follow the same three-piece
  pattern (Config DTO + service method + two thin adapters) rather than
  reinventing a transport-specific path.
- REST response shapes were provably unchanged by the migration
  (characterization tests in `tests/unit/http/10_link_handlers.cpp` were
  written before the migration, per contract C3).
- The CLI adapter reuses the exact REST serializers, so CLI stdout is
  byte-identical to the REST body for the same command.

### Negative

- **With the default in-memory backend, the CLI does not support multi-step
  scripting across separate process invocations.** `linkRepository()` is a
  function-local `static` in-memory singleton, per-process only, with no
  disk-backed persistence (confirmed in Task 03.0 subtask 02, re-confirmed in
  Task 05.0 subtask 03). A `link create` in one process is invisible to a
  `link get` in a second process; only calls within the *same* process observe
  each other. This limitation applies to the **default mode** (no
  `--storage-config`). With `--storage-config <yaml>` (or
  `URL_SHORTENER_STORAGE_CONFIG`) pointing at a persistent backend, separate
  CLI invocations and the running server all share that backend — the
  storage-path duality described in the Non-goals below was resolved in
  Milestone 0006 Task 02.0. See [`docs/cli/README.md`](../cli/README.md) for
  usage details and cross-process visibility bounds.
- The four previously-inline handlers now carry an extra indirection through
  `LinkCommandService`; per-field PATCH 400 codes deliberately stay in the
  REST handler (routing them through `codeForAppError` would collapse them
  into `invalid_request` and change the REST response), so validation is
  intentionally re-applied in the service to keep non-REST callers safe.

### Non-goals (explicit)

Restated from `plan.md` so a future milestone does not rediscover them:

- **No CLI form** is provided for four REST groups:
  - **Observability** (`GET /healthz`, `/readyz`, `/metrics`) - report the
    state of a *running* process; CLI mode never starts one.
  - **Compatibility** (`POST /api/v1/short-urls`,
    `GET /api/v1/short-urls/{slug}`) - HTTP-only aliases for legacy callers;
    CLI users get `link create`/`link get` directly, and there is no legacy
    CLI to stay compatible with.
  - **Redirects** (`GET /r/{slug}`, `GET /{slug}`) - the protected fast path,
    not a management command.
  - **Fallback** (`GET`/`POST`/`DELETE /{path}`, `/`) - the legacy generic
    URI-store (`UriMapSingleton`/`uri.txt`), unrelated to the `Link` domain
    model. CLI mode never touches `uri.txt`.
  - (Placeholders `GET /api/v1/links/{slug}/qr`, `/routing` return
    `501 feature_not_enabled` and have nothing to expose yet.)
- **Storage-path duality resolved in Milestone 0006 Task 02.0**
  (`app::LinkBackend` install hook + `RepositoryLinkStore`, Task
  02.0/10–15). The redirect path, REST handlers, and CLI all obtain their
  `ILinkStore` from `app::LinkBackend::store()`. With `--storage-config
  <yaml>` (or `URL_SHORTENER_STORAGE_CONFIG`) the installed store is a
  `RepositoryLinkStore` backed by the configured `IMetadataRepository`; the
  default remains the in-memory `LegacyLinkStore`. CLI multi-step scripting
  across separate invocations therefore works when both invocations (and the
  server) point at the same persistent backend. See
  [`docs/cli/README.md`](../cli/README.md) for usage and cross-process
  visibility bounds.

  > **Original non-goal (Milestone 0003):** "The `LinkService` vs.
  > `LegacyLinkStore` storage-path duality is not resolved by this milestone.
  > The redirect fast path (`LinkService::Resolve`) reads through the
  > *pluggable* `IMetadataRepository` selected via
  > `StorageConfig`/`BuildStorageAdapters`, while `LinkCommandService` (via
  > `LegacyLinkStore`) reads/writes the older, always-in-memory
  > `linkRepository()` global singleton. These are two different storage paths
  > under the same interface name. This milestone mirrors the command layer as
  > it exists today onto the CLI; reconciling the two storage paths (and
  > thereby enabling cross-invocation CLI scripting on a persistent backend)
  > is a separate, pre-existing architectural question tracked outside Milestone
  > 0003."

## Validation / Rollout

- Shipped incrementally across Milestone 0003 Tasks 01.0-05.0 (all complete).
- REST non-regression pinned by characterization tests written *before* the
  handler migration (`tests/unit/http/10_link_handlers.cpp`).
- Command-service methods covered by unit tests
  (`tests/unit/app/13-17_link_command_service_*.cpp`); the CLI parser and
  dispatch by `tests/unit/cli/*` and integration/e2e suites.
- The cross-process limitation (default mode) is validated by
  `dispatch_create_then_get_roundtrip_same_process`. `cli_03_link_create_then_get_persists_state`
  and `e2e_12_cli_link_get` were extended in Milestone 0006 Task 02.0/13 to
  be two-sided: the SQLite case passes (cross-invocation persistence works
  with `--storage-config`) and the in-memory case asserts isolation (the
  default is unchanged). Both remain expected behavior, not defects.

## Links

- **Roadmap plan:** [Milestone 0003 plan](/docs/roadmap/0003-cli_rest_interfaces/plan.md)
  (Architecture section - the DTO-sharing decision; Explicitly-out-of-scope
  table - the non-goals restated above).
- **Roadmap task:** [Task 06.0 subtask 03 - Design note / ADR](/docs/roadmap/0003-cli_rest_interfaces/06.0-docs-and-registry-sync/03-design-note-adr.md)
- **Milestone status:** [status.md](/docs/roadmap/0003-cli_rest_interfaces/status.md)
  (Task 03.0 subtask 02 storage-scoping finding; Task 05.0 subtask 03
  re-confirmation).
