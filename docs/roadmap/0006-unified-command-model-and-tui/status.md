# Milestone 0006 - Unified Command Model and TUI - Status

Tracks progress against [plan.md](/docs/roadmap/0006-unified-command-model-and-tui/plan.md).

## Current status

| Task | Name | Status | Tests |
|------|------|--------|-------|
| 01.0 | Command model core | ✅ Complete (13/13 subtasks) | 14 (`commands__01`-`14`) |
| 02.0 | Storage path unification | ✅ Complete (16/16 subtasks, incl. added 02.0/16) | 15 targets (`app__19`-`24`, `metadata__09`, `inmemory__16`, `storage__15` x2, http `30`/`31`, `cli__05`, `cli_03`/`17`/`18`, `e2e_12`) |
| 03.0 | Link and browse commands | ✅ Complete (18/18 subtasks) | 23 targets (`commands__20`-`30`, `app__40`-`48`, `core 09`, `memory__17`, `analytics__16`, storage list-events SQLite/Postgres, contract `10`-`12`) |
| 04.0 | REST and CLI adapters | ⬜ Not started | 0 |
| 05.0 | Jobs, pair import/export, stdio | ⬜ Not started | 0 |
| 06.0 | Terminal UI | ⬜ Not started | 0 |
| 07.0 | Tests and conformance | ⬜ Not started | 0 |
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
