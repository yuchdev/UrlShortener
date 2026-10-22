# 14 - Server and CLI on one SQLite file

**Parent task:** 02.0 Storage path unification
**State:** ⬜ Not started
**Depends on:** 02.0/12
**Blocks:** 15
**Estimated size:** M
**Suggested agent:** testing-expert

## Objective

Prove the milestone-level property of this task end to end with real
processes: a running server (redirects + REST) and one-shot CLI invocations
configured with the same `--storage-config` SQLite file see the same links, and
changes made by one are visible to the other within the documented cache bound.
This is the executable form of "the redirect path and the command layer share
one backend".

## Dependencies

- 02.0/12 (the binary honors `--storage-config` in server and CLI mode).
- Existing helpers (verified): `tests/integration/core/integration_common.py`
  (`ServerHarness(args)` starts the binary with stdout/stderr discarded, sleeps
  1.5 s, `stop()` calls `terminate()`; `request(port, method, path, body,
  headers)` returns `(status, payload, response)`), and
  `tests/integration/cli/cli_integration_common.py` (`run_cli`, `find_binary`,
  `write_sqlite_storage_yaml` from subtask 13).
- Server flags used by sibling tests: `--http-port <n> --tls-enabled false
  --shortener-base-domain http://sho.rt`.
- Cache bound: `l1_ttl = 5 s` on persistent backends (subtask 11), so a change
  made by the CLI becomes visible to the server's redirect path within 5 s.
- `GET /healthz` exists (route registry, verified) but the harness' fixed sleep
  is the existing convention; this test polls `/healthz` instead of sleeping
  longer.

## Files affected

| Path | Create / modify | What changes |
|---|---|---|
| `tests/integration/cli/18_server_and_cli_share_sqlite.py` | create | The scenario test |
| `sources.cmake` | modify | `CLI_INTEGRATION_SOURCES` += test (ctest name `cli_18_server_and_cli_share_sqlite`, timeout 60 s from `CMakeLists.txt`) |

## Classes and functions

Test-only. Port: a free port chosen with `socket.bind(("127.0.0.1", 0))`
(sibling tests hard-code 28101-ish ports; a dynamic port avoids collisions with
parallel ctest runs). Helper `wait_until(predicate, timeout, interval=0.25)`.

Scenario (one `unittest.TestCase`, `setUpClass` starts the server once with
`--storage-config <yaml> --http-port <port> --tls-enabled false`, `tearDownClass`
stops it; DB and YAML live in a `TemporaryDirectory`):

| Step | Action | Expected |
|---|---|---|
| 1 | CLI `link create --url https://example.com/a --slug share-a --storage-config` | exit 0, JSON slug `share-a` |
| 2 | `GET /share-a` on the server | 302, `Location: https://example.com/a` (server read what the CLI wrote) |
| 3 | REST `POST /api/v1/links` slug `share-b` | 201 |
| 4 | CLI `link get --slug share-b` | exit 0 (CLI read what the server wrote) |
| 5 | CLI `link disable --slug share-a` | exit 0 |
| 6 | `wait_until` `GET /share-a` returns 410 | within 8 s (bound = L1 TTL 5 s + slack); the status before that may still be 302 (documented staleness, **asserted to end**, not to be instant) |
| 7 | CLI `link delete --slug share-a`; `wait_until` `GET /share-a` returns 404 | within 8 s |
| 8 | REST `GET /api/v1/links/share-b` | 200 and body `slug` `share-b` |
| 9 | CLI `link create` duplicate `share-b` | exit 3 (`slug_conflict`) |
| 10 | Redirect `GET /share-b` 5 times, then stop the server (POSIX: `terminate()` -> SIGTERM handler -> `backend.flush()`); CLI `link get --slug share-b` | `stats.total_redirects == 5` (counter written by the server's flush on shutdown); skipped on Windows where `terminate()` kills the process without running the handler |

Step 6's tolerance is the deliberate cross-process consistency contract (README
decision 2); the test documents it instead of hiding it with fixed sleeps.

## Implementation steps

1. Reuse `ServerHarness` and `request` by adding the sibling `core` directory to
   `sys.path`; reuse `cli_integration_common` helpers (add
   `write_sqlite_storage_yaml` if subtask 13 has not landed).
2. Implement `wait_for_server` (poll `GET /healthz` up to 10 s).
3. Implement the steps; each CLI call runs with `cwd` = a temp directory and an
   absolute YAML path.
4. Register in `sources.cmake`; run.

## Commands

```powershell
cmake --build cmake-build --target url_shortener
ctest --test-dir cmake-build -R "^cli_18_server_and_cli_share_sqlite$" --output-on-failure
# direct (Windows PowerShell):
$env:SIMPLE_HTTP_BIN = (Resolve-Path ./cmake-build/url_shortener.exe)
python tests/integration/cli/18_server_and_cli_share_sqlite.py
```

(Multi-config: `-C Debug`, binary in `cmake-build/Debug/`.)

## Tests

| Test file | Target | Label | Cases |
|---|---|---|---|
| `tests/integration/cli/18_server_and_cli_share_sqlite.py` | `cli_18_server_and_cli_share_sqlite` | integration | steps 1-10 above, as separate `test_` methods ordered by name or one sequential test |

Failure cases included: conflict (9), disabled/deleted visibility after the
cache bound (6, 7), counter durability on shutdown (10). Port-in-use or server
failure to start is reported with the server's exit code (stderr is discarded by
`ServerHarness`; pass a log file for easier diagnosis in this test by starting
the process directly with `stderr` to a temp file).

Manual check: start the server by hand with `--storage-config` and, from another
terminal, run `link create` / `link get` while hitting `GET /{slug}` with
`curl.exe -i`.

## Constraints

- Test duration bounded (< 40 s); no fixed long sleeps (polling with timeouts).
- Dynamic port; temp files only; kill the server in `tearDownClass` even on
  failure (`terminate` then `kill`).
- No dependency on Redis/Postgres; SQLite only.
- Redirect fast path behavior itself is not modified by this test; it only
  observes it. Contracts: C5 (CLI calls do not bind a port - the server is the
  only listener), cache-aside invalidation semantics (README decision 2).
- If SQLite reports `database is locked` (server write-behind flush vs CLI
  write), that is a product finding (busy timeout configuration, see
  `tests/unit/storage/sqlite/11_busy_timeout_configuration.cpp`), to be reported
  rather than masked with retries beyond one.

## Success criteria

1. `cli_18_server_and_cli_share_sqlite` passes on Linux and Windows (step 10
   skipped on Windows with an explicit `skipTest` message).
2. Steps 2 and 4 pass without any sleep: bidirectional visibility of new links
   is immediate (a new slug is never cached negatively).
3. Steps 6-7 converge within 8 s, matching the documented bound.

## Out of scope / risks

- Multiple server processes against one file (counter divergence, README R-1)
  and Postgres.
- Windows graceful shutdown (needs `CTRL_BREAK_EVENT` or a stop endpoint) is
  not available, hence the skip.
- The 5 s bound is a default; operators needing instant cross-process
  visibility must run a single process (TUI/CLI in-process, or the future
  `serve-stdio`/REST client path, Task 06.0 Q10).
