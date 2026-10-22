# 19 - docs/commands/adding-a-transport.md (IPC worked example)

**Parent task:** 05.0 Jobs, pair import/export and stdio transport
**State:** ⬜ Not started
**Depends on:** 05.0/14, 05.0/15 (framing and shutdown semantics are final), 05.0/12 (interface), 05.0/18 (`StdioSession` for the transcript)
**Blocks:** 08.0/04 (verifies the guide; 08.0/01 links it from ADR 0002 when the ADR is accepted)
**Estimated size:** S
**Suggested agent:** docs-writer (accuracy review: app-architect, security-auditor for the IPC security section)

## Objective

Write the how-to that backs the claim "adding a transport is one class": a guide that explains the adapter contract, the NDJSON framing and shutdown rules shipped with `serve-stdio`, and a worked, clearly-labelled design sketch of a Unix-domain-socket / Windows-named-pipe IPC adapter. The IPC adapter is documented, not implemented (decision Q12).

## Dependencies

- The shipped code and specs of 12-15 are the source of truth; the guide quotes them, it does not invent behavior. Where code and this outline disagree after implementation, fix the guide.
- `docs/commands/README.md` section 6 already has a two-line "New transport" paragraph; this guide expands it.
- Style: same conventions as `docs/commands/README.md` (absolute-from-repo-root links, status banner "Proposed" until ADR 0002 is accepted).

## Files affected

| Path | Action | What changes |
|---|---|---|
| `docs/commands/adding-a-transport.md` | create | the guide (outline below) |
| `docs/commands/README.md` | modify | section 6: link to the guide (one sentence); do this in the same commit so no dangling link exists |
| `docs/adr/0002-unified-command-model.md` | modify | optional: add the guide to its "Links" list (docs-updater; 08.0/04 verifies, 08.0/01 if not done here) |

## Required outline of `docs/commands/adding-a-transport.md`

1. **What a transport is** - frame in / call `CommandRegistry::execute` / frame out; what it must NOT contain (business rules, per-command code, DTOs, error-code tables); link to ADR 0002 section 4.
2. **The interface** - `ITransportAdapter` exact signature; `CallerContext` fields; where it is registered (`src/composition/transport_factory.cpp`); exit-code convention (0, 2, 4).
3. **Framing rules (NDJSON)** - the table from subtask 13 verbatim (encoding, `\n`, max line = `max_request_body_bytes`, blank lines skipped, BOM, NUL, no pretty-printed JSON), one response per content line, strict ordering, flush per response, nothing but envelopes on the output stream.
4. **Error behavior** - malformed line -> `invalid_envelope` response and continue; id recovery and `srv-<n>`; exceptions mapped to `internal`; only EOF/stream failure ends a session. Example transcript (5 lines in, 5 out).
5. **Shutdown semantics** - the table from subtask 15 (EOF as graceful stop, job drain with timeout then cooperative cancel, broken output -> exit 4, no signal handlers and why).
6. **Jobs over a transport** - `job` envelope returned immediately, polling with `job.get`, cancel, jobs are process-wide and in-memory; a transport obtains its registry and the one process-wide `JobRunner` from `composition::build_command_runtime` (05.0/20) and calls `shutdown_process_job_runtime` on exit; it never builds its own registry (plan C11).
6a. **File paths** - `link.import`/`link.export` `path` policy: rejected for remote transports, `--path-root` / `--no-file-paths` for local ones whose peer is weaker than the operator (decision Q6).
7. **Worked example: IPC adapter (design sketch, not shipped)**
   - Goal and non-goals (local only; never TCP, never `0.0.0.0`; no remote peers).
   - Class sketch: `IpcAdapter : ITransportAdapter` (`name()=="ipc"`, `run` = accept loop) reusing `NdjsonSession` per connection over a socket/pipe stream (a `std::streambuf` wrapper), ~40 lines of annotated C++.
   - Endpoint: POSIX `AF_UNIX` socket in a `0700` directory, file mode `0600`, unlinked on start/stop; Windows named pipe with an ACL for the current user and `PIPE_REJECT_REMOTE_CLIENTS`.
   - Identity: peer credentials (`SO_PEERCRED`/`LOCAL_PEERCRED`, `GetNamedPipeClientProcessId` + token comparison) mapped to `CallerContext`; same user as the server -> operator (Admin or per `--read-only`); anyone else -> connection refused, not downgraded. `path` arguments remain allowed only because the peer is the same local user; a transport that cannot establish that must mark itself remote so `path` is rejected.
   - Concurrency: bounded connections (for example 8), one thread per connection, sessions are independent, ordering is per connection only; shared `JobRunner`; `CommandRegistry::execute` is const and thread-safe by contract (verify in 01.0 tests).
   - Lifecycle: stop accepting, close connections (sessions end with EOF semantics), drain jobs, join threads, remove the socket file.
   - Wiring: CLI `serve-ipc --socket <path>` parsed like `serve-stdio` (16), factory branch `"ipc"`, no changes to any command.
   - Risks: stale socket files, pipe name squatting on Windows, peer-credential portability, back-pressure from slow readers.
8. **Test checklist for a new transport** - add a shim to the conformance corpus runner (pattern of 07.0/07, support code 07.0/03 and 07.0/05), re-use `tests/integration/cli/stdio_session.py` as the template, framing edge cases from subtask 13/14 tables, no-listener/no-stdout-pollution style assertions, security checks (permissions, identity).
9. **Security checklist** - authorization only in the registry (C7), never log request content (C6), size limits before parsing, bounded memory per connection, local-only exposure, auth broker rule from CLAUDE.md.
10. **Alternatives not chosen** - gRPC, JSON-RPC 2.0 framing (see ADR 0002 alternatives), length-prefixed binary framing, HTTP over Unix socket.

## Implementation steps

1. Draft from the outline once 14/15 are implemented; copy tables from the code comments rather than retyping.
2. Verify every claim against the code: run the 18 tests, then paste a real transcript into section 4.
3. Add the cross-link in `docs/commands/README.md` section 6.
4. Run `/link-check docs/commands` and `/doc-xref docs/commands/adding-a-transport.md`.
5. Ask security-auditor to review section 7 identity and permission claims before merge.

## Commands

```text
/link-check docs/commands
/doc-xref docs/commands/adding-a-transport.md
```
Equivalent manual check: grep that every `](/docs/...)` link target exists, for example with PowerShell `Select-String -Path docs\commands\*.md -Pattern '\]\(/docs/[^)#]+'`.

## Tests

| Check | Tool | Label | Cases covered |
|---|---|---|---|
| Link/anchor validity | `/link-check` | docs | all links in the new guide and the edited README section resolve |
| Transcript accuracy | manual using 18's `StdioSession` | docs | the section 4 transcript was produced by the real binary |
| Example compiles (optional) | scratch `.cpp` outside the repo or a `docs` unit test | unit (if added) | the IPC class sketch compiles against `transport_adapter.hpp` + `ndjson_session.hpp` with a fake stream; not committed unless a docs test list exists for it |

## Constraints

- Docs only: no `src/`, `include/`, `tests/` or CMake changes in this subtask.
- Mark the IPC section explicitly "design sketch, not implemented" so it cannot be mistaken for shipped behavior.
- Do not put secrets, real paths or machine names in examples; use `/run/url-shortener/ipc.sock` and `\\.\pipe\url-shortener` as placeholders.
- Absolute-from-repo-root links, no emojis other than status markers.

## Success criteria

1. `docs/commands/adding-a-transport.md` contains all ten outline sections.
2. `/link-check` reports no dangling links for `docs/commands`.
3. Framing and shutdown tables are textually identical to those in 13 and 15 (checked with `/doc-xref`).
4. security-auditor review recorded in the PR.

## Out of scope / risks

- Implementing IPC or any other adapter; a Tauri sidecar guide (that uses `serve-stdio` directly and belongs to the future Tauri milestone).
- Risk: guide drifts from code; mitigated by the conformance and 18 tests plus the 08.0 docs sync.
