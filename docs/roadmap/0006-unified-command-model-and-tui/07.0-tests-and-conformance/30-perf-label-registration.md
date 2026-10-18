# 30 - Perf label registration

**Parent task:** 07.0 Tests and conformance
**State:** ⬜ Not started
**Depends on:** none (can land first)
**Blocks:** 32, 33
**Estimated size:** S
**Suggested agent:** cpp-expert (CMake only) with testing-expert review

## Objective

Make the `perf` CTest label real. Today `tests/perf/analytics/*.cpp` are
placeholders that print one line, are **not registered** in `CMakeLists.txt` or
`sources.cmake` (no `perf` string in either), and the nightly workflow
`.github/workflows/perf-analytics.yml` runs `ctest -L perf || true`, which
matches zero tests. This subtask registers them so subtask 32 has a home.

## Dependencies

- `docs/testing/README.md` section 3.5 (label `perf`, location `tests/perf/`, advisory), `docs/benchmarking.md` (methodology: warm-up, >= 3 runs, median, identical dataset, non-blocking), `docs/agent/ci-pipeline-specification.md` section 4.1.
- Existing files: `tests/perf/analytics/queue_overflow_redirect_tail_latency.cpp`, `redirect_analytics_enabled_vs_disabled.cpp`, `worker_batch_insert_throughput.cpp`.

## Files affected

| Path | create/modify | What changes |
|---|---|---|
| `sources.cmake` | modify | New `PERF_SOURCES` list with the three existing files (subtask 32 appends its file) |
| `CMakeLists.txt` | modify | Inside `if(BUILD_TESTING)`: `register_labeled_cpp_tests(PERF_SOURCES "perf")` (targets `<parent_dir>__<stem>`, links `url_shortener_common` and `Boost::unit_test_framework`) followed by the loop below that sets `TIMEOUT 300` and `RUN_SERIAL TRUE` so timing is not disturbed by parallel tests. One naming scheme only (07.0 README table); no `perf__` prefix |
| `tests/perf/README.md` | create | How to run, env vars, advisory vs gate, baseline update procedure |

Registration uses `register_labeled_cpp_tests(PERF_SOURCES "perf")`, so targets follow the `<parent_dir>__<stem>` rule: `analytics__queue_overflow_redirect_tail_latency`, `analytics__redirect_analytics_enabled_vs_disabled`, `analytics__worker_batch_insert_throughput`, and (subtask 32) `redirect__redirect_fast_path_guard`. Note the macro also adds `tests/contract/metadata` to the include path, which is harmless.

## Classes and functions

None in C++. CMake fragment (shape):

```cmake
register_labeled_cpp_tests(PERF_SOURCES "perf")
foreach(perf_src IN LISTS PERF_SOURCES)
  get_filename_component(perf_stem ${perf_src} NAME_WE)
  get_filename_component(perf_dir  ${perf_src} DIRECTORY)
  get_filename_component(perf_parent ${perf_dir} NAME)
  set_tests_properties(${perf_parent}__${perf_stem} PROPERTIES RUN_SERIAL TRUE TIMEOUT 300)
endforeach()
```

## Implementation steps

1. Add `PERF_SOURCES` and the macro call; build; confirm `ctest -N -L perf` lists the three tests.
2. Check each placeholder returns 0 and uses no `BOOST_TEST_MODULE` (they define `main()`); the macro links `Boost::unit_test_framework` which may define its own `main` when static - verify link works; if conflicts arise, link without Boost for perf targets through a dedicated loop that keeps the same `<parent_dir>__<stem>` target names (so the 07.0 README table, subtask 32 and CI filters stay valid), and record why.
3. Update the nightly workflow comment only if needed (subtask 33 owns workflow edits).
4. Write `tests/perf/README.md`.

## Commands

```bash
cmake -S . -B cmake-build -G Ninja
cmake --build cmake-build
ctest --test-dir cmake-build -N -L perf          # lists 3 tests
ctest --test-dir cmake-build -L perf --output-on-failure
# Windows: add -C Release (perf must be measured in Release)
```

## Tests

| Test file | Target | CTest label | Cases |
|---|---|---|---|
| existing 3 perf placeholders | `analytics__*` | `perf` | print and exit 0 (unchanged) |

## Constraints

- Perf tests excluded from the documented union `-L "unit|contract|integration|e2e"` (label `perf` only); default developer runs stay fast.
- `RUN_SERIAL` so parallel `ctest -j` does not skew timing.
- Do not turn placeholders into real benchmarks here.

## Success criteria

1. `ctest -N -L perf` lists exactly the registered perf tests (3 now, 4 after subtask 32).
2. Nightly workflow's `-L perf` run now executes something.
3. No existing test changes name or label.

## Out of scope / risks

- Making analytics perf tests meaningful: owned by the analytics milestone, not 0006.
- Risk: linking Boost.Test with plain `main()` (step 2).
