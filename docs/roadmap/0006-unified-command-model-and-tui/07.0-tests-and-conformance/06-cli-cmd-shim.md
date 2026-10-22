# 06 - CLI `cmd` shim

**Parent task:** 07.0 Tests and conformance
**State:** ⬜ Not started
**Depends on:** 05; Task 04.0 (`cmd` subcommand), Task 02.0 (cross-process persistence)
**Blocks:** 15, 33
**Estimated size:** S
**Suggested agent:** testing-expert

## Objective

Run the corpus through `url_shortener cmd <name> --json -` as a real child
process, one process per step, state carried by a SQLite file in the case temp
dir - proving the CLI transport returns the same envelope and the catalog's
exit code for every case.

## Dependencies

- Subtask 05 (`Adapter`, `CaseContext`).
- 04.0: `cmd` prints the full response envelope on stdout by default (`--raw` prints only `result`); `--json -` reads args from stdin; exit code from the catalog error table.
- 02.0: a persistent backend selectable by CLI so consecutive processes share state.

## Files affected

| Path | create/modify | What changes |
|---|---|---|
| `tests/conformance/py/10_cli_cmd_corpus.py` | create | `CliCmdAdapter` + `main_for` |
| `sources.cmake` | modify | Append to `CONFORMANCE_PY_SOURCES` |

## Classes and functions

```python
class CliCmdAdapter(Adapter):
    transport = "cli_cmd"
    def begin_case(self, case, ctx):
        if case.get("backend") == "inmemory": return "one-shot CLI cannot share in-memory state"
    def send(self, step, request_text, ctx) -> dict:
        req = json.loads(request_text)                       # raw_request never reaches here (caps.raw False)
        argv = [find_binary(), *ctx.storage_args, "cmd", req["command"], "--json", "-"]
        if role(step) == "user": argv.insert(1, "--read-only")   # exact flag position: UNVERIFIED, see 04.0
        if req.get("options", {}).get("dry_run"): argv.append("--dry-run")   # UNVERIFIED flag spelling
        if req.get("options", {}).get("async"):   argv.append("--async")
        proc = subprocess.run(argv, input=json.dumps(req.get("args", {})), capture_output=True,
                              text=True, timeout=30, cwd=ctx.tmp)
        env = json.loads(proc.stdout)                         # failure: report stdout+stderr
        if "cli_exit" in step:
            expected_exit = step["cli_exit"]
        elif (step.get("await_job") and env["ok"]
              and env["result"].get("state") in ("failed", "cancelled")):
            expected_exit = 4
        else:
            expected_exit = 0 if env["ok"] else CLI_EXIT[env["error"]["code"]]
        assert proc.returncode == expected_exit
        return env
```

Because `cmd` builds the envelope itself, the CLI response `id` is not
comparable (`caps.id = False`). Jobs: `cmd` waits for completion by default
(catalog section 4), so for `await_job` steps the shim treats the single
printed final `job.get` envelope as the awaited result and synthesizes the
`expect_accept` check only from a second call with `--no-wait`; a case that
needs both lists `cli_cmd` under `transports.skip` for `expect_accept` with the
reason `"no-wait accept envelope covered in 13"` **only if** subtask 13 provides
the dedicated `--no-wait` case.

## Implementation steps

1. Implement the adapter (about 50 lines).
2. Pass `cwd=ctx.tmp` so a stray `uri.txt` or log would land in the case dir (also feeds subtask 21).
3. On non-zero exit with unparsable stdout, raise with stdout, stderr and argv (secrets: argv contains no secrets by construction).
4. Add runner entry `main_for(CliCmdAdapter, "cli_cmd")`.

## Commands

```bash
ctest --test-dir cmake-build -R "^conformance_10_cli_cmd_corpus$" --output-on-failure
# direct: SIMPLE_HTTP_BIN=cmake-build/url_shortener python tests/conformance/py/10_cli_cmd_corpus.py
# Windows: SIMPLE_HTTP_BIN=cmake-build\Debug\url_shortener.exe ; add -C Debug to ctest
```

## Tests

| Test file | Target | CTest label | Cases |
|---|---|---|---|
| `tests/conformance/py/10_cli_cmd_corpus.py` | `conformance_10_cli_cmd_corpus` | `integration;conformance` | whole corpus on `cli_cmd`; exit code equals catalog mapping for every error case; each process exits within 30 s; stdout is exactly one JSON line |

## Constraints

- Real process, real binary; no in-process shortcut.
- One-shot process per step: total corpus time budget 120 s on CI (about 4 steps/case x 150 cases x 150 ms); if exceeded, batch setup steps with `serve-stdio` is NOT allowed here - parallelize cases across worker threads with separate temp dirs instead.
- Test TIMEOUT 300 (set in subtask 05 CMake loop).

## Success criteria

1. Every non-skipped corpus case passes; skips only for `backend: inmemory`.
2. For every `expect.error.code` the process exit code equals the catalog value.
3. No case leaves a listening socket (the run is followed by a port probe of 8000 and the case's reserved port, same pattern as `13_cli_no_server_socket.sh`).

## Out of scope / risks

- UNVERIFIED flag names (`--read-only` placement, `--dry-run`, `--async`, `--no-wait`) are fixed by 04.0; update this shim only.
- `link <verb>` sugar equivalence belongs to 04.0's own tests (output is result-only there).
