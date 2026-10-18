# 05 - Python harness common module

**Parent task:** 07.0 Tests and conformance
**State:** ⬜ Not started
**Depends on:** 01, 02
**Blocks:** 06, 07, 08
**Estimated size:** M
**Suggested agent:** testing-expert

## Objective

One Python module that the four out-of-process shims share: corpus loading,
variable handling, the adapter base class, case execution with job polling,
binary discovery, per-case temp storage, and free-port allocation. Each shim
(06-09) then only implements "send one request, return one envelope".

## Dependencies

- Subtasks 01, 02 (`conformance_match.py`).
- Existing helpers to reuse, not copy: `find_binary()` in `tests/integration/cli/cli_integration_common.py` (honours `SIMPLE_HTTP_BIN`, falls back to `cmake-build*/` candidates incl. `.exe`) and the `ServerHarness` / `request` helpers in `tests/integration/core/integration_common.py` (server start with fixed sleep; this module adds a readiness poll instead).

## Files affected

| Path | create/modify | What changes |
|---|---|---|
| `tests/conformance/py/conformance_common.py` | create | Everything below |
| `tests/conformance/py/02_harness_selftest.py` | create | Self-test with a fake in-process adapter (no binary) |
| `sources.cmake` | modify | Append self-test to `CONFORMANCE_PY_SOURCES` |
| `CMakeLists.txt` | modify | In the `CONFORMANCE_PY_SOURCES` loop set `ENVIRONMENT "SIMPLE_HTTP_BIN=$<TARGET_FILE:url_shortener>;URLSHORTENER_CONFORMANCE_DIR=<repo>/tests/conformance"`, `TIMEOUT 300`, labels `integration;conformance` |

## Classes and functions

```python
TRANSPORT_CAPS = {                     # keep in sync with TransportCaps in C++
  "direct": Caps(id=True,  raw=True,  options=True),
  "rest_generic": Caps(id=True, raw=True, options=True),
  "rest_resource": Caps(id=False, raw=False, options=False),
  "cli_cmd": Caps(id=False, raw=False, options=True),   # UNVERIFIED: depends on 04.0 flags for dry_run/async
  "stdio": Caps(id=True, raw=True, options=True),
  "tui": Caps(id=True, raw=False, options=True),
}

HTTP_STATUS = {"invalid_envelope":400,"unknown_command":404,"invalid_field":400,"invalid_url":400,
               "invalid_slug":400,"reserved_slug":409,"slug_conflict":409,"not_found":404,
               "not_authorized":403,"unsupported":501,"busy":503,"job_cancelled":200,
               "storage_failure":500,"internal":500}
CLI_EXIT = {"invalid_envelope":2,"unknown_command":2,"invalid_field":2,"invalid_url":2,"invalid_slug":2,
            "reserved_slug":2,"slug_conflict":3,"not_found":1,"not_authorized":5,"unsupported":2,
            "busy":4,"job_cancelled":4,"storage_failure":4,"internal":4}   # = the 01.0/02 error-code table (`reserved_slug` is 409, catalog section 1 agrees); subtask 16 asserts equality

class CaseContext:                      # per case
    tmp: Path; backend: str; variables: dict; storage_args: list[str]
    def storage_config_path(self) -> Path        # writes sqlite YAML into tmp (format per docs/storage/configuration.md)

class Adapter(ABC):
    transport: str
    def begin_case(self, case: dict, ctx: CaseContext) -> str | None   # None = run, str = skip reason
    @abstractmethod
    def send(self, step: dict, request_text: str, ctx: CaseContext) -> dict   # returns actual envelope (parsed)
    def end_case(self) -> None

def find_binary() -> str                          # wraps cli_integration_common.find_binary
def free_port() -> int                            # bind port 0, close, return
def wait_http_ready(port: int, timeout: float = 15.0) -> None   # polls GET /healthz (observability route)
def load_corpus(directory: Path) -> list[dict]    # sorted files, injects suite/file into each case
def run_case(case: dict, adapter: Adapter) -> list[str]         # [] = pass; else findings
def build_suite(adapter_factory, transport: str) -> unittest.TestSuite  # one test method per case (named test_<case>)
def main_for(adapter_factory, transport: str) -> None            # unittest.main wrapper used by shims
```

`run_case` mirrors `CaseRunner` (C++): skip resolution (`transports.only/skip`,
`raw_request` vs caps, `backend` vs adapter), `${tmp}` and variable
substitution, `save`, `await_job` polling (25 ms interval, `timeout_ms`), id
and status checks (`HTTP_STATUS` / `CLI_EXIT` unless the step overrides),
`normalize_actual`, `match`. Skipped cases are reported through `unittest`
`skipTest(reason)` so CI shows them; a case skipped on a transport **without** a
`transports.skip` entry is a failure (capability skips must be explicit in the
corpus, except `raw_request` which is capability-derived and listed in the
README of the corpus).

## Implementation steps

1. Write `conformance_common.py` (about 350 lines max); no third-party imports.
2. `storage_config_path` writes the YAML for a SQLite metadata backend in the same shape `ParseStorageConfigYaml` accepts (copy a minimal known-good document from `docs/storage/configuration.md`; Task 02.0 selects it with `--storage-config <file>` (env `URL_SHORTENER_STORAGE_CONFIG`), so `CaseContext.storage_args == ["--storage-config", str(path)]`; keep it in one place so shims never build argv themselves).
3. `02_harness_selftest.py` drives `run_case` with a `FakeAdapter` that echoes canned envelopes: pass path, mismatch path, skip path, job polling path, timeout path.
4. `build_suite`/`main_for` write the same summary JSON as the C++ runner (`$URLSHORTENER_CONFORMANCE_OUT`, file `conformance_summary_<transport>.json`) for the coverage gate (subtask 15).
5. Document in the docstring how to add an adapter (5 steps).

## Commands

```bash
python tests/conformance/py/02_harness_selftest.py
ctest --test-dir cmake-build -R "^conformance_02_harness_selftest$" --output-on-failure
```

## Tests

| Test file | Target | CTest label | Cases |
|---|---|---|---|
| `tests/conformance/py/02_harness_selftest.py` | `conformance_02_harness_selftest` | `integration;conformance` | pass; mismatch with path report; explicit skip; implicit skip -> failure; job poll success; job poll timeout; variable save/substitute; `${tmp}` Windows path normalized to `/` |

## Constraints

- Python 3.9+ syntax compatible with the CI image (`python-version: "3.12"` in e2e workflow; Ubuntu 24.04 system Python 3.12).
- `free_port()` has an inherent race; callers retry server start up to 3 times on bind failure.
- Must run on Windows: `subprocess` with list args, no shell, binary path from `find_binary()` (handles `.exe`).
- Never print request bodies containing `${tmp}` expansions beyond the case temp dir.

## Success criteria

1. Self-test passes; 8 cases above present.
2. `HTTP_STATUS`/`CLI_EXIT` are exact copies of the catalog table (subtask 16 enforces).
3. An adapter is implementable in under 60 lines (checked by the sizes of 06 and 07).

## Out of scope / risks

- A C++ twin of the HTTP/CLI mapping tables is intentionally absent (C++ shims are in-process).
- Risk: `wait_http_ready` depends on `/healthz` being served by the server mode with the chosen storage config; if 02.0 changes startup order, adjust here only.
