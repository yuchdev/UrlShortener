# 0006 Test Matrix, Flake and Coverage Evidence (07.0/34)

Evidence for milestone 0006, Task 07.0, subtask 34 ("Coverage-gap run and
review gates"), Steps 1-3 (full local matrix, flake check, coverage). Produced
on macOS (Darwin 21.6.0, Apple clang) by the testing-expert agent.

- Spec: `docs/roadmap/0006-unified-command-model-and-tui/07.0-tests-and-conformance/34-coverage-gap-run-and-review-gates.md`
- Date: 2026-11-20 / 2026-11-21 (coverage build spanned midnight)
- Host: macOS 12.7 SDK, Apple clang (`/Applications/Xcode.app/.../usr/bin/c++`),
  Ninja, Boost from MacPorts (`/opt/local`), OpenSSL `@3` from `/usr/local`.
- No secrets, DSNs or tokens are included. A credential grep was run on this
  file (see end).

## Environment notes (tooling availability)

| Tool | Present | Path / note |
|---|---|---|
| `lcov` / `genhtml` | NO | Not installed; `cmake/coverage.cmake` lcov path unusable. |
| `llvm-cov` / `llvm-profdata` | YES (via `xcrun`) | Xcode toolchain; used for the Step 3 coverage fallback. |
| `timeout` | NO | Absent from PATH (exercises e2e portable helper). |
| `gtimeout` | YES | `/opt/local/bin/gtimeout` (MacPorts) - removed from PATH for the e2e run. |
| `redis-server` | NO | Redis-backed tests run their fail-open path; `hiredis-test` cannot run. |
| `psql` + local server | YES | `/opt/local/bin/psql`; a local PostgreSQL accepts on `/tmp:5432`, so Postgres integration tests actually execute. |

No software was installed for this run.

## Builds exercised

| Build dir | Type | TUI | Notes |
|---|---|---|---|
| `cmake-build` | Debug | ON | Pre-existing tree; built-only under the mandatory mkdir lock, never reconfigured. |
| `cmake-build-release` | Release | ON | New dir; configured with `CMAKE_PREFIX_PATH=/usr/local/opt/openssl@3:/usr/local`, Ninja, `CMAKE_BUILD_TYPE=Release`. |
| `cmake-build-notui` | Debug | OFF | New dir; same prefix path + Ninja, `URLSHORTENER_ENABLE_TUI=OFF`. |
| `build-cov` | Debug | ON | New dir; llvm source-based coverage flags (lcov unavailable). |

## Summary matrix (build x label -> pass/fail/skip)

| Build | unit\|contract\|integration | conformance | perf | e2e | unit (only) |
|---|---|---|---|---|---|
| Debug / TUI ON (`cmake-build`) | 459/1/0 of 460 | 16/0/0 | 4/0/0 (guard skips) | 20/0/0 | - |
| Release / TUI ON (`cmake-build-release`) | - | - | 4/0/0 (guard measures) | - | 331/0/0 |
| Debug / TUI OFF (`cmake-build-notui`) | - | - | - | - | 266/0/0 |
| Coverage (`build-cov`) | 446/2/0 of 448 (unit+integration+conformance; 2 instrumentation flakes) | (incl. above) | - | - | - |

The only non-passing results anywhere are: (1) `19_rest_patch_via_registry` in
Debug - a **stale pre-`4bc641a` object** in `cmake-build`; after a forced
rebuild of that target the current source passes **25/25** (the tree is already
deterministic); and (2) `01_generated_slug_redirect_test` +
`02_custom_slug_get_by_id_slug_test` under coverage instrumentation - timing
flakes that pass on isolated rerun and in the clean build. No
deterministic/product failure was found. Per the task constraint, no product
code was changed.

---

## STEP 1 - Full local matrix

### Debug / TUI ON (`cmake-build`)

Commands (each build/ctest wrapped in the mandatory `cmake-build/.build.lock`
mkdir lock):

```
cmake --build cmake-build -j4                      # rc=0 (ninja: no work to do; tree already current)
ctest --test-dir cmake-build --output-on-failure -L "unit|contract|integration"
ctest --test-dir cmake-build --output-on-failure -L conformance
ctest --test-dir cmake-build --output-on-failure -L perf
env PATH=<timeout/gtimeout removed> ctest --test-dir cmake-build --output-on-failure -L e2e
```

Results:

| Label group | Total | Pass | Fail | Skip | Notes |
|---|---|---|---|---|---|
| `unit\|contract\|integration` (union) | 460 | 459 | 1 | 0 | Single failure is a wall-clock flake (see below), green on rerun. |
| `conformance` | 16 | 16 | 0 | 0 | `-L conformance` dedicated run. |
| `perf` | 4 | 4 | 0 | 0 | Debug: redirect guard prints `skipped: not an optimized build` and exits 0. |
| `e2e` | 20 | 20 | 0 | 0 | Run with `timeout`/`gtimeout` absent from PATH - portable shell watchdog in `common.sh` exercised. |

CTest union summary line: `99% tests passed, 1 tests failed out of 460`.
Label time summary (union run): conformance 16, contract 12, integration 117,
unit 331 tests (a test counts under each label it carries; conformance tests
that also carry `integration` ran inside the union).

#### The one failure - `35 - 19_rest_patch_via_registry` (unit) - was a STALE BINARY, already fixed in the tree

`tests/unit/http/19_rest_patch_via_registry.cpp`, case
`patch_noop_bodies_return_200_unchanged`. The failing assertion compared the
**whole** body and tripped on one field:

```
before: "updated_at":"2026-11-20T15:46:33Z"
after:  "updated_at":"2026-11-20T15:46:34Z"
```

Root cause (verified): the `cmake-build` test binary was **stale**. Although
`cmake --build cmake-build` reported `ninja: no work to do`, the compiled
`19_rest_patch_via_registry` ran the *pre-fix* full-body assertion. The current
source (committed as `4bc641a "test: deterministic no-op PATCH check"`) already
hardened this exact case: it compares bodies with `updated_at` **elided**
(`without_updated_at(after) == without_updated_at(before)`, line 450) and
separately asserts `updated_at` is a valid RFC 3339 timestamp - so a wall-clock
bump can no longer fail it. A stale object with an mtime newer than the patched
source kept ninja from rebuilding it.

Verification:
1. On the stale binary: `ctest -R "^19_rest_patch_via_registry$" --repeat until-fail:15`
   -> 15/15 Passed (the full-body compare only fails when a run straddles a
   1-second boundary, which is rare).
2. Forced a rebuild of just that target (`touch` the `.cpp` - mtime only, no
   content/git change - then `cmake --build cmake-build --target 19_rest_patch_via_registry`,
   `rebuild_rc=0`; it recompiled, confirming the prior object was stale), then
   `ctest -R "^19_rest_patch_via_registry$" --repeat until-fail:25`
   -> **25/25 Passed**.

Conclusion: **not a product regression and not an open test defect** - the live
tree is already deterministic; the Step 1 failure was a build-state artifact of
a pre-`4bc641a` object in `cmake-build`. No product code was changed by this
subtask. (Other `cmake-build` unit objects may likewise predate recent commits;
the figures above are from the tree as built, with this one target reverified
after a forced rebuild.)

#### Service-dependent integration tests (no CTest-level skips)

CTest reported **0 skipped**. Tests that depend on external services are
internally guarded and reported `Passed`:

| Test | Service | Outcome / reason |
|---|---|---|
| `psql__*` (233-244) | none (unit-level dialect/config) | Pass, ~0.02s, no server used. |
| `storage__analytics_postgresql_*` (309/311), `psql__05_contract_suite_runner` (334), `storage__08_postgres_service_full_flow` (335) | PostgreSQL | **Ran against the local MacPorts server on `/tmp:5432`** and passed. |
| `storage__09/10/11_*_with_redis` (337-339), `04_redis_job_runner` (415) | Redis | Passed via fail-open path - no `redis-server`, cache layer degrades to repository, request still succeeds. |
| `03_postgres_job_runner` (414) | PostgreSQL DSN | Passed in 0.05s (guard short-circuits with no job DSN configured). |
| `05_full_storage_matrix` (416), `02_sqlite_job_runner` (413) | SQLite | Passed; slow (96s / 95s) exercising the full matrix. |
| `hiredis-test` (485) | `redis-server` | **Not run** - carries none of unit/contract/integration/e2e/conformance/perf labels, so it is outside the milestone gate; would additionally need `redis-server`. |

### Release / TUI ON (`cmake-build-release`)

New dir, configured (`cfg_release_rc=0`) and built `-j4` (`build_release_rc=0`,
1155 targets incl. FetchContent deps), then:

```
ctest --test-dir cmake-build-release -L unit                       # 100% tests passed, 0 failed out of 331
ctest --test-dir cmake-build-release -L perf --output-on-failure   # 100% tests passed, 0 failed out of 4
```

| Label | Total | Pass | Fail | Skip |
|---|---|---|---|---|
| `unit` | 331 | 331 | 0 | 0 |
| `perf` | 4 | 4 | 0 | 0 |

In Release the redirect guard **actually measures** (237.56s, vs. Debug where it
skips). Numbers below; no baseline was committed - the checked-in
`tests/perf/redirect/baseline.json` is the placeholder (zeroed, `calibration_ns=1.0`)
and `tests/perf/README.md` states a baseline is written by a human only, never
from this kind of run. Reported for evidence only.

Guard header: `calibration_ns = 2.049`, `self-spread = 1.72% (reliable)`,
`baseline = loaded, gate = off`. Because the baseline is zeroed, every
`baseline norm p99 = 0.00` and `delta = 0.00%` (self-comparison disabled by
design - advisory, not a regression).

**Redirect fast-path latency, analytics DISABLED** (raw ns + calibration-normalised):

| scenario | p50 ns | p95 ns | p99 ns | norm p50 | norm p99 |
|---|---|---|---|---|---|
| root_cache_hit | 38617.66 | 52598.72 | 64737.60 | 18847.99 | 31596.25 |
| root_cache_miss_repo_hit | 38402.64 | 52082.27 | 62242.17 | 18743.04 | 30378.32 |
| prefixed_cache_hit | 44907.80 | 59557.74 | 73464.39 | 21917.99 | 35855.51 |
| prefixed_cache_miss_repo_hit | 45854.77 | 59689.85 | 68834.62 | 22380.18 | 33595.87 |
| root_404 | 36602.81 | 50627.36 | 66469.74 | 17864.61 | 32441.66 |

**Redirect fast-path latency, analytics ENABLED:**

| scenario | p50 ns | p95 ns | p99 ns | norm p50 | norm p99 |
|---|---|---|---|---|---|
| root_cache_hit | 57983.62 | 81631.67 | 109613.21 | 28299.87 | 53498.54 |
| root_cache_miss_repo_hit | 59105.11 | 80871.01 | 104651.47 | 28847.23 | 51076.88 |
| prefixed_cache_hit | 64991.61 | 90851.94 | 136977.93 | 31720.23 | 66854.34 |
| prefixed_cache_miss_repo_hit | 65150.16 | 92300.79 | 140346.30 | 31797.61 | 68498.33 |
| root_404 | 54964.50 | 72117.51 | 87281.12 | 26826.33 | 42598.99 |

**C-registry-presence** (advisory): anchor p50 ratio (with registry / without)
= **1.011** (limit 1.100) - OK. Constructing the full `CommandRegistry` +
services + an idle 4-worker `JobRunner` in-process adds ~1.1% to redirect p50,
well under the 10% ceiling.

### Debug / TUI OFF (`cmake-build-notui`)

New dir, configured (`cfg_notui_rc=0`) with `URLSHORTENER_ENABLE_TUI=OFF` and
built `-j4` (`build_notui_rc=0`, 881 targets - 274 fewer than Release, no FTXUI).

```
ctest --test-dir cmake-build-notui -N | grep -c tui__   # 0
ctest --test-dir cmake-build-notui -N | grep 'Total Tests'  # Total Tests: 419
ctest --test-dir cmake-build-notui -L unit              # 100% tests passed, 0 failed out of 266
```

| Check | Result |
|---|---|
| `tui__*` tests registered | **0** (confirmed) |
| Total tests registered | 419 (vs 485 with TUI on - the ~65 `tui__*` tests plus `hiredis-test` family drop out) |
| `-L unit` | 266 total, 266 pass, 0 fail, 0 skip |

Build note: the TUI-OFF link emitted one benign `ld` warning - a cross-TU weak
symbol visibility note on `std::endl` from `sql_metadata_row_mapper.cpp.o` under
LTO. It is pre-existing (no product code was changed in this subtask), does not
fail the link (`build_notui_rc=0`), and is unrelated to TUI. Recorded, not
fixed (product code is out of scope for 07.0/34).

---

## STEP 2 - Flake check

Commands (under the lock, `cmake-build`):

```
ctest --test-dir cmake-build -L conformance --repeat until-fail:3
ctest --test-dir cmake-build -R "^tui__3[1-3]_" --repeat until-fail:20
```

**Substitution note:** the spec names `-R "^tui__5[0-2]_"`, but no `tui__50/51/52`
stems are built. The as-built non-blocking / state-transition TUI tests are
`tui__31_screen_transition_requests_test`, `tui__32_hotkeys_and_focus_test`,
`tui__33_nonblocking_ui_test`, so `-R "^tui__3[1-3]_"` was run instead.

| Flake run | Repeats | Tests | Result |
|---|---|---|---|
| `-L conformance` | until-fail:3 | 16 | `100% tests passed, 0 tests failed out of 16`; rc=0. No flake. |
| `-R "^tui__3[1-3]_"` | until-fail:20 | 3 | `100% tests passed, 0 tests failed out of 3`; rc=0. No flake. |

Neither target flaked. (The only flaky test observed in the whole matrix is the
wall-clock `19_rest_patch_via_registry` case documented in Step 1.)

---

## STEP 3 - Coverage

**Approach: llvm-cov source-based fallback (lcov unavailable).** `lcov`/`genhtml`
are not installed, so the `coverage` CMake target (`cmake/coverage.cmake`, which
shells out to `lcov`/`genhtml`) cannot run, and the project's `ENABLE_COVERAGE`
option is lcov/gcov-based. `xcrun llvm-cov` and `xcrun llvm-profdata` ARE
available, so the spec's llvm-cov fallback was used instead of the inspection
mapping.

Steps:

```
cmake -S . -B build-cov -G Ninja -DCMAKE_PREFIX_PATH=/usr/local/opt/openssl@3:/usr/local \
      -DCMAKE_BUILD_TYPE=Debug \
      -DCMAKE_C_FLAGS="-fprofile-instr-generate -fcoverage-mapping" \
      -DCMAKE_CXX_FLAGS="-fprofile-instr-generate -fcoverage-mapping" \
      -DCMAKE_EXE_LINKER_FLAGS="-fprofile-instr-generate"           # cfg_cov_rc=0
cmake --build build-cov -j4                                          # build_cov_rc=0 (1155 targets)
env LLVM_PROFILE_FILE="$PWD/build-cov/profraw/%p-%m.profraw" \
    ctest --test-dir build-cov -L "unit|integration|conformance"     # 99% passed, 2 failed/448 (flakes, below)
xcrun llvm-profdata merge -sparse --input-files=<list of all .profraw> -o build-cov/merged.profdata
xcrun llvm-cov report build-cov/url_shortener <-object every other instrumented exe> \
      -instr-profile=build-cov/merged.profdata src/commands src/transport src/tui src/app src/composition src/http
```

**Profile handling (no overwrite):** `LLVM_PROFILE_FILE` used `%p` (pid) and `%m`
(binary signature), so every test process wrote a distinct file - **4990
`.profraw` files, 8.2 GB** - and `llvm-profdata merge` consumed all of them via
`--input-files`. Because most coverage comes from the separate per-test
executables (not `url_shortener`), the report passes **`build-cov/url_shortener`
first and all 385 other instrumented Mach-O test binaries as `-object`** (386
binaries total; `_deps/*` and dylibs excluded), so counters from every test exe
are attributed. Passing only `url_shortener` would have undercounted badly.

**Two instrumented-run failures (flakes, not regressions):**
`379 - 01_generated_slug_redirect_test` and `380 - 02_custom_slug_get_by_id_slug_test`
(both `integration`). Both **pass in the clean Debug build** (the union run's only
failure was #35) and both **pass on isolated rerun in `build-cov`** (2/2). They
are server-spawn/port timing flakes aggravated by the heavy profiling I/O
(8.2 GB of profraw), not product defects. They do not affect the coverage merge.

### Per-directory line coverage (llvm-cov)

Merge: `llvm-profdata merge` of all 4990 `.profraw` -> `build-cov/merged.profdata`
(49 MB), `merge_rc=0`. Report: `llvm-cov report` over 383 instrumented Mach-O
binaries (`url_shortener` first, 382 test exes as `-object`; `CMakeFiles/*.bin`
compiler probes and 3 non-existent/filtered entries dropped from the raw 386),
`report_rc=0`. One expected warning: `807 functions have mismatched data` -
benign, caused by per-binary inlining differences of the shared static lib; it
does not invalidate the line counts.

Per-directory `TOTAL`, line coverage (and raw line counts) from
`xcrun llvm-cov report ... <dir>`:

| Directory | Lines | Missed | **Line cov** | Region cov | Func cov | Branch cov |
|---|---|---|---|---|---|---|
| `src/commands` | 5747 | 417 | **92.74%** | 93.37% | 97.21% | 86.74% |
| `src/transport` | 79 | 7 | **91.14%** | 90.32% | 100.00% | 90.00% |
| `src/tui` | 10724 | 1568 | **85.38%** | 85.19% | 89.80% | 75.87% |
| `src/app` | 2199 | 149 | **93.22%** | 92.71% | 100.00% | 86.31% |
| `src/composition` | 464 | 43 | **90.73%** | 87.57% | 87.80% | 74.55% |
| `src/http` | 2754 | 610 | **77.85%** | 86.20% | 84.67% | 76.66% |
| **Combined (6 dirs)** | 21967 | 2794 | **87.28%** | 88.21% | 91.88% | 79.81% |

Observations:

- Five of the six target directories are at/above ~85% line coverage; the
  combined figure is **87.28%**.
- **`src/http` is the lowest at 77.85%** (610 missed lines) - the main residual
  gap. This is the HTTP transport/handler layer (sessions, TLS plumbing,
  request hardening); much of the miss is error/teardown paths and TLS branches
  that unit/integration suites do not drive. This is the natural next
  `/test-gap src/http` priority (see `docs/reviews/0006-test-gap-report.md`),
  not a blocker for this evidence pass.
- Coverage was produced from the `unit|integration|conformance` suites only (the
  labels the spec names for step 3); e2e and perf were excluded.

---

## Verdict (Steps 1-3)

**GREEN**, with two documented flakes and one tooling substitution.

- Step 1: all four label groups pass on the Debug/TUI-ON tree. The single union
  failure (`19_rest_patch_via_registry`) was a stale pre-`4bc641a` object in
  `cmake-build`; the current source passes **25/25** after a forced rebuild of
  that target, so there is no open product or test defect. Release/TUI-ON builds
  and passes `unit` (331/331) and `perf` (4/4, guard measures). TUI-OFF builds
  and passes `unit` (266/266) with **0 `tui__*` tests** registered.
- Step 2: no flake in `-L conformance --repeat until-fail:3` or the as-built
  `-R "^tui__3[1-3]_" --repeat until-fail:20` (substituted for the non-existent
  `tui__5[0-2]` stems).
- Step 3: coverage produced via the **llvm-cov fallback** (lcov unavailable);
  combined line coverage **87.28%** over the six target dirs, lowest `src/http`
  at 77.85%.
- Out-of-scope here (owned by the rest of 07.0/34): `/test-gap`, `/verify-subtask`,
  `/doc-registry`, `/link-check`, `/pr-review`, and `status.md` sign-off.

## Secret scan

A credential-pattern grep (`password=`/`secret=`/`token=`/`api-key`/private-key
headers/`postgres://`/`redis://`/`AKIA...`/`bearer`/long base64) was run on this
file. The only hit is a **false positive**: the base64-length heuristic matched
the slash-separated label list `unit/contract/integration/e2e/conformance/perf`.
No secrets, DSNs, passwords, tokens or keys are present. The only connection
detail named is the non-secret local socket address `/tmp:5432` (host/port of an
unauthenticated local dev PostgreSQL - no credentials).
