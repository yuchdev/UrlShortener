# 33 - CI wiring

**Parent task:** 07.0 Tests and conformance
**State:** ⬜ Not started
**Depends on:** 04, 06-10, 30, 32 (and the TUI build option from 06.0)
**Blocks:** 34
**Estimated size:** S
**Suggested agent:** cpp-expert (workflow YAML + e2e shell) with testing-expert review

> **Amended 2026-11-20 (as-built reconciliation).** Adds the **portable timeout
> fallback** (user ruling 2026-11-19, item 3) to e2e sections
> `tests/e2e/scripts/sections/1[4-8]_*.sh`. As built they call GNU `timeout 5`
> directly (e.g. `14_cli_link_update.sh:73`, `15:64`, `16:68`, `17:64`,
> `18:64`), which is absent on macOS (`gtimeout` exists) and caused the standing
> `e2e_14`-`18` failures. The fix: a tiny portable shim that uses `timeout` if
> present, else `gtimeout`, else a small background-watchdog fallback.

## Objective

Make sure the new suites actually run in CI and a TUI-less configuration keeps
building: the current workflows run only `-L unit` and `-L integration`
(Ubuntu 24.04, Release, Ninja), nightly `-L perf || true`, and an e2e
workflow limited to path-filtered PRs that runs `run_all_sections.sh`
(sections 01-10 only). The CLI e2e sections (11-18) are registered through
CTest on non-Windows hosts but no workflow selects `-L e2e`.

## Dependencies

- `.github/workflows/ubuntu-unit-tests.yml`, `ubuntu-integration-tests.yml`, `perf-analytics.yml`, `e2e-tests.yml` (inspected).
- `scripts/setup_ubuntu_dependencies.sh` (dependency install; FTXUI is fetched by CMake `FetchContent` per ADR 0003 so no apt package is required; verify network access in CI).
- Labels: conformance and TUI-unit tests already carry `integration;conformance` / `unit`, so existing jobs pick them up automatically once registered. This subtask adds what automatic selection does not cover.

## Files affected

| Path | create/modify | What changes |
|---|---|---|
| `.github/workflows/ubuntu-unit-tests.yml` | modify | Add matrix `tui: [ON, OFF]` (`-DURLSHORTENER_ENABLE_TUI=${{ matrix.tui }}`); for OFF run build + `-L unit` and assert `ctest -N -R "^tui__"` lists nothing |
| `.github/workflows/ubuntu-integration-tests.yml` | modify | After the existing step add `ctest --test-dir build --output-on-failure -L "e2e" -E "^e2e_(0[1-9]|10)_"` for the CLI e2e sections (they need no mock service); upload `coverage_matrix.md` and conformance summaries as artifacts (`actions/upload-artifact@v4`) |
| `.github/workflows/perf-analytics.yml` | modify | Rename job description comment only; add step publishing the redirect guard table to `$GITHUB_STEP_SUMMARY`; keep `continue-on-error: true`; add `URLSHORTENER_PERF_GATE` unset |
| `.github/workflows/e2e-tests.yml` | modify | Add `tests/conformance/**` and `docs/commands/**` to the `pull_request.paths` filter |
| `docs/agent/ci-pipeline-specification.md` | modify | One short subsection documenting the jobs above (docs-sync owner is 08.0 subtask on testing docs; this file is changed only if the spec lists job names) |
| `tests/e2e/scripts/sections/14_cli_link_update.sh` | modify | replace `timeout 5` with the portable shim |
| `tests/e2e/scripts/sections/15_cli_link_delete.sh` | modify | same |
| `tests/e2e/scripts/sections/16_cli_link_enable_disable.sh` | modify | same |
| `tests/e2e/scripts/sections/17_cli_link_restore.sh` | modify | same |
| `tests/e2e/scripts/sections/18_cli_link_preview.sh` | modify | same |
| `tests/e2e/scripts/common.sh` | modify | define the shared `run_with_timeout` shim once (sections 14-18 source it; if a section does not source `common.sh`, inline the shim in its header) |

## Portable timeout fallback (e2e 14-18)

Define one shim (preferably in `tests/e2e/scripts/common.sh`, which the sections
can source, else inline in each section's header) and call it where the sections
currently call `timeout 5`:

```sh
# run_with_timeout <seconds> <cmd...> : prefer GNU timeout, then gtimeout,
# else a background watchdog. Preserves the command's own exit status, and
# returns 124 on timeout, matching GNU timeout so the existing
# "capture rc with || rc=$?" logic is unchanged.
run_with_timeout() {
  secs=$1; shift
  if command -v timeout >/dev/null 2>&1; then
    timeout "$secs" "$@"
  elif command -v gtimeout >/dev/null 2>&1; then
    gtimeout "$secs" "$@"
  else
    "$@" &
    cmd_pid=$!
    ( sleep "$secs"; kill -TERM "$cmd_pid" 2>/dev/null ) &
    watch_pid=$!
    wait "$cmd_pid"; rc=$?
    kill -TERM "$watch_pid" 2>/dev/null; wait "$watch_pid" 2>/dev/null
    return $rc
  fi
}
```

The sections keep `set -euo pipefail` and the existing `|| rc=$?` capture (the
comment at e.g. `14_cli_link_update.sh:69` stays valid; the shim returns 124 on
timeout like GNU `timeout`). The CLI itself runs its own `io_context` and never
blocks, so a timeout firing is still a genuine failure, not a flake.

## Classes and functions

Workflow fragments (shape):

```yaml
strategy:
  matrix:
    tui: ["ON", "OFF"]
steps:
  - run: cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DURLSHORTENER_ENABLE_TUI=${{ matrix.tui }}
  - run: cmake --build build
  - run: ctest --test-dir build --output-on-failure -L "unit"
  - if: matrix.tui == 'OFF'
    run: test -z "$(ctest --test-dir build -N -R '^tui__' | grep -E 'Test +#')"
```

Selection table:

| Layer | ctest selector | Workflow | Blocking |
|---|---|---|---|
| unit incl. `tui__*`, `commands__*`, `docs__03/04/05` | `-L unit` | unit-tests (matrix) | yes |
| conformance (C++ and Python), security process tests | `-L integration` (contains `conformance`) | integration-tests | yes |
| CLI e2e incl. `e2e_19_*` | `-L e2e` minus mock-service sections | integration-tests (new step) | yes |
| perf redirect guard | `-L perf` | nightly perf | no (advisory) |
| TUI manual smoke checklist (`docs/testing/tui_manual_smoke.md`) | none | n/a | milestone sign-off (34) |

## Implementation steps

1. Edit the four workflows; keep Ubuntu 24.04 / Ninja / Release.
2. Run the modified jobs on a branch (`workflow_dispatch` is not enabled on unit/integration; trigger via push) and confirm counts: expected new tests appear in the log.
3. Windows: no CI exists (local only); note in the PR that Windows runs of conformance tests are manual (`-C Debug`).
4. macOS: no CI; ActiveListener is a separate repo; nothing to do.

## Commands

```bash
# local reproduction of each job
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DURLSHORTENER_ENABLE_TUI=ON && cmake --build build
ctest --test-dir build --output-on-failure -L unit
ctest --test-dir build --output-on-failure -L integration
ctest --test-dir build --output-on-failure -L e2e -E "^e2e_(0[1-9]|10)_"
ctest --test-dir build -L perf --output-on-failure
```

## Tests

| Test file | Target | CTest label | Cases |
|---|---|---|---|
| (no new test files; workflow validation) | n/a | n/a | job log shows expected test counts; `TUI=OFF` job lists zero `tui__*` |

## Constraints

- Do not add secrets or tokens to workflows; no network services other than package/FetchContent downloads.
- Keep jobs under existing runtime budget (the integration job gains the conformance suite: target < 10 min total; if more, split conformance into its own job rather than raising timeouts).
- `|| true` stays only on the perf step.

## Success criteria

1. One CI run on the branch executes: all `conformance_*` and `conformance__*` tests, `commands__*`, `tui__*` (TUI=ON), `docs__03-05`, `e2e_19_*`, and the perf guard.
2. TUI=OFF configuration builds and passes `-L unit` with zero `tui__*` tests.
3. Artifacts contain the coverage matrix.

## Out of scope / risks

- Windows/macOS CI: not part of this milestone.
- Risk: FTXUI FetchContent adds minutes to every job; consider caching `_deps` (actions/cache) if build time grows by > 3 min.
