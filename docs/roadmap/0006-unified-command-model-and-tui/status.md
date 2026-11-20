# Milestone 0006 - Unified Command Model and TUI - Status

Tracks progress against [plan.md](/docs/roadmap/0006-unified-command-model-and-tui/plan.md).

## Current status

| Task | Name | Status | Tests |
|------|------|--------|-------|
| 01.0 | Command model core | ✅ Complete (13/13 subtasks) | 14 (`commands__01`-`14`) |
| 02.0 | Storage path unification | ✅ Complete (16/16 subtasks, incl. added 02.0/16) | 15 targets (`app__19`-`24`, `metadata__09`, `inmemory__16`, `storage__15` x2, http `30`/`31`, `cli__05`, `cli_03`/`17`/`18`, `e2e_12`) |
| 03.0 | Link and browse commands | ✅ Complete (18/18 subtasks) | 23 targets (`commands__20`-`30`, `app__40`-`48`, `core 09`, `memory__17`, `analytics__16`, storage list-events SQLite/Postgres, contract `10`-`12`) |
| 04.0 | REST and CLI adapters | ✅ Complete (15/15 subtasks) | 22 (`commands__40`, http `15`-`25`; `cli__06`-`12`; cli int `20`, `21`; `observability__04`; `ratelimiter__08`) |
| 05.0 | Jobs, pair import/export, stdio | ✅ Complete (21/21 subtasks) | 23 (`commands__50`-`65`; `app__60`; `cli__20`, `cli__21`; http `40`; cli int `40`, `41`; e2e `30`) |
| 06.0 | Terminal UI | ✅ Complete | 58 (`tui__01`, `tui__02_*` x3, `tui__03`, `tui__04_*` x6, `tui__05_*` x4, `tui__06_*` x4, `tui__07_*` x3, `tui__08_*` x3, `tui__09_*` x3, `tui__10_*` x2, `tui__11_*` x4, `tui__12_*` x3, `tui__13_*` x2, `tui__14_*` x2, `tui__15_*` x3, `tui__16_*` x3, `tui__17_*` x2, `tui__18_*` x3, `tui__19_*` x2, `tui__20_app_navigation_test`, `tui__21_*` x2, `cli__30`) |
| 07.0 | Tests and conformance | 🔶 In progress (25/34 subtasks) | 20 (`conformance_00_corpus_lint`, `conformance_01_matcher_vectors`, `conformance_02_harness_selftest`, `conformance_10_cli_cmd_corpus`, `conformance_11_stdio_corpus`, `conformance_12_stdio_framing`, `conformance_13_rest_generic_corpus`, `conformance_14_rest_resource_corpus`, `conformance__00_support_selftest`, `conformance__01_direct_registry_corpus`, `conformance__02_tui_client_corpus` (+ link corpus 57 cases, browse/system corpus 39 cases), `commands__70`..`72`, `commands__74`, `commands__75`, `commands__76`..`78`, `cli__31`, `tui__23`, `conformance_31_import_limits_process_level`, `tui__37`, `conformance_30_authz_process_level`, `commands__79_no_http_server_in_local_modes_test`, `commands__80_redirect_isolation_test`, `conformance_32_no_listener_process_level`, `e2e_19`, `docs__03`..`06`, `tui__22_mock_support_helpers_test`, `tui__30_form_model_property_test`, `tui__34_no_secrets_real_registry_test`; perf label: `analytics__*` x3) |
| 08.0 | Docs and registry sync | ⬜ Not started | 0 |

**Legend:** ✅ Complete · 🔶 In progress / partial · ⬜ Not started

**Current gate status:** design complete, decisions confirmed, implementation
not started (2026-10-22). Delivered so far:
[ADR 0002](/docs/adr/0002-unified-command-model.md) and
[ADR 0003](/docs/adr/0003-tui-framework-and-architecture.md) (both still
**Proposed** - intentionally; see below), the [command catalog](/docs/commands/README.md),
this milestone's plan and task/subtask specs, and (2026-10-22) a human review
of all D1-D16 open decisions plus a new Settings screen (D13 in ADR 0003,
Task 06.0 subtask 21) added after the headless design pass. No code has
changed; no build or test was run. Branch `roadmap/tui-unified-command-model`,
merged to `master` locally (not yet pushed).

**D1-D16 are confirmed, not open.** Every recommendation in
[decision-log.md](/docs/roadmap/0006-unified-command-model-and-tui/decision-log.md)
was reviewed with the user on 2026-10-22 and adopted as-is (see that file's
"Human review" section) - none were reversed. This un-blocks every owning
subtask listed in the decision log.

**ADR 0002/0003 stay `Proposed` on purpose**, even after the D1-D16 review:
flipping them to `Accepted` is Task 08.0 subtask 01's job, which requires Task
07.0's gates to have actually passed and real artifacts (pinned FTXUI
version, `tui --help` output, as-built deltas) that do not exist before any
code is written. Confirming the decisions removes the planning-level
blocker; it is not a substitute for that gate.

## Per-task detail

### Task 01.0 - Command model core (✅)

Completed 2026-11-14 on branch `milestone/0006-unified-command-model-and-tui`.

**Delivered** (`include/url_shortener/commands/`, `src/commands/`):

- `json_scan` - bounded, iterative JSON object/array scanner (the one
  sanctioned tokenizer, D3); hashed duplicate-key detection.
- `error_codes` - single error-code / HTTP status / CLI exit-code table with
  REST and CLI parity (C3/C4); UTF-8-safe `sanitize_error_message`.
- `envelope` - closed request envelope parser (echoed `id` re-validated or
  replaced by `srv-<n>`), response serializer that fully validates
  `result_json` as one JSON value, job stub.
- `arg_spec` - `ArgSpec`, `ArgsView`, `validate_args` with per-arg nested
  limits (T-06) and its own 64 KiB byte cap.
- `command_descriptor`, `command_registry` - descriptor + registry; execute
  pipeline: envelope -> lookup -> base permission -> validation ->
  `permission_for` (max) -> dry_run/async/job dispatch -> handler ->
  single-exit audit; every step exception-isolated; fail-closed permission
  mapping.
- `job_submitter` - `IJobSubmitter` seam (runner itself is Task 05.0).
- `audit` - `IAuditSink`, `LoggerAuditSink`, one record per mutating attempt.
- `builtin_commands` - `command.list`; `catalog` - catalog/help renderers;
  `tools/command_catalog_dump` + generated block 3.7 in
  `docs/commands/README.md`.

**Tests / gate:**

- 14 unit executables `commands__01`-`14` (scanner, error table, request
  parse, response serialize, ArgSpec, registry, execute pipeline, options,
  audit, command.list, catalog render, catalog drift, error-code coverage,
  include hygiene).
- Every subtask: `/verify-subtask` PASS (PARTIALs resolved) and a
  `security-auditor` review; Task-close `/pr-review`: feature LGTM, security
  PASS_WITH_FOLLOWUP (T-22 fixed in `a27dbb7`). Threat model:
  [docs/security/0006-01.0-command-model-core-threat-model.md](/docs/security/0006-01.0-command-model-core-threat-model.md)
  ("Task 01.0 close" section).
- Full suite `-L "unit|contract|integration|e2e"`: 284/289. The 5 failures
  are `e2e_14`-`18` CLI scripts that call GNU `timeout`, absent on macOS
  (`gtimeout` exists); untouched by this branch - environment issue, noted
  for Milestone 0007.
- `clang-format` applied module-wide (`e9c139f`, whitespace/sort only).

**Carried forward** (tracked in the run cursor):

- 03.0/05.0: every dry_run-capable job factory must return a non-mutating,
  bounded body (T-21); handlers' `audit_target` must be a validated id;
  `register_documented_commands` registers placeholder services only.
- 04.0: adapters must put only non-identifying labels in
  `CallerContext.principal`/`transport`.
- 06.0/07.0: `parse_response` is lenient (unknown keys, ok/payload).
- 0002: permission-filtered `command.list`; audit of denied sensitive reads.
- 0007: `jsonEscape` control-character passthrough; SQLite unit tests use
  fixed DB files (collide under concurrent ctest); e2e GNU `timeout`;
  `parseRfc3339Zulu` missing-`Z` leniency may be platform-dependent.

### Task 02.0 - Storage path unification (✅)

Completed 2026-11-14 on branch `milestone/0006-unified-command-model-and-tui`.

**Delivered:**

- `ExpiringLinkCache` (L1, TTL with clock-safe expiry, bounded sampled
  eviction, `patch()` that never refreshes `stored_at` - T-32).
- `link_record_codec` (`Link` <-> `LinkRecord`, `lnk_*` attributes,
  fail-closed `lnk_v`, bounded/validated read path).
- `RepositoryLinkStore`: L1-then-repository reads, indexed `findById`,
  writes invalidating both cache tiers on every outcome, write-behind
  `recordRedirect` with a background flusher, store write mutex shared by
  flush and writes (R-1, user decision), exception-safe and time-boxed
  shutdown flush.
- `IMetadataRepository::GetById` on every backend (new subtask 02.0/16,
  user decision; SQLite `idx_links_id`, Postgres migration `005`).
- `LinkBackend` holder (lock-free `store()`, serving latch, `shutdown()`),
  `InitLinkBackend` bootstrap (fail-closed, redacted messages, 0600 via
  umask incl. SQLite companions, exit code 4), `--storage-config` /
  `URL_SHORTENER_STORAGE_CONFIG`, `main.cpp` startup/shutdown order.
- Redirect, REST and slug paths rerouted through `LinkBackend::store()`;
  hardened T-40 guard test against direct store writes from `src/http` /
  `src/cli`.
- Docs: CLI storage/backend selection, storage configuration, adapter
  guide (`GetById`), ADR 0001 note.

**Tests / gate:**

- 15 new or extended test targets (listed in the table above).
- `cli_18_server_and_cli_share_sqlite`: a CLI disable of a continuously
  hit link becomes 410 on the running server after ~4.9 s (5 s L1 TTL) -
  T-32 verified end to end; no "database is locked".
- Postgres path verified on a temporary local PostgreSQL 16 cluster
  (index scan on `idx_links_id`).
- Every subtask: `/verify-subtask` and a security review; Task-close
  `/pr-review`: feature REQUEST_CHANGES on one wrong lifetime comment
  (fixed in `491832b`) -> APPROVE; security PASS_WITH_FOLLOWUP with no new
  findings. Threat model:
  [docs/security/0006-02.0-storage-path-unification-threat-model.md](/docs/security/0006-02.0-storage-path-unification-threat-model.md)
  ("Task 02.0 close" section).
- Full suite `-L "unit|contract|integration|e2e"`: 299/304; the 5 failures
  are `e2e_14`-`18`, which need GNU `timeout` (absent on macOS) - unchanged
  by this Task.

**Carried forward:**

- 03.0: Postgres `ListLinks` owner filter returns 0 rows
  (`metadata__06`, pre-existing) and the Postgres contract harness lacks
  per-case isolation; redirect returns 404/410 instead of 5xx on a
  repository outage (T-35/R-5); `audit_target` and dry-run job body
  obligations from Task 01.0.
- 07.0: server-spawning integration tests flaky under parallel load.
- 08.0: `GetById` in the port lists of `CLAUDE.md` / `ARCHITECTURE.md`.
- 0007: shared limit constants (codec duplicates `core/utils` limits),
  `redis_address` validation, logger CR/LF stripping and testability,
  SQLite unit tests' fixed DB files, e2e GNU `timeout`, clang-format
  version pinning and the whole-file post-edit format hook.

### Task 03.0 - Link and browse commands (✅)

Completed 2026-11-16 on branch `milestone/0006-unified-command-model-and-tui`.

**Delivered:**

- Storage query layer: `ListLinksQuery` (text, sort, order, extra filter),
  `ListLinksPage` default with `kListScanLimit`, Postgres owner-filter bind
  fix (T-50); `IClickEventRepository::ListEvents` (in-memory + SQL, bound
  parameters, whitelisted ORDER BY, escaped LIKE, shared 256-byte text cap,
  offset clamp, page cap 500).
- Command layer: codec helpers (C1), `ValidateCreateLink`/`ValidateUpdateLink`
  dry-run variants, `link.create/get/preview/stats/update/delete/enable/
  disable/restore`, `register_link_commands` with a 26-case REST parity gate
  (C3), `serializeLinkLifetimeStatsJson`.
- Browse: `ILinkLister` (repository + legacy snapshot listers sharing one
  filter implementation), `link.list`, `ITableSource`/`TableCatalog`
  (allowlist = catalog, auth tables refused), `db.tables`/`db.rows`,
  `links`/`click_events` sources (hashed client id only, referrer query,
  fragment and userinfo stripped), `system.info` (allowlisted backend names,
  non-secret settings, `URL_SHORTENER_VERSION`).
- Composition: `build_command_runtime` (14 commands, mandatory audit sink),
  `LinkBackendHandle::repository()`, system-info snapshot published by
  `InitLinkBackend`, documented registry over inert placeholders with
  catalog block 3.7 regenerated.
- Hardening: shared `jsonEscape` now always emits valid JSON (all C0, DEL,
  U+2028/2029 escaped; invalid UTF-8 -> U+FFFD).
- Tooling (user decisions): Stop hook busy guard (`1609d41`); whole-file
  reformat of legacy files accepted.

**Tests / gate:**

- 23 new or extended test targets (table above); full serial suite 314/316
  with the two known flaky server-start tests passing alone (tracked for
  07.0).
- Every subtask: `/verify-subtask` and a security review; findings fixed in
  the same iteration or carried forward with an owner.
- Task-close `/pr-review`: feature LGTM (4 non-blocking suggestions ->
  0007 list); security PASS_WITH_FOLLOWUP (no CRITICAL/new HIGH). Threat
  model with Task-close section:
  [docs/security/0006-03.0-link-and-browse-commands-threat-model.md](/docs/security/0006-03.0-link-and-browse-commands-threat-model.md)

**Open decisions raised at close:** unbounded stats range (max bucket
count), SQLite 32-bit INTEGER read (Y2038), T-54 storage-outage semantics
ADR, clang-format version pinning, public `GET /api/v1/links` gating +
100k scan (04.0/07).

### Task 04.0 - REST and CLI adapters (✅)

Completed 2026-11-17 on branch `milestone/0006-unified-command-model-and-tui`.

**Delivered:**

- Bridge: `args_builder` (escaped args assembly), REST command bridge
  (`execute_resource_command`, `execute_command_envelope`,
  `resource_response`, legacy error mapping), process-wide command runtime
  (fail closed while serving without one).
- REST routes on the registry, bodies byte-identical (C3): reads (03, with
  the 10,000-bucket stats cap and epoch bounds), create (04), PATCH (05),
  delete/enable/disable/restore + legacy helper removal (06).
- Create throttle (T-75): always on, 30/60 s per TCP peer, `link_create:`
  keys, shared `peer:unknown` bucket, fail-open with
  `create_throttle_*` metrics, bounded in-memory limiter (eviction + 1M
  cap); peer read only for `/api/` targets (redirect path untouched).
- Command API (default off): `command_api_enabled`/`command_api_role` +
  CLI flags (08); gated `GET /api/v1/links` with query parser and
  `link.list` write-raise for `status=all|deleted`, `offset` cap 1,000,000
  (07); `POST /api/v1/commands` (JSON-only content type, shared throttle
  incl. `dry_run`, startup log of gate state) (09);
  `GET /api/v1/commands` + regenerated `docs/api/README.md` + operator
  exposure/CSRF notes in `docs/commands/README.md` (10).
- CLI: `--arg` coercion (11), `cmd` parsing (12), `cmd` dispatch with
  bounded stdin, `--read-only`, shared exit-code table (13), link flag ->
  args converters with ArgSpec coverage test (14), `link <verb>` on the
  registry with byte-identical output (15); shared
  `acquire_command_runtime`; `ExitCodeForAppError` in its own TU.
- Logging (user decision): real append-only `LogSink::File`
  (0600, `O_NOFOLLOW`, fail closed with exit 2 "log file unavailable").

**Tests / gate:**

- New targets: commands `40`; http `15`-`25`; cli `06`-`10`, `12`;
  cli integration `20`, `21`; `observability__04`; `ratelimiter__08`.
  Task-close full suite (`unit|contract|integration|e2e`, serial): 353/358;
  the 5 failures are e2e `14`-`18` on macOS (GNU `timeout` missing, 0007
  item) and pass with a `gtimeout` shim (358/358 effective).
- Every subtask: `/verify-subtask` + security review; findings fixed in the
  same iteration or carried with an owner.
- Task-close `/pr-review`: feature REQUEST_CHANGES on exit criterion 2
  (fixed in `b411e2b`, grep now empty) -> resolved; security
  PASS_WITH_FOLLOWUP (no CRITICAL/HIGH). Threat model with Task-close
  disposition (T-60..T-85):
  [docs/security/0006-04.0-rest-and-cli-adapters-threat-model.md](/docs/security/0006-04.0-rest-and-cli-adapters-threat-model.md)

**Spec reconciliation:** test file numbers shifted where slots were taken
(http 19-25, declared per subtask); 08's config/caller helpers landed early
in 07 (`3ce5e66`), 08 consumed; spec 11 prose aligned with its own tests
(`eae6e05`); 04's "peerless not throttled" superseded by the shared
fallback bucket; job waiting in `cmd` deferred to 05.0/20 as specced.

**Open decisions raised at close:** whether a `User` may list
`disabled`/`expired` links (built per the 2026-11-16 decision: only
`all|deleted` need write; the T-71 wording is stricter) - user kept
the as-built behaviour on 2026-11-17; T-71 note added to the threat model.

### Task 05.0 - Jobs, pair import/export and stdio (✅)

Completed 2026-11-18 on branch `milestone/0006-unified-command-model-and-tui`.

**Delivered:**

- Jobs: `JobStore` (bounded retention, metadata-only `list_summaries`),
  `JobRunner` (bounded queue/workers, cooperative cancel, `wait_*`,
  concurrent-safe `shutdown`), registry job dispatch (dry-run always
  synchronous), `job.get`/`job.list` (summaries, no `result`)/`job.cancel`,
  `jobs` browse table, process-wide job runtime in `build_command_runtime`,
  `cmd` job waiting (`--no-wait`), REST `/api/v1/jobs*` (gated; cancel
  write + `application/json`).
- Pairs: CSV/NDJSON line parsers with capped error list, pair file access
  (default-deny `PathPolicy`, rooted `openat` walk, create-new atomic
  export), `link.import` (skip/fail/overwrite via `ReplaceLinkTarget`,
  synchronous non-mutating dry-run), `link.export` (inline <= 1 MiB or
  file; `status=all|deleted` need write).
- stdio: `ITransportAdapter` + factory, `NdjsonLineReader`,
  `NdjsonSession`, `StdioAdapter` (run-once, drain, exit codes),
  `serve-stdio` CLI + wiring (stderr-only logs, SIGPIPE ignored, active
  backend), process tests, adding-a-transport guide.
- Security fixes found during the task: CRITICAL CWE-113 response
  splitting in the URL validator (`25d66be`, pre-0006, master affected);
  `link.export` status bypass (`04dbd13`); `db.rows` links soft-deleted
  bypass (`bf80e89`); CSRF on job cancel (`638c959`); serve-stdio split
  store (`ba2d150`); T-96 parent-directory TOCTOU (`fecc3e7`); job list
  result amplification (`6aa0b97`, `370ffc6`).
- Tooling: shared build lock `cmake-build/.build.lock`, honoured by the
  Stop hook (`5bb56b9`, user decision).

**Tests / gate:**

- New targets: commands `50`-`65`; app `60`; cli `20`, `21`; http `40`;
  cli integration `40`, `41`; e2e `30`. Task-close full suite
  (`unit|contract|integration|e2e`, serial, `gtimeout` shim for macOS
  e2e 14-18): 381/381.
- Every subtask: `/verify-subtask` + security review (18/19 test/doc-only:
  verifier only); findings fixed in-task or carried with an owner.
- Task-close `/pr-review`: feature LGTM (4 non-blocking -> 0007 list);
  security PASS_WITH_FOLLOWUP, per-threat disposition T-86..T-109
  appended to
  [docs/security/0006-05.0-jobs-pairs-and-stdio-threat-model.md](/docs/security/0006-05.0-jobs-pairs-and-stdio-threat-model.md)

**Spec reconciliation:** specs 07/08/09/10/20 amended before build
(`2d99c28`: T-91, T-95, T-96); spec 05 amended for `job.list` without
`result` (user decision 2026-11-18); spec 11 amended for the cancel
content type; test files renumbered where slots were taken (declared per
subtask).

**Open decisions raised at close:** backport the CWE-113 fix to `master`;
per-row audit for import overwrite (T-100) vs job-level only; sanitizer
build of the stdio surface (needs a separately configured build dir).

### Task 06.0 - Terminal UI (✅)

Completed 2026-11-19 on branch `milestone/0006-unified-command-model-and-tui`.

**Delivered:**

- Foundation: FTXUI 7.0.3 (pinned commit) behind `URLSHORTENER_ENABLE_TUI`;
  FTXUI-free `url_shortener_tui_model` plus `url_shortener_tui` libraries
  (C8 include boundary test); render harness and golden files; `json_view`;
  `InProcessClient` over the command registry; `AsyncCaller` (cancellable
  tokens, epoch stale-drop, alive guard); `App`/`Router`/frame, terminal
  restore (RAII + SIGTERM), theme incl. `--theme mono`, help overlay,
  toast/confirm (typed-slug delete), form model/widgets/screen with
  `MutationFlow` as the single mutating-submit choke point, table
  model/view with `sanitize_for_terminal` before `truncate_to_width`.
- Screens: Dashboard (D2), Links (D3) with inline detail/stats (D4) and
  forms (D5), Tables (D6) with inline row JSON, Import/Export (D7), Jobs (D8)
  with `PollTicker`, command palette and generic command screen (D9),
  Settings (D13); `tui` CLI mode wiring (C5: no listener, no `uri.txt`;
  file paths opted in for the local operator).
- Read-only mode (06.0/18): `CapabilityModel` is the only enable/disable
  gate; argument-aware preflight `ICommandClient::required_permission` ->
  `CommandRegistry::resolve_permission`, sharing `effective_permission` with
  `execute()` (fail closed); `[READ-ONLY]` badge never clipped (user ruling
  2026-11-19).
- Manual smoke checklist `docs/testing/tui_manual_smoke.md` (S1-S21, D1-D13
  matrix) and ADR 0003 "As-built amendments".
- Defects found and fixed in-task: `MutationFlow` use-after-free guard
  (`b3b23df`); arg-blind read-only mirror, HIGH (`c770499`, `ec1cfe7`);
  smoke bugs - tab navigation dead at launch from a leaked shared
  `text_input_active`, Ctrl-C exit 130, stray non-TTY stderr line
  (`69114b5`), Ctrl-C swallowed while typing (`57cbc39`), `q` at a tab root
  (`47ac255`); `/pr-review` polish incl. a deterministic render-cost test
  replacing a wall-clock bound (`93bf324`).

**Tests / gate:**

- New targets: 58 (`tui__01`..`tui__21`, `cli__30`; see the row above).
- Task-close full suite (all labels): 433/440 at `f336f5c`; the 7 failures
  were environmental - e2e 14-18 (no GNU `timeout`; 19/19 e2e pass with the
  `gtimeout` shim), `hiredis-test` (third-party, needs `redis-server`), and
  `tui__10` (wall-clock bound under load; 3/3 in isolation; replaced by a
  deterministic byte-count check in `93bf324`). Unit 307/307 at `93bf324`.
  The `commands__14`/`commands__29`/`tui__19` concurrency flakes did not
  recur.
- ThreadSanitizer (new `cmake-build-tsan`, `tui__*` x57): no data races;
  `tui__10` green under TSan after `93bf324`.
- Every subtask: `/verify-subtask` + security review, findings fixed in one
  follow-up commit. Task-close `/pr-review`: security PASS (T-110..T-126
  hold end-to-end); feature REQUEST_CHANGES (1 blocking) -> fixed in
  `93bf324`.
- Manual smoke (macOS pty): all runnable items PASS; S8 limited (no
  cross-process analytics); S15/S19 not run; Windows Terminal, conhost,
  Linux and tmux rows NOT RUN (no such host).

**Spec reconciliation:** detail views, row JSON and forms are hosted
inline in the parent screen (ratified): spec 14's
`src/tui/screens/row_json_screen.cpp` is *superseded by inline hosting in
`tables_screen.cpp`*; specs 12/13/08 are *consumed* with inline hosting.
Raw-JSON toggle is `Ctrl-R` (not `Ctrl-J`), import submit is `Ctrl-S`,
Dashboard issues 4 `link.list` count calls (all ratified, recorded in
ADR 0003 "As-built amendments"). All other 06.0 subtask specs are
*consumed* as written.

**Open decisions raised at close:** see the 2026-11-19 "06.0 close"
entry in Notes & decisions.

## Open risks

- **Storage unification (02.0)** touches the path every link command and the
  redirect fast path depend on; it is the highest-risk task and is gated by
  the existing contract suite plus the protected-fast-path rule (C10: behavior
  and complexity unchanged; redirect counters are persisted only by a
  background flusher, per confirmed decision D12).
- **No JSON library**: one bounded top-level scanner (Task 01.0 subtask 01) is
  the only sanctioned deviation from the manual helpers (per confirmed
  decision D3); escalate to a separate ADR rather than adding a dependency
  inline.
- **New HTTP surface on an unauthenticated server**: `/api/v1/commands`,
  `GET /api/v1/links`, `/api/v1/jobs*` are off by default and role-limited
  until Milestone 0002 (per confirmed decision D4).
- **FTXUI claims unverified** (version, MSVC build, Windows console behavior)
  - see [decision-log.md](/docs/roadmap/0006-unified-command-model-and-tui/decision-log.md);
  still scoped to Task 06.0 subtask 01, unchanged by the 2026-10-22 review.
- **Overlap with 0002** pagination/error contracts; reconciliation is a
  documented projection but must be re-checked when 0002 is scheduled.
- **Tauri** has no spec; its relationship to 0002 is an open question (Q14),
  explicitly left open on 2026-10-22 - nothing in 0006 depends on the answer.

## Notes & decisions

- 2026-11-16 (user, raised at Task 03.0 close):
  - **Stats range:** cap the number of buckets a stats request may produce
    (fixed maximum, e.g. 10,000; over-cap -> `invalid_field` with a fixed
    detail). Applies to REST `GET /api/v1/links/{slug}/stats` and
    `link.stats` alike. Owner: Task 04.0 (REST stats adapter subtask).
  - **Y2038:** fix the SQLite 32-bit INTEGER read before Task 04.0 starts
    (03.0 follow-up; round-trip test with a post-2038 timestamp).
  - **Link listing (04.0/07):** `GET /api/v1/links` requires an
    authenticated caller (401 otherwise); `status=all|deleted` additionally
    require write/admin.
  - **Storage-outage semantics (T-54):** app-architect drafts an ADR
    (Proposed) at milestone close (08.0); the user accepts or rejects it.
  - **clang-format version pinning:** not scheduled in 0006; stays on the
    0007 list.

See [decision-log.md](/docs/roadmap/0006-unified-command-model-and-tui/decision-log.md)
for every choice made without asking the user, with alternatives.

**Implementation run started 2026-11-14 (`implement-milestone`).** Research-phase
findings and dispositions:

- *As-built reconnaissance:* 01.0, 05.0, 06.0, 07.0 have nothing built.
  Existing precursors - `LegacyLinkStore`, `BuildLegacyLinkCommandService`,
  the unused `BuildStorageAdapters`, REST/CLI handlers calling
  `LinkCommandService` directly, the narrow `ListLinksQuery`, the ADR index
  rows and the design-stage command catalog - are what 02.0/03.0/04.0/08.0
  replace by design. Not treated as contract-level divergence.
- *Decomposition audit:* every Task folder, `## Subtasks` row and spec file
  is present. Gaps, to be fixed by `app-architect` spec amendments before the
  affected Task starts:
  - 06.0 README rows 02-05 and 03.0 README row 17 `Blocks` cells omit
    06.0/21 (the spec headers are correct) - fix before 06.0.
  - D13 Settings is missing from 07.0's exit criterion 4 ("goldens D1-D10")
    and from 08.0/01's delta check - amend before 07.0.
  - 07.0/29 attributes D13 goldens to 07.0/24-25; they are owned by
    06.0/21 - amend before 07.0.
  - 07.0/24 test files `20_*`/`21_*` collide with 06.0's prefixes; renumber
    into 07.0's reserved 22-69 range before 07.0.
  - D11/D12 have no closing-gate golden check; covered indirectly by
    07.0/27 and 07.0/29 - noted, no action.
  - No numeric coverage floor exists; 07.0/34 uses `/test-gap` P0/P1
    disposition - noted, no action.

**2026-11-14 (threat-model amendments, pre-implementation 01.0 Task 01.0).**
Applied the three pre-implementation follow-ups from
[the 01.0 threat model](/docs/security/0006-01.0-command-model-core-threat-model.md)
(PASS_WITH_FOLLOWUP) as additive spec amendments; no decision D1-D16 or contract
C1-C11 changed:

- 01.0/05 (T-06, HIGH gap): pinned that `validate_args` derives nested
  `scan_array`/`scan_object` `scan_limits` from each list/object arg's `ArgSpec`
  (`max_items`->`max_members`, `max_length`->string/byte caps) while the
  top-level envelope scan keeps the hardening defaults (65536 bytes / 256
  members), so D10's raised per-arg caps satisfy C3 without relaxing top-level
  DoS caps; added accept-4096 / reject-4097 / reject-257-member tests. Also
  T-03: embedded NUL rejected in decoded keys (`unknown`) as well as values,
  with a test.
- 01.0/03 (T-05, MEDIUM): pinned that the best-effort echoed `id` is
  re-validated (length + no control chars) or replaced by the `srv-<n>`
  fallback, restated the closed/unique-key envelope rule (unknown and duplicate
  top-level/`options` keys rejected) and the no-input-echo guarantee; added
  control-char-id and duplicate-key tests.
- 01.0/09 (T-03, MEDIUM): pinned that `audit_fields` neutralizes invalid UTF-8
  and NUL bytes (replace with `?`) before the sink, alongside the existing
  truncation and control-char rules; added a bare-0x80 / embedded-NUL test.
- *2026-11-14, 01.0/04 deviation (security):* spec 04 asked for a "cheap
  sanity, not full validation" first-character check on `result_json`.
  The security review showed that lets a handler result such as
  `{"a":1},"injected":true` add a top-level key to the response envelope
  (or produce malformed JSON), contradicting threat-model T-17. Replaced
  with full single-value validation through the bounded scanner, using
  result-specific limits larger than the request limits. Management path
  only; no interface change.
- *2026-11-14, 01.0/05 spec amendments after implementation:* (1) a
  timestamp without the trailing `Z` follows the shared `parseRfc3339Zulu`
  (accepted on macOS) - the command layer must not be stricter than REST
  (C3). The leniency comes from `std::get_time` and may differ on Linux; a
  pre-existing REST portability question, noted for 0007. (2) NUL in a
  decoded key surfaces as `args: malformed` (scanner rejects first).
  (3) `validate_args` enforces the 64 KiB byte cap itself.
- *2026-11-14, 01.0/06:* two registration rules added beyond spec 06 after
  the security review: a `mutating` command needs write-or-higher base
  permission unless a `permission_for` hook can raise it (D16). (A second
  rule, "dry_run-capable job command needs a `handler`", was removed after
  01.0/08: per spec 08 / T-21 a job command's dry_run runs the
  `job_factory` body, not the handler.)

**2026-11-14 (threat-model amendment, pre-implementation 02.0 T-32, HIGH).**
Applied the one HIGH follow-up from
[the 02.0 threat model](/docs/security/0006-02.0-storage-path-unification-threat-model.md)
as additive spec amendments; no decision D1-D16 or contract C1-C11 changed (C10
is preserved - the patch op is O(1) under the existing single lock, no new I/O
or thread, collapsing the former get+set into one locked lookup-and-mutate):

- *2026-11-14, 02.0/01 (T-32):* added a `patch(slug, mutator)` op to
  `ExpiringLinkCache` that mutates a present, live entry's `Link` in place under
  the lock without resetting `stored_at`, and is a no-op on a miss or expired
  entry (never inserts or resurrects); added tests for stored_at preservation,
  repeated patching of a hot entry still expiring at the original TTL, and
  patch-on-miss/expired not inserting.
- *2026-11-14, 02.0/05 (T-32):* `recordRedirect`'s L1 counter bump now uses
  `patch` (never `set`), stated the staleness bound (a cross-process change is
  visible within one L1 TTL regardless of redirect rate), and added a
  continuously-hit-link test whose out-of-band disable becomes visible after the
  TTL.
- *2026-11-14, 02.0/14 (T-32):* added a one-line cross-reference noting that
  steps 6-7's convergence depends on the 01/05 patch fix (each poll is itself a
  redirect).
- *2026-11-14, 02.0/03 (user decision):* the security review found
  `RepositoryLinkStore::findById` scans up to 100,000 records per call and
  will be reachable from the unauthenticated `GET /api/v1/links/id/{id}`
  route - a DoS vector and a regression from today's O(1) legacy lookup.
  Decided with the user: add `GetById` to `::IMetadataRepository` with
  indexed implementations in every adapter and contract tests (new subtask
  02.0/16), then switch `findById` to it. Must land before 02.0/08 and 11.
- *2026-11-14, 02.0/16 Postgres verification:* run against a temporary
  local PostgreSQL 16 cluster (loopback only). `GetById` and migration
  `005` work (`idx_links_id` present; the id query uses an index scan).
  Two problems that predate this milestone surfaced: the Postgres contract
  harness shares one database across cases and runs (no reset), and
  `metadata__06_list_filtering_pagination` fails on Postgres even on a
  fresh database (`ListLinks` owner filter returns 0 rows; likely the
  untyped `:owner_id IS NULL` parameter). Both routed to 03.0/01-02, which
  rework `ListLinks` and its contract tests.
- *2026-11-14, 02.0/05 (user decision):* the stats flush's read-modify-write
  can overwrite a disable or soft-delete committed in its window, not only
  lose a counter as D12 described. Decided with the user: a store-level write
  mutex shared by the flush and `update()` closes the same-process race; the
  cross-process window stays accepted. D12 and threat T-33 amended to say so.

**2026-11-14 (threat-model amendments, pre-implementation 03.0 T-50, MEDIUM).**
Applied the MEDIUM follow-up from
[the 03.0 threat model](/docs/security/0006-03.0-link-and-browse-commands-threat-model.md)
(PASS_WITH_FOLLOWUP) as additive spec amendments; no decision D1-D16 or contract
C1-C11 changed, Depends on/Blocks headers unchanged:

- *2026-11-14, 03.0/01 (T-50):* spec 01 now owns the Postgres `ListLinks`
  owner-filter defect. Root-caused the `metadata__06` Postgres failure to
  `PostgresSqlDialect::ListSql()` repeating the named parameter `:owner_id`
  (`(:owner_id IS NULL OR owner_id = :owner_id)`) while `PostgresSession::List`
  binds it once, so SOCI's second positional placeholder is unbound and an
  owner-scoped query returns 0 rows (SQLite tolerates the repeat). Amended to fix
  it with a single-use `has_owner` guard `(:has_owner = 0 OR owner_id = :owner_id)`
  mirroring 03.0/04, carved out the one allowed dialect/session edit against the
  "no SQL change" constraint and success criterion 4, and reaffirmed scan overflow
  -> `permanent_failure` (never silent truncation, T-56). No API/signature or schema
  migration; SQLite/in-memory untouched.
- *2026-11-14, 03.0/02 (T-50):* spec 02 now runs the `owner` narrowing case on
  the Postgres harness and asserts it narrows (guarding 01's fix), gives the
  Postgres harness per-case isolation via a new `ResetMetadataHarness` fixture
  that issues `DELETE FROM links` before each case (chosen over a unique table
  prefix - dialect hardcodes `links` - and a unique schema - would need
  `CREATE SCHEMA`/`search_path`/DDL in the session factory), and qualified the
  "fix the test, never the helper" / "skip with a named reason" instructions so a
  single-backend failure from a backend SQL defect is fixed in the backend, never
  masked by weakening or skipping a case.

**2026-11-16 (threat-model amendments, pre-implementation 04.0 REST and CLI adapters).**
Applied the three spec gaps and the follow-up from
[the 04.0 threat model](/docs/security/0006-04.0-rest-and-cli-adapters-threat-model.md)
(PASS_WITH_FOLLOWUP) and the 2026-11-16 user decisions as additive spec amendments;
no decision D1-D16 or contract C1-C11 reversed. Amended specs:

- **04.0/07** (`07-rest-list-links-route.md`, T-71 HIGH): defined "authenticated"
  for 0006 entirely by the config gate + `command_api_caller`: gate off -> 404 (no
  caller; strictly stronger than 401, so no `401` is ever emitted in 0006); gate on
  -> Admin only for role `admin` + loopback peer, else User (both "authenticated");
  `status=all|deleted` require write/admin, enforced by a permission-raising
  `permission_for` hook on the `link.list` descriptor
  (`src/commands/link_list_command.cpp`, listed under 07's Files affected) so the
  same rule holds over `POST /api/v1/commands` and `cmd link.list`; a `User` with
  `status=all|deleted` -> `403 not_authorized` (canonical). Documented the T-60
  interaction (other resource routes stay unauthenticated Admin until 0002) and
  fixed the test cases that let a non-loopback `User` list `status=all`.
- **04.0/03** (`03-rest-read-routes-via-registry.md`, T-76 MEDIUM): fixed
  10,000-bucket cap in the shared `LegacyLinkStatsReader::read`
  (`src/app/legacy_adapters.cpp`) before any allocation; over-cap -> `invalid_field`
  with the fixed detail token **`range_too_large`** (no echoed range), **not**
  promoted to a REST code (consistent with subtask 02 / T-62's two-token set), so
  REST returns `400 invalid_request` and `link.stats` returns `400 invalid_field`,
  message `range_too_large`; at-cap accepted / cap+1 rejected for hour/day/week with
  REST+command parity. One intentional, user-approved C3 exception (only abusive
  over-cap spans change).
- **04.0/04** (`04-rest-create-via-registry.md`, T-75 MEDIUM; user decision
  2026-11-16 - **always on by default**): `POST /api/v1/links` (+ compat `short-urls`)
  throttle creation via `IRateLimiter` keyed on the client peer address (never a
  header). A **dedicated in-memory create limiter is constructed unconditionally at
  startup** (safe defaults **30 creates / 60 s / client address**); when
  `StorageConfig.rate_limit.enabled && backend == "redis"` the Redis limiter is used
  **instead**. Selected via a new `LinkBackendHandle::createRateLimiter()`. Limit/window
  from new `ServerConfig` fields **`link_create_rate_limit` (default 30, `0` = disabled)**
  and **`link_create_rate_window_seconds` (default 60)**, CLI flags
  **`--link-create-rate-limit`** / **`--link-create-rate-window-seconds`**. Over-limit ->
  **`429 rate_limited`** (transport-level, like 415/404, not a `CommandErrorCode`, no
  01.0/02 table change); limiter failure is fail-open; redirect and CLI not throttled.
  `createLinkFromRequest` gains a `const RouteContext&` parameter. **Subtask 04 also
  delivers the behavior-neutral `RouteContext::peer` plumbing** (formerly 09 step 0) so
  the throttle has a client address and no 04 -> 09 -> 06 -> 04 cycle arises.
- **04.0/09** (`09-rest-post-commands-endpoint.md`, T-68 + T-75): the handler builds a
  **full** `envelope_limits` (keeps `max_depth = 8`, `max_command_len = 64`) overriding
  only `max_bytes`/`max_id_len`, with the member (256) / key (128) caps enforced by
  `parse_request`'s `scan_limits` (noted that these are not `envelope_limits` fields,
  unlike the threat-model shorthand); `link.create` over the generic endpoint is
  throttled identically to 04.0/04 (same `createRateLimiter()`, same config) with
  `dry_run` counting like a real create. Step 0 (peer plumbing) is reworded to
  **verify** the plumbing that subtask 04 delivers, performing it only if 04 has not.
- **New wire tokens:** `rate_limited` (REST transport-level, HTTP 429) and
  `range_too_large` (stats over-cap detail token). Neither is a `CommandErrorCode`.
- **Not amended:** 04.0/08 (T-64 defaults off/`user`, loopback matrix) and the T-72
  carry already match the threat model; no contradiction found. The README Subtasks
  table is unchanged - subtask 04 absorbing the peer plumbing adds no new subtask-graph
  edge (04 already precedes 06/07/09), so no cycle and no table change.

### 2026-11-19 - 06.0/18 read-only: argument-dependent permission (user ruling)

- **Finding (security review of `cdfacd1`, HIGH x2):** `link.list` and `link.export` raise
  their required permission to `write` via `permission_for` (`status=all|deleted`, export
  `path`), but the TUI client mirror (`is_read_only_command`, `CapabilityModel`,
  `MutationFlow`) saw only the descriptor's base `read`. A `--read-only` session therefore
  sent write-permission requests (default Links view, Dashboard counts, default Export),
  which the server correctly refused (C7 held; no data exposure), and the 18 matrix test
  was arg-blind so it could not detect it.
- **Ruling (user, 2026-11-19):** **client preflight.** `ICommandClient` gains
  `required_permission(const CommandRequest&)`; `InProcessClient` evaluates the registry's
  real `max(base, permission_for(args))` (single source of truth, no duplicated rules in
  the TUI). `CapabilityModel::can_run(info, args)` uses it. A future remote client must
  provide an equivalent or fail closed. This extends contract C8's client interface.
- **Display ruling (user):** in read-only, Links defaults to status `active`; `all` /
  `deleted` choices render disabled with the `not_authorized` reason. Dashboard total /
  deleted counts show `n/a (read-only)`; active / disabled still load. Nothing hidden.

### 2026-11-19 - 06.0 close: open decisions and follow-ups

Task 06.0 closed (see per-task detail). Raised to the user at close,
pending a ruling unless marked:

1. `tui` has no `--path-root` / `--no-file-paths` (serve-stdio has both);
   a non-read-only local operator gets unconfined import/export paths.
   Read-only sessions cannot use paths (both path forms require `write`).
   LOW; parity recommended.
2. Manual smoke on Windows Terminal, legacy conhost, Linux and tmux needs a
   human run of `docs/testing/tui_manual_smoke.md` (criterion 1 of
   06.0/20); macOS pty pass is complete.
3. e2e 14-18 depend on GNU `timeout` (macOS needs a `gtimeout` shim).
4. `SettingsInfo` is a TUI-local mirror of `app::SystemInfo` because the
   C8 boundary forbids `url_shortener/app/` includes: ratify, or carve out
   `app/system_info.hpp`.
5. A future remote `ICommandClient` must override `required_permission`
   (base default under-estimates argument-dependent permission; the server
   still enforces). Carry to the remote-client work.
6. LOW: Ctrl-C needs a live UI thread (raw mode); a wedged loop leaves only
   SIGTERM. INFO: SIGHUP restore relies on FTXUI's handler only.
7. Carried from earlier tasks, still open: clang-format version pinning,
   vcpkg baseline for ftxui 7.0.3, 0002 constraints T-92/T-100/T-108, the
   SME transcript in `adding-a-transport.md`, the Y2038 SQLite read, and the
   co-author trailer variance in branch history.

**User rulings (2026-11-19):**

- (1) Add `--path-root` / `--no-file-paths` to `tui` in Task 07.0, wired
  into the runtime `file_paths`, with regression tests.
- (2) Ratified deferral: 06.0 stays ✅; the Windows Terminal, conhost, Linux
  and tmux rows of `docs/testing/tui_manual_smoke.md` are a deferred human
  pass.
- (3) The `e2e` 14-18 scripts get a portable timeout fallback (`timeout`,
  else `gtimeout`, else a watchdog) in Task 07.0.
- (4) `SettingsInfo` mirror ratified: it mirrors the `system.info` wire
  contract, not a code dependency.
- Items 5-7 stay recorded follow-ups.

### 2026-11-20 - 07.0 entry: as-built reconciliation (user rulings)

An as-built probe found Task 07.0 subtasks 22-29 largely satisfied by
06.0 under other names (`MockCommandClient`, `FormModel`, `d0N_*_80x24`
goldens, `tui__18_readonly_*`); specs 18-21 and 24 are stale against
01.0-06.0 (see
[docs/security/0006-07.0-tests-and-conformance-threat-brief.md](/docs/security/0006-07.0-tests-and-conformance-threat-brief.md)).
User rulings:

- **Amend specs, fill gaps:** specs 22-29 are rewritten against the as-built
  names, existing coverage is marked *consumed*, and only real gaps are
  built. Stems `20_`/`21_` are renumbered into the free 22-69 range.
- **Keep TUI redaction (defense in depth):** 07.0/28 test 62 drops the
  "TUI has no redaction logic" structural assertion. It instead asserts the
  server handlers are the primary control (real-registry frames contain no
  secret markers) and that the TUI filter also drops secret-named fields
  (T-125).
- Spec 20 also carries the `tui --path-root/--no-file-paths` product change
  (2026-11-19 ruling); spec 19 adds the `resolve_permission` raised-permission
  assertions (06.0/18 ruling).

### 2026-11-20 - 07.0/06 CLI response id in conformance (user ruling)

`url_shortener cmd` builds its own envelope and supplies the client id `cli-1`
when `--id` is absent (specified in 04.0/13). The conformance matcher's N4
rule expected any non-echoed id to be server-minted (`srv-<n>`), so the first
06 shim injected `--id srv-<n>`. Ruling: **the shim echoes the corpus request
id via the real `--id` flag** (the `cli_cmd` transport is treated as
echo-capable). For steps without an id, the matcher accepts the CLI's
documented `cli-<n>` client id. No product change. `docs/commands/README.md`
is amended to state the CLI's default client id.

### 2026-11-20 - 07.0/20 SSRF private-target bypass (user ruling)

The 07.0/20 security review found a pre-existing private-target bypass, and it
was reproduced on the real binary. With private targets disallowed,
`http://127.0.0.1/` is refused, but `http://2130706433/`,
`http://0x7f000001/`, `http://0177.0.0.1/` and `http://[::1]/` are accepted.
The cause is in `src/core/utils.cpp`: `isPrivateHost` matches by string
prefix, and the authority's port split breaks bracketed IPv6. The impact is
minting redirects to loopback/internal targets (CWE-918/601 filter bypass).
This is a redirect service and it does no server-side fetch. `master` is also
affected. **Ruling: fix now in 07.0**, as a separate `fix(security)` commit:
- normalize numeric IPv4 (decimal/octal/hex) before the private check;
- parse bracketed IPv6 before the port split;
- keep public numeric IPs accepted;
- assert the 07.0/20 SSRF vectors as explicit `invalid_url`.

**Resolution (2026-11-20):** fixed in `752dad8` and `5d970c8`; the tests are in `7eea03d` and `6a83c81`. Two review rounds found that the first fix still missed the trailing-dot, `%2e`, `localhost.` and IPv6-embedded forms, and those are now closed too. With private targets denied, `invalid_url` is now returned for:
- numeric IPv4 in decimal, hex, octal and short forms, including a trailing dot;
- bracketed and IPv4-mapped/compatible IPv6, NAT64 and 6to4 that embed a private v4;
- CGNAT, benchmark, multicast, reserved and broadcast ranges.

Public hosts are unchanged. One new policy: a `%` in the host is always refused (no percent-decoding), even when private targets are allowed. The redirect fast path is untouched (C10). `master` is also affected, and a backport decision is pending at the 07.0 close.
