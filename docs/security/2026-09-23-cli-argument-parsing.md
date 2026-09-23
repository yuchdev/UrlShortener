# Threat Model - CLI argument parsing (Task 02.0) - 2026-09-23

Scope: `git diff 1df5d6e...5239e13` (commits 2613c03, 9fe4086, b5f3798,
e8bdee1, 320396e, 5239e13). New per-verb argv->DTO mapping in
`src/cli/link_command_args.cpp` and top-level `link <verb>` dispatch/help in
`src/cli_parser.cpp`. Unrelated uncommitted working-tree changes ignored.

Context: argv is **process-local, not network-facing**. CLI dispatch that
actually *runs* a parsed command against `LinkCommandService`/storage lands in
Task 03.0 - today the parsed DTOs are consumed by nothing. Severities reflect
that; "once wired" notes call out what changes when Task 03.0 lands.

## Assets & trust boundaries

- Untrusted input: `argv` (verb tokens + flag values) for `link create/get/
  update/delete/enable/disable/restore/preview/stats`.
- Boundary 1: argv -> Boost.ProgramOptions + manual `splitCommaList`/
  `parseMetadataList` -> `app::` DTOs (this task).
- Boundary 2 (Task 03.0): DTOs -> `LinkCommandService` -> `ILinkStore` (SQL/
  in-memory) and rendered `short_url`/JSON output.
- Config side effects already wired: `--base-domain` and
  `--allow-private-targets` mutate `ServerConfig` during `parseCreateArgs`.

## STRIDE-lite

| Category | Assessment |
|---|---|
| Spoofing/Auth | N/A this task (local process, no network). Follow-up: Task 03.0 mutating verbs (create/update/delete/enable/disable/restore) must be gated by `AccessGuard::requireWrite` and audited; the CLI path does not currently traverse `ControlSet`/`AccessGuard`. |
| Tampering/Injection | No injection sink reached. SQL layer is fully parameterized via `soci::use` (`sqlite_session_factory.cpp`, `sql_click_event_repository.cpp`); no `system`/`popen`/`exec`/`printf`-family sinks in `src/`. JSON output via `jsonString`->`jsonEscape`. |
| Repudiation/Audit | No audit record for CLI link mutations. Deferred to Task 03.0 (see Elevation). |
| Info disclosure | No secrets in these argv surfaces; no hardcoded credentials in diff. Parse errors echo the offending value verbatim (e.g. metadata entry) - not secret, low concern if later logged. |
| DoS | Parsing is linear O(n); `splitCommaList` is iterative (no recursion), bounded by argv size which is OS `ARG_MAX`-capped. No amplification/parser-bomb. |
| Elevation | `--allow-private-targets` bool_switch weakens SSRF guard; by-design escape hatch (see LOW-1). |

## Findings

### [LOW] CLI `--base-domain` bypasses `normalizeAndValidateBaseDomain`
- Vector / evidence: `src/cli/link_command_args.cpp:384` sets
  `config.shortener_base_domain = base_domain` after only `requireNonEmpty`.
  The HTTP path validates at `src/http/http_server.cpp:36` via
  `normalizeAndValidateBaseDomain` (scheme/host/userinfo checks,
  `src/core/utils.cpp:254`). `shortUrlFor` (`src/app/link_command_service.cpp:14`)
  only strips trailing `/` and concatenates.
- Impact: process-local and self-provided; `short_url` is a computed, JSON-
  escaped output field, not persisted. Once Task 03.0 wires dispatch, a
  malformed/misleading base domain (missing scheme, embedded userinfo, control
  chars) renders into `short_url` without the parity the server enforces.
- Mitigation: route CLI `--base-domain` through `normalizeAndValidateBaseDomain`
  for parity with the server startup path.

### [LOW] `--allow-private-targets` weakens SSRF guard (CWE-918); prefix guessing footgun
- Vector / evidence: `src/cli/link_command_args.cpp:301,386-387` sets
  `config.shortener_allow_private_targets = true`; enforcement point
  `src/core/utils.cpp:237` (`!allow_private_targets && isPrivateHost(host)`).
- Impact: intentional operator escape hatch mirroring the existing
  `SHORTENER_ALLOW_PRIVATE_TARGETS` env var and server `--allow-private-targets`
  flag; defaults off, explicit opt-in, per-invocation (one-shot CLI, no cross-
  request persistence). The operator running the binary already has host
  access, so no privilege gain. Foreseeable once wired: `link create` could mint
  links to intranet targets. Secondary: Boost.ProgramOptions default
  `unix_style` enables `allow_guessing`, so the unambiguous prefix `--allow`
  resolves to this security-relevant switch - a mild footgun.
- Mitigation: keep off by default (done); ensure help/docs warn it disables SSRF
  protection (help text at `src/cli_parser.cpp` says "Permit private/intranet
  targets" - acceptable). Consider disabling `allow_guessing` for the create
  verb so the SSRF switch cannot be reached by an abbreviated prefix.

### [INFO] Manual split helpers are safe
- `splitCommaList` (`link_command_args.cpp:74`) and `parseMetadataList`
  (`:106`): iterative, no recursion; `start = comma + 1` and
  `find('=', eq + 1)` are index-safe at end-of-string (`substr(size())` and
  `find` at `size()` are defined). Malformed metadata throws
  `std::invalid_argument` cleanly (no UB). Repeated single-value flags and
  unknown flags surface as `po::error` wrapped into `std::invalid_argument` via
  `runVerbParse`. Empty/huge/control-char values are carried as `std::string`
  and re-validated downstream by `validateTags`/`validateMetadata`. No
  buffer/overflow issue.
- Pre-existing (not introduced here): `jsonEscape` (`src/core/utils.cpp`) does
  not escape C0 control chars other than `\n\r\t`, so raw control bytes in
  tags/metadata would pass into JSON output. Out of scope for this task; flag if
  Task 03.0 renders untrusted fields to a shared sink.

## Follow-ups for Task 03.0 (non-blocking now)
- Gate mutating verbs behind `AccessGuard::requireWrite`/`requireUserManagement`
  and emit an audit record (parity with `auth_audit_log`), since CLI dispatch
  will act on production storage.
- Add `--base-domain` validation parity (LOW-1) before dispatch renders
  `short_url`.

## Verdict: PASS_WITH_FOLLOWUP
No CRITICAL or HIGH findings. Parsing surface is process-local, memory-safe, and
reaches no injection sink (parameterized SQL, no shell/format-string). Two LOW
items and Task-03.0 authorization/audit follow-ups noted above.

## Task 03.0 - CLI dispatch and process lifecycle (2026-09-23, round 2)

Scope: `git diff 5239e13...1fe3919` (5953f20, 85e3fd4, d59cf47, d0b2e3c, 1fe3919)
- the first commit range where parsed argv reaches storage-mutating
`LinkCommandService` calls. New: `src/cli/link_command_dispatch.cpp`,
`include/url_shortener/cli/link_command_dispatch.hpp`, the `main.cpp`
short-circuit branch, e2e sections 14-18.

### Trust boundary
Untrusted input remains process-local `argv` only - dispatch runs before any
`io_context`/`HttpServer` construction (`main.cpp:47` vs. `io_context` at
`:56`; confirmed by direct read, not just prior commit messages). The boundary
crossed by this task: parsed `app::` DTOs -> `LinkCommandService` (via
`app::BuildLegacyLinkCommandService`, the *same* factory
`src/http/handlers/link_handlers.cpp` uses) -> `LegacyLinkStore` ->
`linkRepository()` in-memory singleton.

### Findings
- **No new bypass vs. REST.** SSRF guard (`isPrivateHost` /
  `normalizeTargetUrl`, `src/core/utils.cpp:237`) is enforced inside
  `LinkCommandService::CreateLink` itself, so CLI `create` gets the identical
  protection as the REST handler - not duplicated, not skippable. REST enforces
  no body-size limit or rate limit in `link_handlers.cpp` either, so CLI
  matching that (argv has no equivalent transport-layer limit, bounded by
  `ARG_MAX`) is parity, not a new gap.
- **`--allow-private-targets` reachability confirmed per-invocation only.**
  Flows `parsed.config` -> `LinkCommandService`'s `const ServerConfig&` ->
  `normalizeTargetUrl`; touches no persistent or global state. Each one-shot
  CLI process re-derives it from its own argv.
- **[INFO] `src/cli/link_command_dispatch.cpp:70`** (pre-fix) / `:71-79`
  (post-fix) - stderr error line (`"<verb> failed: <detail>"`) only ever
  carries static string literals from `link_command_service.cpp` /
  `legacy_adapters.cpp` ("Link not found", "url must be an absolute
  http/https URL", etc.). No DSN, token, salt, or filesystem path is ever
  interpolated - nothing the REST error path would redact leaks here.
- **No resource-exhaustion path.** `linkRepository()`
  (`src/storage/link_repository.cpp:87-91`) is a function-local `static`,
  process-lifetime only; a one-shot CLI process starts empty and frees the
  map on exit - no cross-invocation accumulation. Slug generation is a
  bounded 10-iteration loop. A DTO/verb variant mismatch raises
  `std::bad_variant_access`, caught by `main`'s try/catch -> clean exit 1, not
  a hang.
- **[LOW, round-2 delta only]** Round-1's blocking finding was a *test* bug
  (not a product-security issue): e2e sections 14-18's `if ! timeout 5 ...;
  then rc=$?` captured the negated exit status (always 0), making the
  `rc -eq 124` hang-detection branch unreachable - a false negative on the C4
  no-server-socket guarantee. Fixed in `1fe3919`
  (`rc=0; timeout 5 ... || rc=$?`), independently verified via `bash -c`
  under `set -euo pipefail` for the hang/normal/nonzero-exit cases. No
  product-code security surface was affected by either the bug or the fix.
- **[LOW, carried, still open]** `--base-domain` still skips
  `normalizeAndValidateBaseDomain` (unchanged since Task 02.0's audit); now
  that dispatch is real rather than parsed-only, a malformed base domain does
  render into `short_url` on an actual CLI invocation. Recommend folding into
  Task 06.0's cleanup pass.
- **[INFO, carried, still open]** CLI mutating verbs still do not traverse
  `AccessGuard`/`ControlSet` - confirmed this is parity with the REST path
  (`src/http/handlers/link_handlers.cpp` doesn't call `AccessGuard` either;
  only the separate auth-broker subsystem in `src/security/` does), not a gap
  this milestone introduces. Tracked as a milestone-level follow-up per
  `plan.md`'s decision not to reconcile the two storage/authz paths.

## Verdict (Task 03.0): PASS_WITH_FOLLOWUP
No CRITICAL or HIGH in either round. The one round-1 blocking finding was a
test-only false-negative (fixed, independently reverified). Two LOW/INFO
items remain open at the milestone level (base-domain validation parity;
no `AccessGuard`/audit on mutating verbs), tracked for Task 06.0 and beyond,
not blocking this task's completion.
