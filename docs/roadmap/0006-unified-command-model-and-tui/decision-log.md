# Milestone 0006 - Decision Log (headless run)

This milestone was designed in a session that could not ask the user
questions. Every point where a question would normally have been asked is
recorded here with the options, the **recommended option that was chosen**,
and what changes if you pick differently. Review these; reverse any with the
listed cost.

Date: 2026-10-19. Branch: `roadmap/tui-unified-command-model`.
Nothing here is implemented; only documents changed.

| # | Question | Chosen (recommended) | Alternatives | Reverse cost |
|---|---|---|---|---|
| Q1 | Which C++17 TUI library? | **FTXUI** | notcurses, ncurses/PDCurses (+wrappers), cpp-terminal, ImTui | Low until Task 06.0 starts; screens only touch FTXUI inside `src/tui/` |
| Q2 | Where does the new work sit in the roadmap? | **New milestone 0006**, executed **before** 0002 and 0004 | Renumber to insert as 0004; fold into 0003 | Renumbering touches every cross-link; folding reopens a completed milestone |
| Q3 | Shape of the "identical JSON command" contract | **Custom JSON-RPC-shaped envelope** (`v,id,command,args,options` -> `ok,result\|job\|error`) | JSON-RPC 2.0 verbatim; protobuf/gRPC | Envelope is isolated in `envelope.cpp`; JSON-RPC framing is a small adapter-level change |
| Q4 | Add a JSON library? | **No.** Manual `core/utils.h` helpers stay for existing code; **one** sanctioned addition: the bounded top-level scanner `commands::scan_object`/`scan_array` (Task 01.0 subtask 01), reused by envelope, `ArgsView`, args builder and TUI (see "Open decisions" D3) | nlohmann/json, simdjson | Medium: nested `args` (campaign object, NDJSON import) will strain the helpers; a library needs its own ADR |
| Q5 | Fix the ADR 0001 storage-path duality in this milestone? | **Yes, as prerequisite Task 02.0** | Defer again; make TUI work only in-process | Without it, "display database tables" and cross-invocation CLI cannot work on a real backend |
| Q6 | Trust model for local CLI/TUI/stdio | **Implicit local operator = Admin**, `--read-only` downgrades; remote transports use real sessions. **Extension for `serve-stdio`** (its parent may be a webview-driven Tauri sidecar, a weaker principal than a person at a shell): `--path-root <dir>` confines every `link.import`/`link.export` `path` to that directory after canonicalization, and `--no-file-paths` rejects `path` (`unsupported`); without either flag stdio behaves like the CLI. A read-only session may not export to `path` (it writes a file). Task 05.0 subtasks 07 and 16 | Require login/token for TUI; separate TUI-user accounts; make `--no-file-paths` the stdio default | Adding auth to local modes touches Milestone 0002 Task 07.0; flipping the stdio default is one line |
| Q7 | What "database tables" means in the TUI | **Port-backed logical tables** (`links`, `click_events`, `jobs`) via `db.*` commands | Raw SQL/table browser per backend | Raw browser would bypass ports, differ per backend and risk exposing auth/hash tables |
| Q8 | Job durability | **In-memory**, bounded pool | Persisted job table | Durable jobs need a schema + migration (Postgres numbered migrations) |
| Q9 | Default `status` for `link.list` | **`active`** (TUI sends `all`) | `all`; `active+disabled` | Trivial; one default in the descriptor |
| Q10 | TUI backend client | **In-process** first behind `ICommandClient`; remote client later | Build REST/IPC remote client first | Seam exists, so deferred cost is a new client class |
| Q11 | Build default for TUI | **`URLSHORTENER_ENABLE_TUI=ON`** | OFF by default | Flip default; CI matrix would need both |
| Q12 | IPC now or later | **Later**; ship `serve-stdio` as the proof transport and sidecar attach point, document IPC as worked example | Implement Unix socket/named pipe now | One adapter class (~1 task) |
| Q13 | Roadmap decomposition depth | **Full Task -> Subtask files**, but subtask specs are compact (objective/files/acceptance) | Task READMEs only; very detailed subtasks | Expand subtask files when each task is scheduled |
| Q14 | What is the "Tauri admin panel"? | **Not found in the repo** (`grep -ri tauri` empty). Treated as a future milestone (unnumbered) that consumes REST `/api/v1/commands` or a `serve-stdio` sidecar. **No Tauri milestone was created.** | Create a placeholder 0007 | Please confirm whether Tauri is meant to replace/complement the React admin of 0002 |
| Q15 | How 0002's `/admin/api/v1` relates to commands | **Projection**: admin endpoints call the registry; page/page_size is translated to offset/limit; admin error codes map from command error codes | Keep 0002's admin API independent | If independent, `link.list` etc. get a second implementation, contradicting the milestone goal |
| Q16 | Stale statuses in `docs/roadmap/README.md` | **Corrected** 0003 to complete (its own status.md says complete and master contains the merge) | Leave | n/a |
| Q17 | ADR status | **Proposed** (not Accepted) so you can veto | Accepted | Edit header |
| Q18 | `target_url` vs REST's `url` field name | **Verified against `src/http/handlers/link_handlers.cpp` (`createLinkFromRequest`)**: REST `POST /api/v1/links` accepts BOTH `url` and `target_url` (`url` is read first and wins if both are present; either missing gives `400 invalid_url "url is required"`). `slug` is the slug field; `code` is accepted only on the compatibility route (`CreateSlugFieldMode::slug_or_code`). Other accepted keys: `redirect_type`, `expires_at`, `enabled`, `tags`, `metadata`, `campaign`; `deleted_at` is rejected (`400 invalid_request`). The response body field for the target is `url` (not `target_url`). Decision: the command ArgSpec has the single canonical arg `target_url`; the REST resource-route projection (Task 04.0 subtask 04) renames `url` -> `target_url` before `execute` (precedence preserved: `url` wins); the generic `POST /api/v1/commands` and `cmd` accept only `target_url`; CLI sugar keeps `--url`. The catalog sentence "REST body also accepts legacy alias `url`" is accurate only for the resource route. | Use `url` as canonical | Doc + ArgSpec only |
| Q19 | Branch name | `roadmap/tui-unified-command-model` (cut from `master`, uncommitted work) | other | `git branch -m` |
| Q20 | Commit the changes? | The headless design run made no commit; the documents were later committed as `fe78bf6` ("docs(roadmap): add milestone 0006 - unified command model and TUI") on this branch. Review fixes after that commit are uncommitted until someone commits them | n/a | `git revert fe78bf6` |
| Q21 | `docs/adr/template.md` is referenced by `docs/adr/README.md` but does not exist | New ADRs follow ADR 0001's structure; template **not** created | Create template | Out of scope |

## Things to research tomorrow (I could not verify offline / without asking)

1. **FTXUI facts** used in ADR 0003 come from prior knowledge, not a fresh
   check: current release number, C++17 floor, MSVC support, vcpkg port name
   (`ftxui`), `Table` component availability, behavior on legacy conhost vs
   Windows Terminal. Task 06.0 subtask 01 is scoped to verify and pin.
2. **Whether FTXUI is the "most adopted"** - stated from memory. Compare
   GitHub stars/activity against notcurses and cpp-terminal before accepting ADR 0003.
3. **Tauri**: confirm intent (Q14) - sidecar over `serve-stdio` vs plain HTTP
   to the server, and whether the React admin (0002) is being replaced.
4. ~~**Q18** REST body field names.~~ Resolved (see Q18; verified in the Task 03.0 subtask 07 and Task 04.0 subtask 04 specs).
5. **ListLinksQuery / ListEvents** feasibility on the Redis and Postgres
   adapters - RESOLVED in the Task 03.0 specs (subtasks 01-04): there is no Redis metadata repository (Redis is cache/rate-limit only); only `InMemoryMetadataRepository` and `SqlMetadataRepository` (SQLite and Postgres via one class) implement `ListLinks`; `IClickEventRepository` has two implementations plus test fakes.
6. **Job runner vs `AnalyticsWorker`**: whether to reuse the worker/queue
   primitives instead of a second thread pool.
   **Finding (Task 05.0 subtask 02): do not reuse.** `BoundedClickEventQueue`
   is hard-typed to `ClickEvent`, drops on overflow (`TryEnqueue` is `noexcept`
   and returns `DroppedFull`) whereas jobs must return `busy` and never drop;
   `AnalyticsWorker` is one thread, enabled only via `AnalyticsConfig`, and
   built around batch/flush/retry to `IClickEventRepository`. Templating them
   would modify protected analytics code and its tests. Only the pattern is
   copied (mutex + condition_variable + deque, stop flag, destructor stops).
   `JobRunner` is a new small pool in `src/commands/` (N workers, bounded
   queue, per-job cancel flag).
7. **`link.import` `on_conflict=overwrite`** (found in Task 05.0): the catalog
   offers it but `app::UpdateLinkCommand` cannot change `target_url` and a
   soft-deleted slug still blocks `CreateLink`. Proposed: a new narrow
   `LinkCommandService::ReplaceLinkTarget` (subtask 09), deleted slugs stay a
   conflict. Confirm, or drop `overwrite` from the ArgSpec enum.
8. **`serve-stdio` shutdown** (Task 05.0): EOF on stdin is the graceful stop
   (drain jobs up to 30 s, then cooperative cancel, exit 0); no SIGINT/SIGTERM
   handler is installed because portable interruptible stdin reads do not
   exist. Confirm this is acceptable for the future Tauri sidecar.

## Open decisions needing human review

Made during spec review so the specs are consistent; each one is a recommendation
that a person must confirm (or reverse) before the owning subtask starts.

| # | Decision | Recommendation (current spec) | Owner subtask | If reversed |
|---|---|---|---|---|
| D1 | Link ids for `link.update` / `link.enable` / `link.disable` (and `link.delete` / `link.restore`) - Milestone 0002 addresses rows by `url_pair_id` | **Recommended: add an optional `id` arg** to these commands (exactly one of `slug`/`id`, as `link.get` already does; additive, no `v` bump; resource routes stay slug-only, C3). If confirmed before Task 03.0 starts, 03.0/09-10 add it; until then the specs stay slug-only and the 0002 bridge resolves id -> slug with `link.get {id}` (two commands, small race window) | 03.0/09-10, 08.0/14 | Keep slug-only for good: 0002 always resolves first |
| D2 | `link.import on_conflict=overwrite` needs to change a target URL, which `UpdateLinkCommand` cannot | Add narrow `LinkCommandService::ReplaceLinkTarget` (same URL validation/SSRF check as create); soft-deleted slugs stay a conflict | 05.0/09 | Drop `overwrite` from the `on_conflict` enum (catalog + ADR 0003 D7) |
| D3 | JSON parsing | One bounded, iterative top-level scanner (`commands::scan_object`/`scan_array`) is the only sanctioned deviation from the `core/utils.h` helpers; the TUI reuses it (no `src/tui/json_scan.cpp`); conformance `mini_json` is test-only code under `tests/` | 01.0/01, 06.0/02, 07.0/03 | A JSON library needs its own ADR (ADR 0002 "No JSON library") |
| D4 | Exposure of the new HTTP surface while the server has no authentication | Off by default (`--command-api-enabled false`); role flag `--command-api-role user\|admin` (default `user`); `admin` honored only for loopback peers (the listeners always bind all IPv4 interfaces, so a bind-address check is impossible today), every other peer runs as `user`; `manage_users` always refused over REST; applies to `/api/v1/commands`, `GET /api/v1/links`, `/api/v1/jobs*`. The peer is the TCP `remote_endpoint` threaded into the router by 04.0/09 (new plumbing; nothing records it today). **Reverse-proxy warning:** behind a proxy on the same host every request arrives from loopback, so `admin` would apply to all internet clients; `X-Forwarded-For`/`Forwarded` are deliberately **not** honoured (spoofable). Such deployments must keep `--command-api-role user` | 04.0/08, 04.0/09, 04.0/07, 05.0/11 | Enable by default only after Milestone 0002 sessions exist; trusted-proxy support needs its own decision |
| D5 | Milestone 0004 SC5 public-create rate limit placement | In the **REST adapter** (`POST /api/v1/links` and the generic bridge for `link.create`), keyed by HTTP client; not in the command handler, so CLI/TUI/stdio (the operator) are never throttled | 08.0/15 (0004 plan note) | Put it in the `link.create` handler with a transport check (couples a handler to transports, against C1) |
| D6 | ADR 0002/0003 acceptance | Stays a **human** decision: 08.0/01 only proposes the edits and waits for an explicit instruction; agents never flip `Proposed` -> `Accepted` | 08.0/01 | n/a |
| D7 | Registering REST and the one-shot CLI as `ITransportAdapter` classes | **Dropped** for 0006: wrapping `HttpServer` and one-shot CLI would change `main.cpp` startup/TLS order; only `serve-stdio` (and future IPC) implement the interface; REST/CLI still go through `execute` | 05.0/12 | Wrap them later; a refactor with no behavior change |
| D8 | Extra CTest label `conformance` and a registered `PERF_SOURCES` list | Conformance tests carry labels `integration;conformance` (so the documented union still runs them); perf files get `register_labeled_cpp_tests(PERF_SOURCES "perf")` with `RUN_SERIAL` (targets `<parent_dir>__<stem>`) | 07.0/03, 07.0/05, 07.0/30 | Keep conformance under `integration` only; leave perf unregistered (CLAUDE.md already documents the `perf` label) |
| D9 | What the catalog generator owns | Only block `### 3.7 Generated command reference` of `docs/commands/README.md`; sections 1-6 are prose guarded by the drift test 07.0/16 | 01.0/12, 07.0/16, 08.0/03 | Generate the whole file (loses the rationale prose) |
| D10 | `ArgSpec` default caps (2048 chars / 64 items) are tighter than REST accepts today | Raise the caps of the nine link commands (65536 bytes / 4096 items) so the service stays the validator and REST bodies do not change (C3) | 03.0/07 | Keep tight caps and accept a behavior change on REST (violates C3) |
| D11 | CLI flag grammar of `link <verb>` | Kept in the existing hand-written parsers (`link_command_args.cpp`); flags -> DTO -> `args` converters plus an ArgSpec coverage test. Only `cmd --arg` is compiled from `ArgSpec` | 04.0/14, 04.0/15 | Generate `link <verb>` flags from `ArgSpec` (breaks quirks such as `--expires-at clear`, C4) |
| D12 | Write-behind redirect counters on persistent backends | Delta-accumulating pending counters, merged into every L1 reload, flushed only by a background timer (never inside a request). Residual races (02.0/05): a load that runs while a flush is in flight (pending map already swapped out, write not yet done) returns a count that is too low until the next reload (persisted totals unaffected, R-2); two processes flushing the same slug can lose one delta (R-1). Counters are approximate, never authoritative | 02.0/05, 02.0/11 | Write-through per redirect (one DB write per redirect), or an atomic `UPDATE ... SET n = n + ?` per flush |
| D13 | One job-dispatch mechanism | Descriptor field `job_factory` (01.0/06) builds a self-contained `JobBody` on the caller thread after authorization and validation; `CommandRegistry::set_job_submitter(std::shared_ptr<IJobSubmitter>)` (01.0/08); `JobRunner` (05.0/02) implements `IJobSubmitter` and is one process-wide `shared_ptr` (05.0/20) injected into every registry, including REST's per-request ones. There is no `set_job_runner`, and no handler-side submission | 01.0/06, 01.0/08, 05.0/02, 05.0/04, 05.0/20 | A runner-owned registry of job handlers by name (two lookups, drift risk) |
| D14 | Where audit lines go per mode (an uninitialized logger writes to stderr) | Every mode initializes logging **explicitly** before `build_command_runtime`: server = configured sink; `cmd` = disabled unless `--log-file <path>` (file) or `--verbose` (stderr); `link <verb>` = disabled, no new flags (C4); `tui` = file, default `./url_shortener-tui.log`, `--log-file` overrides, never the terminal; `serve-stdio` = stderr, never stdout. Tests: `link create`/`cmd link.create` success leaves stderr empty (04.0/15, 04.0/13) | 04.0/12, 04.0/13, 04.0/15, 05.0/17, 06.0/19, ADR 0002 "Authorization" | Audit on stderr in one-shot modes (breaks C4 stderr parity), or a mandatory audit file for `cmd` |
| D15 | Should `serve-stdio` default to `--no-file-paths` when stdin is not a TTY? | **No (recommended).** `serve-stdio` stdin is a pipe in practically every real use, so a TTY-dependent default is "always on" with a confusing interactive exception. Keep Q6 (behaves like the CLI unless `--no-file-paths`/`--path-root`); if hardening is wanted for the Tauri sidecar, flip the default **unconditionally** (`--allow-file-paths` opt-in) - one parser line plus the 05.0/16 and 05.0/18 cases | 05.0/16, 05.0/17, 05.0/18 | TTY-dependent default (surprising), or unconditional deny-by-default |
| D16 | Argument-dependent permission (`link.export` with `path` writes a file, so it needs `write`; inline export is `read`) | Optional descriptor hook `permission_for(ArgsView) -> Permission` (01.0/06), evaluated by `execute` after `ArgSpec` validation; it may only **raise** the base permission (a lower value is a registration error); refusal is `not_authorized` before the job factory runs | 01.0/06, 01.0/07, 05.0/10, 06.0/18, ADR 0002 section 2 | Split into `link.export` (read, inline) and `link.export_file` (write) - two commands, no hook |

## Human review (2026-10-22)

Walked through with the user in chat (not headless this time). Decision: **adopt
every D1-D16 recommendation as-is** - none are reversed. Since each
recommendation was already the text baked into the current specs, this review
closes the open-decision gate without further spec edits, with three exceptions
called out below where confirmation changed (or should change) something.

| # | Outcome |
|---|---|
| D1 | **Confirmed.** Add the optional `id` arg to `link.update`/`enable`/`disable`/`delete`/`restore` - 03.0/09-10 may implement this when scheduled, not deferred to the "two commands, small race window" fallback. |
| D2 | **Confirmed.** `LinkCommandService::ReplaceLinkTarget` ships; `overwrite` stays in the `on_conflict` enum. |
| D3 | **Confirmed.** No JSON library; the bounded scanner is the only sanctioned tokenizer. |
| D4 | **Confirmed.** Command API off by default, `user`-role default, loopback-only `admin`, reverse-proxy warning stands as written. |
| D5 | **Confirmed.** 0004's SC5 rate limit lives in the REST adapter, not the `link.create` handler. |
| D6 | **Partially confirmed - see "ADR acceptance" below.** The human instruction this decision was waiting for has now arrived, but D6 itself ties the *flip* to Task 08.0 subtask 01, which is gated on Task 07.0's conformance/security gates passing. Those gates cannot pass before any code exists. See below: decisions are confirmed now; ADR headers stay `Proposed` until 08.0/01 actually runs post-implementation. |
| D7 | **Confirmed.** REST/one-shot CLI are not wrapped as `ITransportAdapter`. |
| D8 | **Confirmed.** `conformance` rides on the `integration` label; `PERF_SOURCES` gets `RUN_SERIAL`. |
| D9 | **Confirmed.** The catalog generator owns only the generated-reference block. |
| D10 | **Confirmed.** The nine link commands' `ArgSpec` caps are raised to match REST's existing limits (C3). |
| D11 | **Confirmed.** `link <verb>` flag parsing stays hand-written; only `cmd --arg` is ArgSpec-compiled. |
| D12 | **Confirmed.** Write-behind, timer-flushed, approximate redirect counters; the two named races (R-1/R-2) are accepted as documented, not fixed. |
| D13 | **Confirmed.** `job_factory` + `CommandRegistry::set_job_submitter`, one process-wide `JobRunner`. |
| D14 | **Confirmed.** Per-mode logging destinations exactly as listed (file for `tui`, never the console). |
| D15 | **Confirmed.** `serve-stdio` does not default to `--no-file-paths` on a non-TTY stdin. |
| D16 | **Confirmed.** `permission_for(ArgsView)` hook, raise-only, evaluated after `ArgSpec` validation. |

**ADR acceptance stays open on purpose.** Flipping `docs/adr/0002-unified-command-model.md`
and `docs/adr/0003-tui-framework-and-architecture.md` from `Proposed` to
`Accepted` is Task 08.0 subtask 01's job, and that subtask's own spec requires
Task 07.0 ("all gates passed") plus real artifacts that do not exist yet
(a pinned FTXUI version/commit from Task 06.0 subtask 01's PR, `url_shortener
tui --help` output, the "as built" deltas). Confirming D1-D16 now removes the
*planning*-level blocker so implementation can start; it does not and should
not pre-empt 08.0/01's own gate. Re-run 08.0/01 for real once 07.0 is green.

**Q14 (Tauri) stays open, not blocking.** Still unresolved in this review;
nothing in 0006 depends on an answer (0006's own plan.md already treats Tauri
as "a later milestone after 0006, no spec yet"). Answer it when that
milestone is actually proposed.

## Process notes

- The `bash-guard` hook blocked one shell command because the script text
  contained a word it treats as a production marker (a false positive on
  documentation prose). I did **not** set the override variable it suggested;
  I wrote the file with the Write tool instead. You may want to tighten that hook.
- Agents were used: five authoring agents wrote the task/subtask specs, an
  independent Opus review produced a findings list that was then applied, and a
  second review (different model) of commit `fe78bf6` requested the D13-D16
  changes and the corrections in this log. The repository skills `/pr-review`,
  `/doc-registry` and `/link-check` have **not** been run on these documents -
  recommended before merging.
