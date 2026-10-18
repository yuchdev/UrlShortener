# Task 05.0 - Jobs, pair import/export and stdio transport

**Parent milestone:** [Milestone 0006 - Unified Command Model and TUI](/docs/roadmap/0006-unified-command-model-and-tui/plan.md)
**Status:** ⬜ Not started
**Depends on:** 01.0 (envelope 03-04, registry 06-08), 03.0 (codec/link commands 05-07, list ports 01/12, `link.list` 14, table port 15, runtime builder 18); 02.0/12 (shutdown order in `main.cpp`); 04.0/02, 04.0/08, 04.0/09 only for subtask 11

## Scope

Add the job runtime (catalog section 4) and everything that rides on it:

1. **Jobs** - `JobStore`, `JobRunner` (bounded pool and queue, cooperative cancel, drain/shutdown), `kind=job` dispatch in the registry (one mechanism, decision log D13: descriptor `job_factory` -> `IJobSubmitter::submit`, `JobRunner` implements `IJobSubmitter` and is injected as `std::shared_ptr` with `set_job_submitter`; subtask 04 proves it with the real runner) and the `job.get|list|cancel` commands (+ REST `/api/v1/jobs*` rows, gated like the generic endpoint).
2. **Pairs** - `link.import` (CSV and NDJSON, `skip`/`fail`/`overwrite`, dry-run, capped error list) and `link.export`, with all file access isolated and path-checked.
3. **Composition** - one process-wide `JobRunner` and the job/pair commands and the `jobs` table wired into `composition::build_command_runtime` (subtasks 20-21), so every mode (server, `cmd`, `tui`, `serve-stdio`) gets the same registry (plan C11). No subtask of this task builds its own registry.
4. **stdio transport** - `ITransportAdapter`, the NDJSON framing, `NdjsonSession`, `StdioAdapter`, the `serve-stdio` CLI mode (with `--read-only`, `--path-root`, `--no-file-paths`) and its process-level tests, plus the `docs/commands/adding-a-transport.md` guide with an IPC worked example (design only; IPC is not implemented, decision Q12).

Decisions made in this task: `JobRunner` does not reuse `AnalyticsWorker`/`BoundedClickEventQueue` (finding in subtask 02 and decision-log research item 6); `overwrite` needs a new service method (subtask 09, open decision D2); EOF on stdin is the only graceful stop of `serve-stdio` and no signal handlers are installed (subtask 15); REST and the one-shot CLI are not wrapped as `ITransportAdapter` (subtask 12, open decision D7). `cmd` job waiting is a follow-up edit to 04.0/13 owned by subtask 20 (no hard edge). `link.export` with `path` needs `write` through the descriptor's `permission_for` hook (subtask 10, D16).

## Dependency graph

```text
01 job types/store -> 02 runner pool -> 03 wait/shutdown -----------------------------+
                          |   \                                                        |
                          |    +-> 21 jobs table source (+03.0/15) ----------------+   |
                          +-> 04 registry job dispatch -> 05 job.* cmds ------------+   |
                                      |                                             |   |
06 line parsers --+                   +--------------+                              v   v
07 file access ---+--> 08 link.import --> 09 overwrite ------------------> 20 process job runtime
      |            \--> 10 link.export ------------------------------------>   in build_command_runtime
      |                                                                       (+03.0/18, 02.0/12)
      |                                                                          |        |
      |                                                      11 REST job routes <-+        +-> 04.0/13 cmd, 06.0/19 tui
      +--> 13 NDJSON line reader -> 14 NdjsonSession ----+   (needs 04.0/02, 08, 09)
12 ITransportAdapter + factory ----------------------------+--> 15 StdioAdapter (needs 03)
16 CLI parser serve-stdio ---------------------------------+--> 17 runtime wiring (needs 12, 15, 16, 20)
                                                                  |
                                                         18 process tests -> 19 adding-a-transport guide
```

## Subtasks

| # | Document | Status | Depends on | Blocks |
|---|----------|--------|------------|--------|
| 01 | [Job types and JobStore](01-job-types-and-store.md) | ⬜ Not started | none | 02, 05 |
| 02 | [JobRunner worker pool, queue, cancel](02-job-runner-worker-pool.md) | ⬜ Not started | 01, 01.0/08 | 03, 04, 05, 21 |
| 03 | [JobRunner wait, drain, shutdown](03-job-runner-wait-and-shutdown.md) | ⬜ Not started | 02 | 15, 20 |
| 04 | [Registry job dispatch](04-registry-job-dispatch.md) | ⬜ Not started | 02, 01.0/06, 01.0/07, 01.0/08 | 05, 08, 10, 20 |
| 05 | [job.get, job.list, job.cancel](05-job-commands.md) | ⬜ Not started | 01, 02, 04 | 11, 20, 06.0/15, 06.0/16, 06.0/17 |
| 06 | [Pair line parsers and error list](06-pair-line-parsers.md) | ⬜ Not started | none | 07, 08 |
| 07 | [Pair file access and path rules](07-pair-file-access.md) | ⬜ Not started | 06 | 08, 10, 13 |
| 08 | [link.import (skip/fail, dry-run)](08-link-import-command.md) | ⬜ Not started | 04, 06, 07, 03.0/05, 03.0/06, 03.0/07 | 09, 10, 20, 06.0/15, 06.0/20 |
| 09 | [link.import overwrite](09-link-import-overwrite.md) | ⬜ Not started | 08 | 20 |
| 10 | [link.export](10-link-export-command.md) | ⬜ Not started | 04, 07, 08, 03.0/01, 03.0/12, 03.0/14 | 20, 06.0/15, 06.0/20 |
| 11 | [REST job routes](11-jobs-rest-routes.md) | ⬜ Not started | 05, 20, 04.0/02, 04.0/08, 04.0/09 | none |
| 12 | [ITransportAdapter and factory](12-transport-adapter-interface.md) | ⬜ Not started | 01.0/06, 01.0/07 | 15, 17, 19 |
| 13 | [NDJSON line reader](13-ndjson-line-reader.md) | ⬜ Not started | 07 | 14 |
| 14 | [NdjsonSession](14-ndjson-session.md) | ⬜ Not started | 13, 01.0/01, 01.0/03, 01.0/04, 01.0/07 | 15, 19 |
| 15 | [StdioAdapter, drain, shutdown](15-stdio-adapter-and-shutdown.md) | ⬜ Not started | 03, 12, 14 | 17, 19 |
| 16 | [CLI parser: serve-stdio](16-cli-parser-serve-stdio.md) | ⬜ Not started | none | 17 |
| 17 | [serve-stdio runtime wiring](17-serve-stdio-runtime-wiring.md) | ⬜ Not started | 12, 15, 16, 20 | 18 |
| 18 | [serve-stdio process tests](18-serve-stdio-process-tests.md) | ⬜ Not started | 17 | 19, 07.0/07 |
| 19 | [adding-a-transport guide](19-adding-a-transport-guide.md) | ⬜ Not started | 12, 14, 15, 18 | 08.0/04 |
| 20 | [Process-wide job runtime in `build_command_runtime`](20-process-job-runtime-composition.md) | ⬜ Not started | 03, 04, 05, 08, 09, 10, 21, 02.0/12, 03.0/18 | 11, 17, 06.0/19 |
| 21 | [`jobs` table source](21-jobs-table-source.md) | ⬜ Not started | 02, 03.0/15 | 20 |

Parallel lanes once 01.0 and 03.0 are ready: A = 01-05, 21 (jobs), B = 06-10 (pairs, 08/10 need A's 04), C = 12-16 (transport; 15 needs 03); 20 joins A and B into the shared runtime, then 11 and 17. 06.0 (TUI) needs 05, 08, 10 (command contracts) and 20 (runtime, for 06.0/19).

## Key constraints

- Bounded worker pool and queue; a full queue returns `busy`, nothing is dropped silently (subtasks 02, 04).
- One process-wide `JobRunner`; job bodies capture only shared ownership and values, never a per-request `CommandRuntime` (subtask 20).
- Import rows go through `LinkCommandService::CreateLink` only - no second validation, SSRF or slug path (08); `overwrite` uses one new service method that reuses `normalizeTargetUrl` (09).
- `path` args are rejected on REST (`unsupported`); local file access only for CLI/TUI/stdio, with UNC/device paths rejected, size caps, create-new atomic export (07); `serve-stdio --path-root` confines and `--no-file-paths` disables `path` (decision Q6); a read-only session may not export to `path`.
- Job state is in-memory; durable jobs are a non-goal. Bodies must poll the cancel flag once per row/page (no forced kill).
- stdout of `serve-stdio` carries response envelopes only; logs are forced to stderr (17); one response line per content line, strict order (13, 14).
- C5: `serve-stdio` never constructs `HttpServer`, binds a socket or touches `uri.txt`; C10: the redirect fast path is not touched by any subtask.
- Build: new `.cpp` go into `COMMON_CPP_SOURCES` in `sources.cmake` (the list feeding `url_shortener_common`), tests into `COMMANDS_UNIT_SOURCES`/`APP_UNIT_SOURCES`/`CLI_UNIT_SOURCES`/`HTTP_UNIT_SOURCES`/`CLI_INTEGRATION_SOURCES`/`E2E_CLI_SECTIONS`. Test target names: `commands__<stem>`, `app__<stem>`, `cli__<stem>`; `tests/unit/http` tests use the bare stem. Test prefixes (plan.md "Test file prefixes"): `tests/unit/commands` 50-69 (uses 50-65), `tests/unit/app` 60-69 (uses 60), `tests/unit/cli` 20-29 (uses 20-21), `tests/unit/http` 40-49 (uses 40), `tests/integration/cli` 40-49 (uses 40-41).
- C++17 only, no JSON library (the only tokenizer is `commands/json_scan.hpp`, 01.0/01), naming per `.clang-tidy` (lower_case methods/members/enumerators, `m_` private prefix; type names follow ADR 0002 CamelCase), format per `.clang-format`.

## Exit criteria

1. All 21 subtasks ✅ and their tests green: `ctest --test-dir cmake-build -L "unit|contract|integration|e2e" --output-on-failure`.
2. A 10k-row CSV import runs as a job while other commands keep answering; cancel keeps partial results; export -> import round trip is lossless for both formats.
3. `printf '<requests>' | url_shortener serve-stdio` returns one valid envelope line per request, survives malformed lines, exits 0 on EOF after draining jobs, and leaves no listening port.
4. A job submitted with `POST /api/v1/commands` is readable with `GET /api/v1/jobs/{id}` in a later request (process-wide runner), both routes gated by `--command-api-enabled`.
5. `docs/commands/adding-a-transport.md` exists, links resolve (`/link-check docs/commands`), and its framing/shutdown tables match the code.
6. `status.md` row 05.0 updated and `/verify-subtask` reports PASS for every subtask.

## Risks

- Some 01.0/03.0 symbol names used here are still UNVERIFIED (written concurrently); the subtask references are current, reconcile names when scheduling.
- Wrapping REST and CLI as `ITransportAdapter` classes was dropped from this task (subtask 12 rationale, decision D7); revisit in 07.0 if the conformance runner wants a uniform shim.
- `serve-stdio` shares CLI parser/dispatch files with 04.0/12 (`cmd` parsing); land in sequence.
