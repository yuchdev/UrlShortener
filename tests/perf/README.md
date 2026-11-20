# Performance benchmarks (`perf` label)

Advisory latency and throughput benchmarks for the URL shortener. These tests
are registered with the CTest label **`perf`** and are deliberately kept out of
the default developer run and the unit gate: they are *advisory* unless a
workflow explicitly promotes a specific check to a gate
(`docs/testing/README.md` section 3.5).

## Layout

```
tests/perf/
├── analytics/   # analytics pipeline benchmarks (placeholders today)
└── redirect/    # redirect fast-path timing guard (subtask 32)
    ├── redirect_fast_path_guard.cpp   # benchmark + advisory checks (plain main)
    └── baseline.json                  # committed baseline, Release-only
```

Each `.cpp` is a standalone program that defines its own `main()` and prints a
one-line summary. They are listed in `PERF_SOURCES` in `sources.cmake` and
registered in `CMakeLists.txt` via `register_labeled_cpp_tests(PERF_SOURCES
"perf")`. Target names follow the repo-wide `<parent_dir>__<stem>` rule, e.g.
`analytics__worker_batch_insert_throughput`.

> The analytics benchmarks under `analytics/` are **placeholders**: they print a
> line and exit 0. Turning them into real measurements is owned by the analytics
> milestone, not milestone 0006. Do not add real benchmark logic here.

## How to run

Always point `--test-dir` at the build directory and never reconfigure an
existing build tree.

```bash
# Build the perf targets (part of a normal build).
cmake --build cmake-build

# List the registered perf tests without running them.
ctest --test-dir cmake-build -N -L perf

# Run only the perf suite.
ctest --test-dir cmake-build -L perf --output-on-failure
```

On Windows / multi-config generators add `-C Release` to every `cmake --build`
and `ctest` invocation: performance must be measured in a Release build.

The perf label is **excluded** from the documented union
`-L "unit|contract|integration|e2e"`, so routine runs stay fast. Select it
explicitly with `-L perf`.

## Measurement properties

- **`RUN_SERIAL TRUE`** — perf tests never run concurrently with other tests, so
  a parallel `ctest -j` cannot skew timing.
- **`TIMEOUT 300`** — a hung benchmark fails instead of blocking the suite.

## Methodology (`docs/benchmarking.md`)

When a placeholder is promoted to a real benchmark, follow the project
methodology: a warm-up phase, at least 3 measured runs, report the median,
use an identical dataset across runs, and keep the measurement non-blocking.

## Environment variables

The current placeholders need none. Real benchmarks that spawn the server or
drive storage should reuse the existing env-var conventions
(`SIMPLE_HTTP_BIN`, `SHORTENER_BASE_DOMAIN`, `URL_SHORTENER_ANALYTICS_HASH_SALT`,
etc.) rather than inventing new ones, and must never log secrets or the
analytics salt.

## Advisory vs. gate

Perf runs are advisory by default. A nightly workflow
(`.github/workflows/perf-analytics.yml`) runs the suite but does not fail the
build on regressions. Promotion of any specific benchmark to a hard gate is a
workflow decision owned by the CI-wiring subtask (07.0/33), not by the
benchmark files themselves.

## Baseline update procedure

1. Run the suite three times in a Release build on a quiet machine
   (`ctest --test-dir cmake-build -L perf --output-on-failure`).
2. Take the median of the reported numbers.
3. Record the new baseline alongside the benchmark (and in
   `docs/benchmarking.md` when a shared baseline table exists), with the machine
   class and build type noted — a baseline is only comparable against the same
   hardware and Release settings.
4. Never relax a threshold to make a failing run pass without an explicit,
   reviewed justification.

### Redirect fast-path guard (`redirect/baseline.json`)

The redirect guard (`redirect__redirect_fast_path_guard`) compares the measured,
calibration-normalised redirect latency against `redirect/baseline.json`. The
guard finds that file through the `PERF_BASELINE_PATH` compile definition set in
`CMakeLists.txt`; there is no runtime path to configure.

- **Debug builds skip.** A build without `NDEBUG` prints
  `skipped: not an optimized build` and exits 0 — the numbers would not be
  comparable to the Release baseline. Measure and record **only** in a Release
  build.
- **Checks and thresholds.** Everything is computed on normalised numbers
  (`measured_ns / calibration_ns`):
  - *C-baseline* (advisory; a gate under `URLSHORTENER_PERF_GATE=1`): normalised
    p50 within **+25 %** and p99 within **+40 %** of the baseline.
  - *C-registry-presence* (advisory; gated likewise): constructing a full
    `CommandRegistry` + documented services + an idle 4-worker `JobRunner`
    in-process keeps the redirect p50 ratio **≤ 1.10**.
  - *C-no-regression-vs-self* (always on): three repetitions; a relative spread
    (stddev/median) **> 15 %** marks the run `UNRELIABLE` and suppresses the
    baseline/registry checks (exit 0) — a noisy runner, not a regression.
- **Writing a new baseline (human only, never CI).** On a quiet Release machine:

  ```bash
  cmake-build-rel/redirect__redirect_fast_path_guard --write-baseline
  ```

  It refuses to write when `CI=true`. Commit the regenerated
  `redirect/baseline.json` with the compiler, CPU model and build type noted in
  the file's `compiler`/`cpu` fields. The committed baseline must be recorded on
  the nightly workflow's runner class so the normalised comparison stays valid.
- The baseline checked in with subtask 32 is a **placeholder** (zeroed samples,
  `calibration_ns = 1.0`): a zeroed scenario disables its own comparison, so the
  guard stays advisory until a real baseline is recorded on the CI runner.
