# 08 - link.import command (skip / fail policies, dry-run, errors list)

**Parent task:** 05.0 Jobs, pair import/export and stdio transport
**State:** ⬜ Not started
**Depends on:** 05.0/04, 05.0/06, 05.0/07, 03.0/05 (`link.create` args codec and error-code mapping), 03.0/06 (`ValidateCreateLink`, create without persisting), 03.0/07 (`link.create` `ArgSpec`)
**Blocks:** 09, 10, 20, 06.0/15 (Import screen), 06.0/20 (manual import check)
**Estimated size:** M
**Suggested agent:** cpp-expert (tests: testing-expert; security review: security-auditor)

## Objective

Register `link.import` (catalog section 3.3): a `kind=job`, mutating, `write`-permission command that reads `full_url -> short_url` pairs from inline `data` or a local `path` in `csv` or `ndjson`, creates each row through `LinkCommandService::CreateLink`, honours `on_conflict=skip|fail` and `options.dry_run`, reports cooperative-cancel partial results and returns the documented counts with at most 100 per-line errors. `overwrite` is handled by subtask 09.

## Dependencies

- 05.0/04 (job dispatch), 06 (parsers), 07 (`LineSource`, limits).
- Real symbols verified: `app::LinkCommandService::CreateLink(const CreateLinkCommand&) const -> Result<LinkView>`, `app::CreateLinkCommand {target_url, slug, redirect_type, expires_at, enabled, tags, metadata, campaign}`, `app::AppErrorCode`, `app::LinkCommandServiceBundle::service` (`std::shared_ptr<LinkCommandService>`). `CreateLink` already does URL normalization, SSRF host check (`normalizeTargetUrl`/`isPrivateHost`), slug validation, reserved-slug and conflict checks, and generates a slug when `slug` is nullopt.
- Specified elsewhere (names to reconcile when implementing): the shared `link.create` args decoding (`ArgSpec` of 03.0/07 + codec of 03.0/05, `args JSON -> CreateLinkCommand`), the `AppErrorCode -> error.code` mapping (`from_app_error`, 01.0/02, via `to_command_error` in 03.0/05), and the non-persisting validation entry `LinkCommandService::ValidateCreateLink` (03.0/06). Do not add a second validator here.
- `PathPolicy` (05.0/07) is part of `PairCommandDeps` (`file_paths`), filled by `build_command_runtime` (05.0/20).
- Test double: a local fake `ILinkStore` (pattern of `tests/unit/app/app_test_fakes.hpp`; that directory is not on the include path of `tests/unit/commands/`, so add `tests/unit/commands/commands_test_fakes.hpp` instead of changing CMake include paths).

## Files affected

| Path | Action | What changes |
|---|---|---|
| `include/url_shortener/commands/pair_commands.hpp` | create | `PairCommandDeps`, `register_pair_commands` |
| `src/commands/pair_commands.cpp` | create | descriptor, job factory, import body |
| `tests/unit/commands/commands_test_fakes.hpp` | create | `FakeLinkStore`, `FakeStatsReader`, blocking hooks |
| `tests/unit/commands/57_link_import_command.cpp` | create | cases below |
| `sources.cmake` | modify | add to `COMMON_CPP_SOURCES` (feeds `url_shortener_common`; the `URL_SHORTENER_CPP` list at the top of the file is legacy): `src/commands/pair_commands.cpp`; test into `COMMANDS_UNIT_SOURCES` |

## Classes and functions

```cpp
namespace url_shortener::commands {
struct PairCommandDeps {
    std::shared_ptr<app::LinkCommandService> service;   // shared with link.* handlers
    std::size_t max_inline_bytes = 65536;               // ServerConfig::max_request_body_bytes
    PathPolicy file_paths;                              // serve-stdio --path-root / --no-file-paths (07, 20)
    // export dependencies are added by subtask 10
    // Job bodies copy `service` (shared_ptr) and values; they never capture a CommandRuntime (20).
};
void register_pair_commands(CommandRegistry& registry, PairCommandDeps deps);
}
```

Descriptor: name `link.import`, perm write, kind job, mutating, dry_run supported, `ArgSpec`s exactly as the catalog (`format` enum required, `data` string, `path` string, `on_conflict` enum default `skip`).

Job factory (caller thread, fails fast, queues nothing on error):

1. Exactly one of `data`/`path` -> else `invalid_field`.
2. `path` present and the file-path capability is absent (`deps.file_paths.allow_paths == false`) -> `unsupported` ("file paths disabled"), a value-free message, **independent of transport name** (T-95); REST obtains this by default-deny (07 rule 6, 20). The factory performs **no** `transport == "rest"` check for `path` - the refusal is driven by the capability alone, so any future transport that does not grant it inherits deny. (The `on_conflict=overwrite` REST restriction below is a separate capability and is unchanged.) Caller transport REST and `on_conflict=overwrite` -> `unsupported` (catalog: overwrite is not offered by REST). Any caller and `overwrite` before subtask 09 -> `unsupported`.
3. `LineSource::from_inline(data, deps.max_inline_bytes)` / `from_path(path, deps.file_paths)`; map `PathError` to `CommandError` (the `from_path` `unsupported` from step 2 surfaces here when the factory delegates the capability check to the policy).
4. Return a `JobBody` that owns the `LineSource` (moved), format, policy, dry-run flag and the service `shared_ptr`.

**Dry-run executes synchronously, not as a job (01.0 T-21, T-91).** `link.import` declares `supports_dry_run`; when `options.dry_run` is true the registry runs this same import body **synchronously on the request thread** with a `NoopJobContext` (whose `cancel_requested()` is always `false` and whose `advance`/`set_total` are no-ops) and returns a `result` envelope, **never a `job`** and never a job record. The dry-run body performs **no** `CreateLink` persistence - it validates each row with `ValidateCreateLink` (03.0/06) plus the in-file duplicate-slug set - and stays bounded by `kMaxImportFileBytes` / `kMaxImportLineBytes` / `kMaxImportRows` exactly as a real run (T-97). A real (non-dry-run) run is submitted as a job and executes on a worker thread; the factory builds the identical `ImportPlan`, so only the job context and the persistence call differ.

Body (worker thread), per row:

1. If `ctx.cancel_requested()` stop with `cancelled=true` (counts so far are kept in `result_json`).
2. `LineSource::next` with `kMaxImportLineBytes`; `too_long` -> count `failed`, `errors.add(line, "invalid_field")`, continue; `io_error` -> job `failed` / `internal`.
3. `PairLineParser::parse`; `skipped` -> continue; `error` -> `failed`.
4. Row count above `kMaxImportRows` -> job `failed` / `invalid_field` "too many rows" with partial counts.
5. Build `CreateLinkCommand` (csv: `target_url`, optional `slug`; ndjson: shared args decoder; decoder failure -> `failed`/`invalid_field`).
6. dry-run: validate without persisting, plus an in-file duplicate-slug set (bounded by `kMaxImportRows`) so a repeated slug in the file is reported as `slug_conflict` exactly as a real run would; real run: `service->CreateLink`.
7. Outcome: ok -> `created++`; `slug_conflict` -> `skip`: `skipped++`; `fail`: abort the job `failed` / `slug_conflict` (counts so far kept); any other error -> `failed++` and `errors.add(line, code)`.
8. `ctx.advance(1)` after each row; `set_total` only for inline sources (count of `\n`), otherwise total stays 0 = unknown.

Result JSON (additive over the catalog): `{"total":n,"created":n,"skipped":n,"failed":n,"errors":[{"line":n,"code":"invalid_url"}],"errors_omitted":n,"dry_run":false}`. `total` counts non-blank data rows processed.

## Implementation steps

1. Implement `register_pair_commands` and the `link.import` descriptor; expose `ArgSpec` enums once.
2. Implement the factory with the checks above; unit-test each rejection before writing the body.
3. Implement the body as a free function in the anonymous namespace taking a small `ImportPlan` struct, so it is testable without the registry.
4. Wire result serialization (manual, `jsonEscape`), reuse `ImportErrorList::to_json`.
5. Write the fake store with an optional hook `on_create(n)` that can block on a latch.
6. Tests through the registry and a real `JobRunner` (`worker_count=2`).
7. Review: confirm by grep that `pair_commands.cpp` calls neither `isPrivateHost` nor `normalizeTargetUrl` nor touches the store directly.

## Commands

```text
cmake --build cmake-build --target commands__57_link_import_command
ctest --test-dir cmake-build -R "^commands__57_link_import_command$" --output-on-failure
```
Windows/multi-config: `--config Debug` / `-C Debug`.

## Tests

| Test file | Target | Label | Cases |
|---|---|---|---|
| `tests/unit/commands/57_link_import_command.cpp` | `commands__57_link_import_command` | unit | mixed good/bad CSV: counts (created/skipped/failed/total) and per-line errors with correct 1-based physical line numbers; blank slug generates a slug (created link has a non-empty slug); header row skipped; NDJSON happy path with tags/campaign; NDJSON unknown key -> `failed` with `invalid_field`; private-target row (`http://127.0.0.1/`) -> `invalid_url` (single SSRF path), with `ServerConfig::shortener_allow_private_targets=false` (verified field); with `true` the same row is created (proves the import honours the same switch); conflict with `skip` -> `skipped`; with `fail` -> job `failed`/`slug_conflict` and prior rows persisted; repeated slug inside one file; 150 bad rows -> `errors` has 100 and `errors_omitted` 50; `kMaxImportRows+1` rows -> job `failed`; over-long line counted as failed and next line processed; dry-run: counts as a real run, store unchanged, in-file duplicate slug detected; dry-run returns a synchronous `result` envelope (not a `job`) and creates no job record, its `NoopJobContext` reports `cancel_requested()==false` (not cancellable), the fake store records zero `CreateLink` calls (no mutation), and a `kMaxImportRows+1` dry-run still fails bounded with partial counts (boundedness, T-91/T-97); cancel mid-import (gate at row 50 of 100): state `cancelled`, `created==50`, partial result present and store holds 50 links; caller with `deps.file_paths.allow_paths==false` and `path` -> `unsupported` and nothing queued, for a REST runtime and for a non-"rest" transport runtime alike (capability-based, no transport-name check, T-95); REST caller with `overwrite` -> `unsupported`; both/neither of `data`/`path` -> `invalid_field`; inline data above `max_inline_bytes` -> `invalid_field`; `ControlSet::User` -> `not_authorized`; 10k-row import: while the store blocks at row 5000, `link.get` through the same registry still answers (no head-of-line blocking) and `job.get` shows `running` with progress 5000 |

Manual check: import a 10k-row CSV from the CLI/TUI against SQLite and confirm `job.get` progress advances.

## Constraints

- C1/C3: no second validation, SSRF, slug or conflict path - every row goes through `CreateLink` (dry-run through `ValidateCreateLink`); `link.create` results/bodies are unchanged by this subtask.
- C6: error entries hold line number and code only; no URLs, slugs or file paths are logged or returned in errors; one audit line per job (registry), none per row.
- C7: `write` permission; `--read-only` local sessions get `not_authorized`.
- T-95: `path` is refused by the file-path capability (`deps.file_paths.allow_paths == false` -> `unsupported`), not by transport name; the REST per-request runtime is default-deny (20).
- T-97 resource caps: the body enforces `kMaxImportLineBytes` per line (07 reader), `kMaxImportFileBytes` on file reads (07), and `kMaxImportRows` on row count (body step 4); dry-run honours the same caps; plain text only (no decompression -> no bomb).
- T-101: `link.import` is **throttle-exempt** - it does not call `apply_create_throttle` (04.0 T-75, per HTTP peer) - and is reachable only with `write`. Over REST the gate runs it only as a loopback `admin`, `path` is `unsupported`, and inline `data` is capped at `max_request_body_bytes` (~1k rows/request); `User` -> `not_authorized`. Over stdio/CLI the caller is the local operator.
- Threading: the body runs on a worker; all state it touches is local or thread-safe (service/store). Backend concurrency for SQL stores is UNVERIFIED and exercised by the Linux/Windows SQLite integration run in 18.
- C9, C++17, naming/format per `.clang-tidy`/`.clang-format`; `.cpp` in `sources.cmake`; C10 untouched.

## Success criteria

1. `commands__57_link_import_command` passes.
2. `grep -n "isPrivateHost\|normalizeTargetUrl\|slugExists" src/commands/pair_commands.cpp` returns nothing (single validation path).
3. After a cancelled import, `job.get` returns `cancelled` with counts equal to links actually present (asserted).
4. A dry-run import returns a synchronous `result` (no `job` id, no job record), is not cancellable (`NoopJobContext`), and leaves the store untouched (asserted zero `CreateLink` calls).
5. Existing `app__` and `http` suites unchanged: `ctest --test-dir cmake-build -L unit --output-on-failure`.

## Out of scope / risks

- `overwrite` (09), export (10), a streaming total for file sources, resumable imports.
- A very large import holds one worker for its duration; with `worker_count=2` a second import runs in parallel, a third waits in the queue (or gets `busy` when the queue is full).
- Row-at-a-time `CreateLink` on a remote SQL backend can be slow (one round trip per row); batching needs a new port method and is a follow-up.
