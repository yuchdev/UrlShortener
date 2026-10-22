# 32 - Redirect fast-path timing guard

**Parent task:** 07.0 Tests and conformance
**State:** ⬜ Not started
**Depends on:** 30; best run after Task 02.0 (storage unification touches the redirect path) and Task 04.0 (resource routes)
**Blocks:** 33, 34
**Estimated size:** M
**Suggested agent:** testing-expert

## Objective

Detect a regression in redirect latency caused by this milestone: measure
in-process `GET /{slug}` and `GET /r/{slug}` dispatch (cache-hit and
cache-miss + repository hit) through the real application router, compare with
a checked-in baseline normalized by a CPU calibration loop, and prove that
the mere presence of the command registry, job runner and TUI-free local
services does not slow redirects. Advisory by default, a gate on demand.

## Dependencies

- Subtask 30 (perf label); `docs/benchmarking.md` methodology (warm-up, identical dataset, median of >= 3 runs, report variance); existing external tool `scripts/benchmark_redirect.py` (wrk/hey based, not CI).
- In-process drive pattern from `tests/unit/http/13_root_redirect_handler.cpp`: `RouterBuilder::buildApplicationRouter()`, `router.dispatch(req, config, false)`, `url_shortener::linkRepository()`, `url_shortener::linkCache()`; after 02.0 use whatever shared-backend accessor 02.0 defines (**UNVERIFIED**).

## Files affected

| Path | create/modify | What changes |
|---|---|---|
| `tests/perf/redirect/redirect_fast_path_guard.cpp` | create | benchmark + guard (plain `main`, no Boost.Test) |
| `tests/perf/redirect/baseline.json` | create | committed baseline (format below) |
| `tests/perf/README.md` | modify | baseline update procedure |
| `sources.cmake` | modify | Append file to `PERF_SOURCES` |
| `CMakeLists.txt` | modify | `target_compile_definitions(redirect__redirect_fast_path_guard PRIVATE PERF_BASELINE_PATH="${CMAKE_CURRENT_SOURCE_DIR}/tests/perf/redirect/baseline.json")` |

## Classes and functions

```cpp
namespace {
struct Sample { double p50_ns, p95_ns, p99_ns; std::size_t n; };
double calibrationNs();                         // fixed integer/hash loop, median of 5 runs: normalizes machine speed
Sample measure(const std::function<void()>& op, std::size_t warmup, std::size_t iterations);
Sample measureRedirect(std::string_view path_kind /* "root" | "prefixed" */, bool cache_hit);
struct Baseline { double calibration_ns; std::map<std::string, Sample> scenarios; };
Baseline loadBaseline(const char* path);        // tiny hand parser (no JSON library) or sscanf format
int main();                                     // prints a table; exit 0 unless gate enabled and a check fails
}
```

Scenarios: `root_cache_hit` (`GET /{slug}`), `root_cache_miss_repo_hit`
(cache erased each iteration), `prefixed_cache_hit` (`GET /r/{slug}`),
`prefixed_cache_miss_repo_hit`, plus `root_404` (missing slug). Dataset: 10 000
links pre-created, requests cycle through 1 000 distinct slugs to avoid a
single hot entry; analytics disabled (as in baseline) and a second pass with it
enabled to track the documented enabled-vs-disabled comparison.

Checks (computed on **normalized** numbers `p99_ns / calibration_ns`):

| Check | Rule | Default |
|---|---|---|
| C-baseline | normalized p50 and p99 are within +25 % / +40 % of baseline | advisory |
| C-registry-presence | measure scenario A (no registry constructed), then construct a full `CommandRegistry` + services + idle `JobRunner` (4 workers) and measure scenario A again in the same process: p50 ratio <= 1.10 | advisory (gate when `URLSHORTENER_PERF_GATE=1`) |
| C-no-regression-vs-self | three repetitions; use median of medians; report relative spread (stddev/median) and mark the run `UNRELIABLE` (exit 0) if spread > 15 % | always |

`baseline.json` shape (written by `--write-baseline`, never by CI):

```json
{ "recorded": "2026-10-19", "compiler": "gcc 13.2 -O2", "cpu": "<model>", "calibration_ns": 1234.5,
  "scenarios": { "root_cache_hit": {"p50_ns": 0, "p95_ns": 0, "p99_ns": 0, "n": 200000} } }
```

(The committed baseline must be recorded on the CI runner class used by the nightly workflow, in Release; numbers shown are placeholders - the first real baseline is produced when this subtask is implemented.)

## Implementation steps

1. Implement measurement with `std::chrono::steady_clock`, batch timing (time 64 requests per sample to beat clock overhead), 20 000 warm-up iterations, 200 000 measured.
2. Pre-build `BeastRequest` objects once per slug; measure only `dispatch`.
3. Implement checks; print markdown table (`| scenario | p50 ns | p95 | p99 | normalized | baseline | delta |`) suitable for the PR description and `GITHUB_STEP_SUMMARY`.
4. `--write-baseline` flag; refuses to run in a Debug build (`NDEBUG` check) and on `CI=true`.
5. Before 0006 code lands, record the baseline on master to give the milestone a true "before".

## Commands

```bash
cmake -S . -B cmake-build-rel -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build cmake-build-rel --target redirect__redirect_fast_path_guard
ctest --test-dir cmake-build-rel -L perf -R redirect --output-on-failure
URLSHORTENER_PERF_GATE=1 ctest --test-dir cmake-build-rel -R redirect__redirect_fast_path_guard --output-on-failure
cmake-build-rel/redirect__redirect_fast_path_guard --write-baseline       # human-run only
# Windows multi-config: cmake --build cmake-build --config Release ; ctest -C Release ...
```

## Tests

| Test file | Target | CTest label | Cases |
|---|---|---|---|
| `tests/perf/redirect/redirect_fast_path_guard.cpp` | `redirect__redirect_fast_path_guard` | `perf` | 5 scenarios x 2 analytics modes; C-registry-presence; UNRELIABLE detection |

## Constraints

- Advisory by default (docs/testing/README.md 3.5): exits 0 on regressions unless `URLSHORTENER_PERF_GATE=1`; the structural guard (31) is the hard gate.
- Release builds only; Debug prints "skipped: not an optimized build" and exits 0.
- RUN_SERIAL (subtask 30); no network; no other process load assumed.
- No third-party benchmark library (Google Benchmark would be a new dependency).

## Success criteria

1. Running the guard on a quiet machine twice yields spread < 15 % and all checks pass.
2. Injecting a 20 us busy-wait into `handleRootRedirect` makes C-baseline fail under `URLSHORTENER_PERF_GATE=1` (verified once by hand and recorded).
3. A CommandRegistry constructed in-process leaves the redirect p50 within 10 %.
4. The nightly workflow summary shows the table (wired in subtask 33).

## Out of scope / risks

- End-to-end load tests with `wrk`/`hey` (see `docs/benchmarking.md`); remain manual.
- Risk: noisy shared CI runners; mitigation: normalization, spread check, advisory default; a persistent trend store is out of scope.
