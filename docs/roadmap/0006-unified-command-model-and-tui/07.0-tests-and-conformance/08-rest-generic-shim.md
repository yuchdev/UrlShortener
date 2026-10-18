# 08 - REST generic shim

**Parent task:** 07.0 Tests and conformance
**State:** ⬜ Not started
**Depends on:** 05; Task 04.0 (`POST /api/v1/commands`)
**Blocks:** 09, 15, 33
**Estimated size:** M
**Suggested agent:** testing-expert

## Objective

Run the corpus over `POST /api/v1/commands` against a real server process,
following the existing Python integration approach (spawn the binary, talk
`http.client`), including the HTTP status mapping from the catalog error table
and a defined way to act as `admin` and as `user`.

## Dependencies

- Subtask 05 (`free_port`, `wait_http_ready`, `Adapter`); existing `request()` helper pattern from `tests/integration/core/integration_common.py`.
- 04.0 decisions this shim uses (Task 04.0 subtasks 08-09, catalog section 2 "REST access control"): there is no REST
  authentication before Milestone 0002; the generic endpoint is enabled with `--command-api-enabled true` and its caller role is
  fixed per server by `--command-api-role user|admin` (`admin` honored only for loopback peers; the shim always connects to
  `127.0.0.1`). Backend: `--storage-config <tmp sqlite yaml>` (Task 02.0 subtask 10).

## Files affected

| Path | create/modify | What changes |
|---|---|---|
| `tests/conformance/py/13_rest_generic_corpus.py` | create | `RestGenericAdapter` + corpus run |
| `tests/conformance/py/conformance_rest.py` | create | `ServerProcess` (start/stop/readiness/retry on port clash) and `RestClient` shared with subtask 09 |
| `sources.cmake` | modify | Append runner |

## Classes and functions

```python
class ServerProcess:
    def __init__(self, bin: str, storage_args: list[str], port: int, extra: list[str]) -> None
    def start(self) -> None      # Popen, wait_http_ready; up to 3 attempts with new port on early exit
    def stop(self) -> None       # terminate, wait 5 s, kill
class RestClient:
    def __init__(self, ports: dict[str, int]) -> None   # role -> port of the server started with that --command-api-role
    def post_command(self, role: str, body: str) -> tuple[int, dict | None, str]   # status, parsed json, raw text
class RestGenericAdapter(Adapter):
    transport = "rest_generic"
    def begin_case(self, case, ctx): ...      # one server per case (fresh store); returns skip if backend=inmemory is fine (single process) -> allowed
    def send(self, step, request_text, ctx) -> dict:
        status, env, raw = self._client.post_command(role(step), request_text)
        expected = step.get("http_status") or (200 if env["ok"] else HTTP_STATUS[env["error"]["code"]])
        assert status == expected, (status, raw)
        assert content_type == "application/json"
        return env
```

**Role strategy (resolved by Task 04.0 subtask 08):** per case, the shim starts the server with
`--command-api-enabled true --command-api-role admin` for `caller: admin` steps and a second server on the same SQLite file with
`--command-api-role user` for `caller: user` steps (both bound on all IPv4 interfaces as the server always is; requests go to
`127.0.0.1`). No test-only auth fixture and no new production flag. Mixed-role cases need `--storage-config` (one shared SQLite
file); because a second process may serve a cached copy for up to the 5 s L1 TTL of Task 02.0, user-role steps in the corpus are
limited to reads of data created before the user server started and to authorization denials (which never depend on cached state).

## Implementation steps

1. Implement `ServerProcess`/`RestClient` (raw `http.client`, 10 s timeouts, no redirects).
2. Add status checks: success 200; accepted job 200 (catalog: `ok:true` + `job`; decided in 04.0/09: `http_status_for` returns 200 for accepted jobs).
3. `raw_request` cases: send verbatim body; wrong `Content-Type` and empty body get separate cases in subtask 14 (not here).
4. Run each case against a fresh server (about 0.3 s startup) or, for speed, one server per suite with `link`-table truncation through the test backdoor **only if** such a backdoor already exists; default is one server per case.

## Commands

```bash
ctest --test-dir cmake-build -R "^conformance_13_rest_generic_corpus$" --output-on-failure
# manual: SIMPLE_HTTP_BIN=cmake-build/url_shortener python tests/conformance/py/13_rest_generic_corpus.py -v
```

## Tests

| Test file | Target | CTest label | Cases |
|---|---|---|---|
| `tests/conformance/py/13_rest_generic_corpus.py` | `conformance_13_rest_generic_corpus` | `integration;conformance` | whole corpus on `rest_generic`; status code equals catalog mapping for every error case; response `Content-Type: application/json`; server started with `--max-request-body-bytes` default so oversize body case yields the catalog error |

## Constraints

- Server always bound to `127.0.0.1` on a free port; never `0.0.0.0`.
- No secrets in logs: tokens never printed; failure output redacts `Authorization` headers.
- TLS off in these runs (TLS covered by existing tests).
- Server stdout/stderr go to files in `ctx.tmp`, attached to failure output.

## Success criteria

1. All non-skipped cases pass; every skip has a corpus-declared reason.
2. HTTP status equals the catalog value for each error code present in the corpus.
3. Server processes are always terminated (no orphan after a failing case).
4. "Resolved decisions" section records the role strategy chosen.

## Out of scope / risks

- CSRF/cookie behavior (Milestone 0002) is not tested here.
- Risk: per-case server start makes the run slow (about 150 cases x 0.4 s); permitted fallback is grouping cases by `(backend, caller-set)` and restarting only when it changes, never sharing a store between cases.
