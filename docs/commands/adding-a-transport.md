# Adding a Transport

> **Status:** Proposed (design of Milestone 0006; normative once
> [ADR 0002](/docs/adr/0002-unified-command-model.md) is accepted).

This guide explains how to add a new stream transport to the URL Shortener
command model. It covers the `ITransportAdapter` contract, the NDJSON framing
and shutdown rules used by the shipped `serve-stdio` mode, and a worked design
sketch of a Unix domain socket / Windows named pipe IPC adapter. The IPC adapter
is documented as a design sketch — it is not implemented in this codebase.

`serve-stdio --help` links here. The authoritative command and job model is in
[docs/commands/README.md](/docs/commands/README.md). The design rationale is in
[ADR 0002](/docs/adr/0002-unified-command-model.md), specifically
[section 4](/docs/adr/0002-unified-command-model.md#4-transport-adapters-the-only-thing-that-differs).

---

## 1. What a transport is

A transport has exactly three responsibilities and no others:

1. **Frame in** — turn the transport's native input into a `CommandRequest`
   envelope.
2. **Call** `CommandRegistry::execute`.
3. **Frame out** — turn the resulting `CommandResponse` into the transport's
   native output.

A transport **must not** contain:

- Business rules of any kind.
- Per-command logic, DTO construction, or error mapping.
- Authorization decisions beyond supplying a `CallerContext` whose
  `control_set` was fixed at construction time by the composition layer.

Those all live behind the registry (milestone contracts C1 / C2). See
[ADR 0002 section 4](/docs/adr/0002-unified-command-model.md#4-transport-adapters-the-only-thing-that-differs).

Only stream transports that own their own receive/execute/respond loop implement
`ITransportAdapter`. The REST route handler inside `HttpServer` and the
one-shot CLI function called from `main` are deliberately **not** expressed as
`ITransportAdapter` (decision log D7): REST is a route handler, and CLI is a
one-shot function; both still follow the same three responsibilities and call
only `execute`.

---

## 2. The interface

### `ITransportAdapter`

Declared in `include/url_shortener/commands/transport_adapter.hpp`.

```cpp
class ITransportAdapter
{
public:
    virtual ~ITransportAdapter() = default;

    // Stable lower-case identifier, e.g. "stdio", "ipc".
    // The referenced storage must outlive the adapter.
    virtual std::string_view name() const = 0;

    // Runs the receive/execute/respond loop until terminal, then
    // returns a process exit code (0, 2, or 4; see table below).
    // Must be called exactly once per instance.
    // Must not construct HttpServer, bind a socket, or touch uri.txt
    // unless the adapter is explicitly a network transport (C5).
    virtual int run(CommandRegistry& registry) = 0;

    // Optional: response envelopes successfully written, for the
    // shutdown log line.  Default: 0.
    virtual std::size_t responses_written() const noexcept { return 0; }
};
```

`run` must be called exactly once per instance; a second call is a programming
error. The `CommandRegistry` passed to `run` must outlive the call.

### `CallerContext`

Declared in `include/url_shortener/commands/command_descriptor.hpp`.

```cpp
struct CallerContext {
    std::string principal;    // "local-operator", session user, etc.
                              // Diagnostic only; not an authorization input.
    ControlSet  control_set;  // The ONLY authorization input.
                              // Set by composition, never read from input.
    std::string transport;    // "rest", "cli", "tui", "stdio", "ipc", etc.
                              // Diagnostic only; not an authorization input.
};
```

`control_set` is the **sole** authorization root (threat model T-107, milestone
contract C7). The registry performs no transport-based restriction of its own;
the composition layer is fully trusted to set `control_set` correctly. No byte
of the input stream may ever influence it.

### Registration in the factory

New adapters are instantiated by `composition::make_transport_adapter`
(`src/composition/transport_factory.cpp`), the single adapter-selection point
(mirrors `BuildStorageAdapters`). To add a new adapter:

1. Implement `ITransportAdapter` in `src/transport/<name>_adapter.cpp` (and
   `include/url_shortener/transport/<name>_adapter.hpp`).
2. Add a case-sensitive `name == "<name>"` branch to `make_transport_adapter`.
3. Add CLI parsing following the pattern of `parse_serve_stdio` in
   `src/cli_parser.cpp`.
4. Add a new dispatch function following `RunServeStdio` in
   `src/cli/serve_stdio_dispatch.cpp`.

No command file, handler, descriptor, DTO, or serializer changes are needed.

### `TransportOptions`

The factory takes a `composition::TransportOptions` struct
(`include/url_shortener/composition/transport_factory.hpp`):

```cpp
struct TransportOptions {
    bool                       read_only    = false;   // User vs Admin role
    std::size_t                max_line_bytes = 65536; // mirrors max_request_body_bytes
    std::chrono::milliseconds  drain_timeout{30000};   // EOF job-drain window
    commands::JobRunner*       job_runner   = nullptr; // non-owning; nullptr = no drain
};
```

`TransportOptions` grants **no** filesystem (`path`) capability by default.
The local-operator file-path capability is a separate, default-deny decision
of the composition root (threat model T-95); see section 6a.

### Exit codes

| Code | Meaning |
|------|---------|
| `0` | Clean shutdown: input ended and all in-flight jobs drained. |
| `2` | Usage or validation error at startup (e.g. invalid `--path-root`). |
| `4` | Internal error or stream failure (broken pipe, escaping exception). |

Codes are drawn from the shared error catalog
(`commands::error_info_for(CommandErrorCode::...)`).

---

## 3. Framing rules (NDJSON)

The session's normative behavior, reproduced from
`include/url_shortener/commands/ndjson_session.hpp`
(the comment in that file states it is identical to this section):

1. One content line in, exactly one response line out, strictly in order. The
   next line is not read until the previous response has been flushed. Long work
   never blocks the loop because a `kind=job` command returns the `job` envelope
   immediately.

2. An output line is compact single-line JSON followed by a single `\n`
   (never `\r\n`), flushed. `serialize_response` escapes control characters so a
   response can never carry a raw newline; `write_response` asserts this in debug
   and substitutes an `internal` error response if it is ever violated.

3. Verdict mapping over `read_result`: `line` is parsed and executed; a parse
   failure becomes an `invalid_envelope` response whose message names the problem
   class and never echoes the line; `too_long` becomes `invalid_envelope`
   "line too long"; `invalid` (a NUL byte) becomes `invalid_envelope`
   "invalid byte in line"; `io_error` ends the loop with
   `session_end::input_failed`; `eof` ends it with `session_end::eof`.

4. The response `id` is echoed from the request. When the line is a valid
   object, the id is recovered best-effort with the sanctioned scanner
   (`scan_object` + `find_member("id")`), string kind only and accepted only when
   1..64 bytes and free of control characters; otherwise a session-local `srv-<n>`
   is assigned. The same `srv-<n>` fallback names a `too_long` or `invalid` line
   (whose bytes were dropped and cannot be scanned). The counter is per session,
   so a scripted session's ids are deterministic and cannot be forced to collide
   by a client sending `"id":"srv-5"` (threat model T-104).

5. Any `std::exception` escaping `CommandRegistry::execute` becomes an `internal`
   error response with a fixed message and the loop continues;
   `AccessDeniedError` is already mapped to `not_authorized` inside `execute`, so
   a `User` caller invoking a write command never throws into the session.

6. A malformed line never ends the session; only EOF or a stream failure does.

7. Nothing but response envelopes is ever written to the output stream (contract
   C2, threat model T-102): no banners, prompts or logs. The one `debug`-level
   session start/end line carries only counts and a reason (no request content,
   contract C6) and goes to the logger, never to `out`.

**Additional input constraints** (enforced by `NdjsonLineReader`,
`include/url_shortener/commands/ndjson_line_reader.hpp`):

| Property | Value |
|---|---|
| Encoding | UTF-8; non-ASCII bytes are passed through unchanged. |
| Line terminator | `\n`; a preceding `\r` is stripped. A final line without a terminator is accepted. |
| Max line length | `max_request_body_bytes` (default 65536 bytes); longer lines are `too_long`. |
| Blank / whitespace-only lines | Silently skipped; no response is produced. |
| UTF-8 BOM | Stripped from the first physical line only; kept on later lines. |
| NUL byte | Refused as `invalid`; physical line consumed and dropped before parsing. |
| Pretty-printed JSON | Not supported; a multi-line JSON object produces `invalid_envelope`. |

---

## 4. Error behavior

Malformed input produces an error response; the session continues:

| Input verdict | id assigned | Response `error.code` |
|---|---|---|
| Valid JSON object with valid `id` (string, 1..64 chars, no control chars) | Client `id` echoed. | Per command. |
| Valid JSON, `id` missing, not a string, or > 64 chars | `srv-<n>` assigned. | Per command. |
| Malformed / non-JSON | `srv-<n>` (bytes dropped; not parseable). | `invalid_envelope` |
| Line over `max_request_body_bytes` | `srv-<n>` (bytes dropped). | `invalid_envelope` |
| Line containing a NUL byte | `srv-<n>` (bytes dropped). | `invalid_envelope` |

Only EOF or a hard stream error ends a session. All other inputs — including
malformed JSON, over-long lines, and authorization failures — produce a response
and the loop continues.

### Example transcript (5 content lines in, 5 responses out)

Blank lines and `#` comments are for readability; the actual wire carries no
blank lines between requests.

```text
# --> request (stdin)
# <-- response (stdout)

--> {"v":1,"id":"c-1","command":"command.list"}
<-- {"v":1,"id":"c-1","ok":true,"result":{"commands":[...]}}

--> {not valid json!
<-- {"v":1,"id":"srv-1","ok":false,"error":{"code":"invalid_envelope","message":"malformed JSON"}}

--> {"v":1,"id":"c-3","command":"link.get","args":{"slug":"no-such-slug"}}
<-- {"v":1,"id":"c-3","ok":false,"error":{"code":"not_found","message":"link not found"}}

--> {"v":1,"id":"c-4","command":"unknown.thing"}
<-- {"v":1,"id":"c-4","ok":false,"error":{"code":"unknown_command","message":"no such command"}}

--> {"v":1,"id":"c-5","command":"link.create","args":{"target_url":"https://example.com/docs","slug":"ex"}}
<-- {"v":1,"id":"c-5","ok":true,"result":{"slug":"ex","url":"https://example.com/docs",...}}
```

Note: the second input line was not parseable JSON, so `id` `srv-1` was
assigned by the server-local counter. The malformed line did not end the
session; the following three commands were processed normally.

<!-- SME REVIEW NEEDED: replace with a transcript produced by the real binary
     (run tests/integration/cli/ suite from the 05.0/18 StdioSession helper). -->

---

## 5. Shutdown semantics

The `serve-stdio` shutdown contract (reproduced from
`include/url_shortener/transport/stdio_adapter.hpp`):

| Event | Behavior |
|---|---|
| stdin EOF | stop reading; drain jobs (below); exit `0`. |
| client reads stdout only after closing stdin | still works; earlier responses were already flushed; jobs finishing during the drain produce no output. |
| stdout closed / broken pipe | `output_failed`; cancel jobs; exit `4` (requires `SIGPIPE` ignored, wired at startup before the job runtime is built; without it a closed pipe raises the signal and the default disposition terminates the process). |
| SIGINT / SIGTERM | no handler installed on purpose; the default disposition terminates the process immediately. In-memory jobs are lost (documented non-goal, T-106); rows already created stay persisted. A parent must close stdin for a graceful stop. |
| unrecoverable registry fault | not possible: `std::exception`s are mapped to a response by the session, and any that still escapes is caught by `run` and mapped to exit `4`. |

No signal handler is installed and no reader thread is spawned: a portable
interruptible blocking read on stdin does not exist (POSIX `SA_RESTART`
semantics, non-interruptible Windows console reads), so EOF-as-shutdown is the
documented contract rather than signal handling.

**Job drain table** (from `StdioAdapter::run`,
`include/url_shortener/transport/stdio_adapter.hpp`):

| Session end reason | Drain behavior | Exit |
|---|---|---|
| `eof` | `wait_idle(drain_timeout)`; on timeout: `cancel_all()` + 5 s grace + one `warn stdio_drain_timeout` (counts only); then `shutdown()` unless a non-cooperative body is still running, in which case the blocking join is skipped (bounded, carry-forward 3). | `0` |
| `input_failed` | `cancel_all()` immediately + 5 s grace + `shutdown()` (no long wait). | `4` |
| `output_failed` | Same as `input_failed`. | `4` |

The default drain timeout is 30 seconds (`k_drain_timeout` in
`src/cli/serve_stdio_dispatch.cpp`).

**Non-goal: hard signals and non-cooperative job bodies.** A job body that
never polls `IJobContext::cancel_requested()` can hold the graceful drain for
the full timeout. After the grace window, the blocking join is skipped (a value-
free `warn stdio_drain_abandoned` is emitted) rather than hanging the process.
Any such body is joined by process teardown once it unblocks. A body that never
unblocks is an implementation defect in that command, not a transport
responsibility.

---

## 6. Jobs over a transport

A `kind = job` command (or any `async_capable` command submitted with
`options.async`) returns a `job` envelope immediately:

```json
{ "v": 1, "id": "c-7", "ok": true, "job": { "id": "j-9", "state": "queued" } }
```

The caller polls with `job.get {"id":"j-9"}` or cancels with
`job.cancel {"id":"j-9"}`.

**Key properties:**

- Jobs are **process-wide and in-memory**. They are held by the one process-wide
  `JobRunner` (default: 2 workers, queue depth 16, 256 retained records; see
  `composition::JobRuntimeSettings`).
- A job **outlives** the request that submitted it. A `CommandRuntime` may be
  destroyed while its submitted job still runs. Job bodies must own every value
  they need by deep-copy at factory time and must never capture a reference to
  request-owned memory (threat model T-87).
- The `JobRunner` is bounded: a full queue yields `busy` and creates no record.
- A transport obtains both its registry and the process-wide `JobRunner` from
  `composition::build_command_runtime`; the non-owning `job_runner` pointer in
  `TransportOptions` lets the adapter drain at EOF. After `run` returns, call
  `composition::shutdown_process_job_runtime` to drain, cancel, and join workers
  so no thread outlives the process (contract C11).
- A transport **never** builds its own `CommandRegistry` or its own `JobRunner`.

**Startup order** (implemented in `src/cli/serve_stdio_dispatch.cpp`):

1. Validate startup options (e.g. `--path-root` must name an existing directory;
   returns exit `2` on failure without echoing the path value, contract C6).
2. Call `composition::build_command_runtime(config, backend, runtime_options)` —
   the one composition root (contract C11).
3. Build `TransportOptions` (role, line cap, drain timeout, non-owning
   `job_runner` pointer from `runtime.job_runner.get()`).
4. Call `composition::make_transport_adapter("stdio", transport_options, in,
   out)` and then `adapter->run(*runtime.registry)`.
5. Call `composition::shutdown_process_job_runtime(k_shutdown_timeout)` —
   joins any remaining workers (idempotent; the adapter already drained and, for
   a cooperative body, joined them).

### 6a. File paths (`link.import` / `link.export` `path` argument)

The `path` argument is a **default-deny** capability (threat model T-95). A
transport that does not explicitly grant it will receive `unsupported` from the
pair commands before they touch the filesystem, independent of transport name.
The capability is set by `CommandRuntimeOptions::file_paths` in
`build_command_runtime`, never by the transport's input.

| Scenario | `PairFileOptions::allow_paths` | Effect |
|---|---|---|
| REST (remote peer) | `false` (forced in the REST runtime) | `path` → `unsupported` |
| `serve-stdio --no-file-paths` | `false` | `path` → `unsupported` |
| `serve-stdio --path-root <DIR>` | `true`, root = `<DIR>` | `path` confined to `<DIR>` |
| `serve-stdio` (default, local operator) | `true`, no root | Full filesystem access |
| `cmd` / `tui` | `true`, no root | Full filesystem access |
| Any new remote transport | `false` (default) | `path` → `unsupported` unless explicitly opted in |

**`--path-root` confinement and CWE-59 (hard links).**
`--path-root <DIR>` performs component-wise canonical path comparison to confine
`path` to the specified directory subtree. This confinement does **not** stop
reading an in-root hard link to a regular file that lives outside the root on
the same filesystem (CWE-59). Linux `fs.protected_hardlinks`
(`/proc/sys/fs/protected_hardlinks`) mitigates this for unprivileged users on
Linux; the sysctl is not portable to macOS, Windows, or other POSIX systems.
Writes are safe: `link.export` creates the target exclusively (`O_CREAT |
O_EXCL`) and renames atomically via a `.tmp-<pid>-<n>` file, never following an
existing link at the target path. For read operations (`link.import`),
`--no-file-paths` is the hard control that eliminates the attack surface
entirely; `--path-root` is a convenience confinement suitable for a trusted
local operator who wants to restrict where the binary can read files.
See threat model T-96.

---

## 7. Worked example: IPC adapter (design sketch, not shipped)

> **This section documents a design sketch.** No IPC adapter exists in this
> codebase. The purpose is to show that adding one is a local change — one class,
> one factory branch, one CLI parser extension — that requires no modification
> to any command. See milestone 0006 decision log Q12 for why IPC was not
> implemented in this milestone.

### Goal and non-goals

**Goal:** a local IPC transport using a Unix domain socket (`AF_UNIX`) or a
Windows named pipe, sharing the same `NdjsonSession` framing as `serve-stdio`,
so each connection is handled by one session instance.

**Non-goals:**

- TCP or any `0.0.0.0` binding. The auth broker must stay local-only (CLAUDE.md).
- Remote peers. Peer credentials must be verified before granting any role.
- Any change to any command handler, descriptor, DTO, or serializer.

### Endpoint security

| Platform | Socket / pipe |
|---|---|
| POSIX | `AF_UNIX` socket in a `0700` directory; file mode `0600`; unlinked on start and stop. |
| Windows | Named pipe `\\.\pipe\url-shortener`; ACL for the current user only; `PIPE_REJECT_REMOTE_CLIENTS` flag set. |

### Identity and `CallerContext`

Peer credentials are verified before the first request is dispatched:

| Platform | Mechanism |
|---|---|
| Linux | `SO_PEERCRED` on the accepted socket. |
| macOS / BSD | `LOCAL_PEERCRED` / `getpeereid`. |
| Windows | `GetNamedPipeClientProcessId` + token comparison. |

A peer whose effective UID matches the server's UID is treated as the **local
operator** (`ControlSet::Admin`, or `ControlSet::User` under `--read-only`). Any
other peer is **refused at connection time** — not downgraded — so there is no
path from a peer credential mismatch to elevated rights (threat model T-107).

The `path` argument follows the same rules as `serve-stdio` (section 6a):
the composition layer decides whether to grant the capability by setting
`PairFileOptions::allow_paths`; the IPC adapter uses the same wiring.
A transport that cannot establish that the peer is the local operator must leave
`allow_paths = false`.

### Class sketch (~40 annotated lines)

```cpp
// src/transport/ipc_adapter.cpp  --  DESIGN SKETCH, NOT COMPILED
// Demonstrates that NdjsonSession is reused over a socket streambuf without
// modification; the adapter provides the socket layer, nothing else.

class IpcAdapter final : public commands::ITransportAdapter
{
public:
    // 'caller' is built by composition at construction time (T-107):
    // Admin (or User under --read-only), transport = "ipc".
    IpcAdapter(int listening_sock,
               commands::CallerContext caller,
               std::size_t max_line)
        : m_sock(listening_sock), m_caller(std::move(caller)),
          m_max_line(max_line) {}

    std::string_view name() const override { return "ipc"; }

    int run(commands::CommandRegistry& registry) override
    {
        // Accept loop; bounded: reject beyond kMaxConnections.
        while (!m_stop && connections_active() < kMaxConnections) {
            const int fd = accept(m_sock, nullptr, nullptr);
            if (fd < 0) break;

            // Refuse non-local-operator peers before any command runs.
            if (!local_operator_peer(fd)) { close(fd); continue; }

            // One thread per connection; each connection runs one NdjsonSession.
            m_pool.submit([&registry, fd, this] {
                SocketStreambuf buf(fd);  // std::streambuf wrapping the socket fd
                std::istream in(&buf);
                std::ostream out(&buf);
                // NdjsonSession is reused unchanged; it knows nothing of sockets.
                commands::NdjsonSession session(in, out, m_max_line);
                // CallerContext fixed by construction -- never read from input (T-107).
                session.serve(registry, m_caller);
                close(fd);
            });
        }
        // Stop accepting, drain jobs, join threads, unlink socket.
        return drain_and_exit();
    }

private:
    static constexpr int kMaxConnections = 8;
    int                           m_sock;
    commands::CallerContext       m_caller;
    std::size_t                   m_max_line;
    std::atomic<bool>             m_stop{false};
    BoundedThreadPool             m_pool{kMaxConnections};
};
```

The key observation: `NdjsonSession` is reused over a `std::streambuf` wrapper
for the socket fd. The session is oblivious to the transport medium; the adapter
provides only the peer-credential check and the accept loop.

### Concurrency

Sessions are independent; in-order guarantee is per-connection only. The
process-wide `JobRunner` is shared across all connections and threads;
`CommandRegistry::execute` is `const` and thread-safe by contract. Every command
handler must respect the same invariant.

### Lifecycle

1. Unlink any stale socket file left by a previous crash.
2. Bind and `chmod 0600` the socket in a `chmod 0700` directory.
3. Accept up to `kMaxConnections` connections, one thread each.
4. On shutdown: set `m_stop`, close the listening socket (unblocks `accept`),
   send EOF to each active connection (which ends its `NdjsonSession`), drain and
   cancel in-flight jobs, join worker threads, unlink the socket file.

### CLI wiring

```text
url_shortener serve-ipc --socket /run/url-shortener/ipc.sock
# Windows:
url_shortener serve-ipc --pipe \\.\pipe\url-shortener
```

Parsed like `serve-stdio` in `src/cli_parser.cpp`; dispatched through a new
`"ipc"` branch in `composition::make_transport_adapter`
(`src/composition/transport_factory.cpp`). No changes to any command file.

### Risks

| Risk | Mitigation note |
|---|---|
| Stale socket file after a crash | Unconditionally unlink before `bind`. |
| Pipe name squatting on Windows | `CreateNamedPipe` with `FILE_FLAG_FIRST_PIPE_INSTANCE`. |
| Peer-credential portability | Test on each target OS; `LOCAL_PEERCRED` semantics vary across BSDs. |
| Slow reader stalls the connection | Bounded: a blocked `flush` stalls only that connection's thread; others run freely. `kMaxConnections` caps the total thread count. |

---

## 8. Test checklist for a new transport

1. **Conformance corpus shim.** Add a shim following the pattern of Task
   07.0/07, using the support code from 07.0/03 and 07.0/05. The shim replays
   every `(request envelope, expected response envelope)` pair in
   `tests/conformance/commands/` through the new adapter. Identical
   `result` / `error.code` across all adapters is the executable definition of
   transport equivalence.

2. **Process-level session helper.** Use
   `tests/integration/cli/stdio_session.py` as the template for a Python session
   helper for the new transport. `StdioSession` demonstrates the full pattern:
   binary pipes, background reader thread for a non-blocking `recv`, stderr
   accumulation, context manager lifecycle, and a self-check `__main__`.

3. **Framing edge cases** (from the section 3 / 4 tables):
   - Blank / whitespace-only lines produce no response (`expect_no_response`).
   - A line over `max_request_body_bytes` produces `invalid_envelope`; the
     session continues and the next valid request succeeds.
   - A NUL byte in a line produces `invalid_envelope`; the session continues.
   - A malformed JSON line produces `invalid_envelope` with `id` = `srv-<n>`.
   - Five valid requests return five responses in the same order.
   - A request with no `id` field receives a `srv-<n>` correlation id.

4. **No-listener assertions** (contract C5):
   - The transport process must not bind any HTTP port.
   - The output stream carries only response envelopes: a grep of stdout for
     non-JSON content must return nothing.
   - Stderr carries logs; request content must never appear there (C6).

5. **Security checks:**
   - File / pipe permissions: accessible only to the owner.
   - Identity: a connection from a different UID is refused before any command
     runs.
   - `--no-file-paths`: `link.import {"path":...}` returns `unsupported`.
   - `--path-root`: a `path` outside the root returns `invalid_field`.
   - `--read-only`: `link.create` returns `not_authorized`.

---

## 9. Security checklist

A new transport must satisfy all of the following before it handles requests:

| Rule | Threat / Contract | Detail |
|---|---|---|
| **Authorization lives in the registry** | C7 | Never reimplement per-command access checks in the transport. Supply only `CallerContext`; `AccessGuard` inside `execute` enforces it. |
| **Caller identity fixed at construction** | T-107 | `control_set` is set by composition from flags or peer credentials; no byte of the input stream can escalate it. |
| **File-path capability default-deny** | T-95 | Leave `PairFileOptions::allow_paths = false` unless the transport can establish that the peer is the local operator. |
| **Output stream carries envelopes only** | T-102, C2 | Never write logs, banners, or prompts to the response stream. Force the logger onto stderr or an equivalent non-protocol sink **before** the first request is read (and before the job runtime and its workers are built). |
| **Bounded line / frame sizes** | T-103 | Enforce `max_line_bytes` before JSON parsing; drop oversized frames without storing. |
| **One-in / one-out ordering** | T-104 | Strictly sequential: read one request, flush one response, then read the next. Never pipeline; the in-order guarantee depends on single-threaded dispatch. |
| **Backpressure / flush** | T-105 | Flush after every response. A parent that never reads the output stalls the connection (bounded: single-threaded, no further reads queued). |
| **Shutdown / drain / exit codes** | T-106 | On EOF: `wait_idle(drain_timeout)` → `cancel_all()` → grace → exit `0`. On stream failure: cancel immediately → exit `4`. Never allow an exception to reach `std::terminate`. |
| **Never log request content** | C6 | Audit lines carry only command name, `id`, and outcome — no argument values. |
| **Local-only exposure** | CLAUDE.md | The auth broker must never be exposed to the public internet. Bind only to a Unix socket, a Windows named pipe, or loopback. Never `0.0.0.0`. |

**Default posture for `serve-stdio`.**
`serve-stdio` grants `ControlSet::Admin` and full filesystem access by default,
because the assumed peer is the local operator (the parent process that launched
it). For an untrusted or sidecar peer (for example, a webview that may expose
stdin to web content), always run with both `--read-only` and `--no-file-paths`:

```bash
url_shortener serve-stdio --read-only --no-file-paths
```

For a semi-trusted peer that may name files but only within a known directory:

```bash
url_shortener serve-stdio --read-only --path-root /var/data/imports
```

Remember that `--path-root` does not stop a hostile peer from exploiting an
in-root hard link to a regular file outside the root (CWE-59; see section 6a).
`--no-file-paths` is the hard control.

---

## 10. Alternatives not chosen

| Alternative | Reason not chosen |
|---|---|
| **gRPC** | Introduces a code-generator toolchain dependency and binary framing; incompatible with the no-JSON-library rule (C9) and the small-footprint local-operator use case. |
| **JSON-RPC 2.0 framing** | Very similar to the envelope format but adds batch requests and notification semantics that are not needed here; the minor schema deviation would force clients to support two dialects. See ADR 0002 "Alternatives considered". |
| **Length-prefixed binary framing** | Harder to debug (not `cat`-able or `echo`-able), no meaningful advantage for local IPC at the expected throughput, and requires a separate length-reader. |
| **HTTP over Unix socket** | Reintroduces the HTTP parser and `HttpServer` dependency; contradicts contract C5 for non-network modes. |
