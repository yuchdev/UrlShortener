# 13 - CLI persistence tests

**Parent task:** 02.0 Storage path unification
**State:** ⬜ Not started
**Depends on:** 02.0/12
**Blocks:** 15
**Estimated size:** M
**Suggested agent:** testing-expert

## Objective

Turn the two documented limitation tests into two-sided tests. Today both
*assert* that separate CLI processes do not share state, which is exactly what
this task removes for persistent backends:

- `tests/integration/cli/03_link_create_then_get_persists_state.py`
  (ctest `cli_03_link_create_then_get_persists_state`): three cases assert
  "get after create in a new process fails with `Link not found`".
- `tests/e2e/scripts/sections/12_cli_link_get.sh`
  (ctest `e2e_12_cli_link_get`, non-Windows only): the same by slug and by id.

Despite the file names, the current assertions are about isolation. After this
subtask: **without** `--storage-config` they keep asserting isolation (default
in-memory behavior, unchanged contract), **with** a SQLite `--storage-config`
they assert persistence across processes. The two ctest names stay (names are
referenced by ADR 0001, docs/cli/README.md and the milestone docs).

## Dependencies

- 02.0/12 (binary supports `--storage-config`).
- Test helpers (verified): `tests/integration/cli/cli_integration_common.py`
  (`run_cli(binary, *args, cwd=None, timeout=10)` -> `subprocess.run` with
  captured text output; **no `env` parameter today**, so subprocesses inherit
  the caller's environment, including a possible
  `URL_SHORTENER_STORAGE_CONFIG`), `parse_json_stdout`, `CliIntegrationBase`.
- `tests/e2e/scripts/sections/12_cli_link_get.sh` structure (verified: binary
  discovery via `URLSHORTENER_BIN`, `mktemp -d`, `assert_not_found` helper).
- CTest registration (verified): CLI python tests via `CLI_INTEGRATION_SOURCES`
  as `cli_<stem>` with `SIMPLE_HTTP_BIN`; e2e sections via `E2E_CLI_SECTIONS`,
  registered only when `NOT CMAKE_HOST_WIN32` and bash exists.

## Files affected

| Path | Create / modify | What changes |
|---|---|---|
| `tests/integration/cli/cli_integration_common.py` | modify | `run_cli(..., env=None)`; helper `write_sqlite_storage_yaml(tmpdir) -> str`; helper `clean_env()` removing `URL_SHORTENER_STORAGE_CONFIG` |
| `tests/integration/cli/03_link_create_then_get_persists_state.py` | modify | Keep 3 isolation cases (clean env, no flag); add 3 persistence cases |
| `tests/e2e/scripts/sections/12_cli_link_get.sh` | modify | Unset the env var for the isolation half; add a SQLite half |
| `tests/integration/cli/*.py` (other 15 files) | none | They run in default mode; only verify they still pass |

No `CMakeLists.txt`/`sources.cmake` change (both files already registered).

## Classes and functions

Helpers (Python):

```python
def clean_env() -> dict:
    env = dict(os.environ); env.pop("URL_SHORTENER_STORAGE_CONFIG", None); return env

def write_sqlite_storage_yaml(tmpdir: str, name: str = "shortener.sqlite3") -> str:
    path = Path(tmpdir) / "storage.yaml"
    db = (Path(tmpdir) / name).as_posix()          # forward slashes: safe in YAML on Windows
    path.write_text(f"metadata:\n  backend: sqlite\n  sqlite_path: {db}\n", encoding="utf-8")
    return str(path)
```

New/changed test cases in `03_link_create_then_get_persists_state.py`:

| Case | Mode | Expectation |
|---|---|---|
| `test_create_then_get_by_slug_is_not_found_in_new_process` | default (existing, now with `env=clean_env()`) | get exits non-zero, stderr has `Link not found` |
| `test_create_then_get_by_id_is_not_found_in_new_process` | default (existing) | same |
| `test_different_tmpdirs_are_isolated` | default (existing) | same |
| `test_sqlite_create_then_get_by_slug_persists` | `--storage-config` | create exit 0; get in a **new process** exit 0; `slug`, `target_url`, `id` equal to the create output |
| `test_sqlite_create_then_get_by_id_persists` | `--storage-config` | get by `--id` returns the same link |
| `test_sqlite_lifecycle_persists_across_processes` | `--storage-config` | `link disable`, then `link get` shows `enabled == false`; `link delete`, then `link get` shows a `deleted_at`; `link restore` clears it; every step in its own process |
| `test_sqlite_two_databases_are_isolated` | two YAMLs with different db files | link created against A is `Link not found` against B |
| `test_sqlite_duplicate_slug_conflict_across_processes` | `--storage-config` | second create of the same slug exits 3 (slug_conflict) |

Docstrings/class names are corrected to say what is asserted (the old class
docstring claims persistence while asserting isolation); file name unchanged.

`12_cli_link_get.sh`: keep `assert_not_found` under `env -u
URL_SHORTENER_STORAGE_CONFIG`; add steps 4-6: write `storage.yaml` into
`$TMPDIR_CLI`, `link create ... --storage-config`, then `link get --slug` and
`link get --id` in separate invocations must exit 0 and print JSON whose `slug`
matches; final line `PASS: 12_cli_link_get`.

## Implementation steps

1. Add the helpers; thread `env=` into `run_cli`.
2. Update the three existing cases to pass `env=clean_env()`.
3. Add the five new cases.
4. Update the shell section with the `env -u` handling and the SQLite half.
5. Run both tests, then the whole CLI integration family.

## Commands

```powershell
cmake --build cmake-build --target url_shortener
ctest --test-dir cmake-build -R "^cli_03_link_create_then_get_persists_state$" --output-on-failure
ctest --test-dir cmake-build -R "^cli_" --output-on-failure
# non-Windows only (registered when bash exists and not CMAKE_HOST_WIN32):
ctest --test-dir cmake-build -R "^e2e_12_cli_link_get$" --output-on-failure
# direct run, Windows included:
$env:SIMPLE_HTTP_BIN = (Resolve-Path ./cmake-build/url_shortener.exe)
python tests/integration/cli/03_link_create_then_get_persists_state.py
```

(Multi-config generators: use `-C Debug` and the `Debug/` binary path. The e2e
section cannot run under native Windows; use WSL or Git Bash with
`URLSHORTENER_BIN` set, or rely on Linux CI.)

## Tests

| Test file | Target | Label | Cases |
|---|---|---|---|
| `tests/integration/cli/03_link_create_then_get_persists_state.py` | `cli_03_link_create_then_get_persists_state` | integration | table above (3 existing + 5 new) |
| `tests/e2e/scripts/sections/12_cli_link_get.sh` | `e2e_12_cli_link_get` | e2e | isolation half (existing) + SQLite half |

Boundary/failure cases covered: duplicate slug (exit 3), missing link after
reopen (exit 1), cross-database isolation, soft-delete/restore persistence,
by-id lookup (exercises `findById` scan on SQL).

Manual check: run the SQLite scenario twice in the same temp directory to make
sure leftover database files do not break re-entrancy (the tests use fresh
`TemporaryDirectory`s, so they should not).

## Constraints

- Tests must not depend on the developer's environment: `clean_env()` removes
  `URL_SHORTENER_STORAGE_CONFIG`; temp directories only; absolute db paths.
- Windows path hygiene: forward slashes in YAML; no `cwd` reliance (the db path
  is absolute; `cwd=tmpdir` kept for the isolation cases as before).
- No server is started in these tests (CLI only; C5 preserved - the existing
  `04_cli_exits_no_listening_port.py` still passes).
- Exit codes asserted come from `ExitCodeForAppError` (1 not found, 3 conflict).

## Success criteria

1. `cli_03_link_create_then_get_persists_state` passes with all eight cases.
2. `e2e_12_cli_link_get` passes on a Linux host.
3. The pre-existing isolation assertions still pass in default mode (they
   prove the default did not change).
4. All other `cli_*` integration tests pass unmodified.

## Out of scope / risks

- Server + CLI sharing one file (14); Postgres (needs a service).
- `link stats` across processes stays empty (`click_events` are in-process,
  README R-3); not asserted here.
- SQLite lock contention between simultaneous CLI processes is not exercised;
  the tests are sequential.
