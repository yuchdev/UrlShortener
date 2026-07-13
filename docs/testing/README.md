# Testing Reference

A single, authoritative overview of how the UrlShortener project is tested: the
test types, the directory and CMake/CTest architecture, and how to build and run
every suite — individually or all at once.

> Companion documents:
> - [`docs/agent/testing-requirements.md`](../agent/testing-requirements.md) — the
>   normative quality bar (coverage targets, mandatory scenarios, review rules).
> - [`docs/testing/sqlite_state_assert.md`](./sqlite_state_assert.md) — the
>   `tools/sqlite_state_assert.py` helper used by integration/e2e assertions.
> - [`docs/testing/e2e_testing/AGENTIC_QA.md`](./e2e_testing/AGENTIC_QA.md) — the
>   end-to-end scenario catalogue.

---

## 1. Test taxonomy

The suite is organised into five categories. Four of them are registered as
CTest tests and carry a CTest **label**; the fifth (perf) is a set of standalone
micro-benchmarks.

| Category | CTest label | Framework | What it exercises |
|---|---|---|---|
| **Unit** | `unit` | Boost.Test (C++) | Pure, isolated logic with no network/disk: URL validation & normalization, slug rules, lifecycle/expiry precedence, redirect decisions, error mapping, cache-record conversion, config parsing, security primitives (hashing, tokens, access guard), analytics builders/sanitizers. |
| **Mock / Contract** | `contract` | Boost.Test (C++) | Backend-agnostic behavioural suite in `tests/contract/metadata/`. Written once against the `IMetadataRepository` **interface** (driven by fakes/real adapters) and reused across in-memory, SQLite, and PostgreSQL so every backend is held to identical behaviour. |
| **Integration** | `integration` | Boost.Test (C++) **and** Python | Real components wired together (fakes or a temp SQLite/live binary): full resolve flow, cache-aside + invalidation, analytics best-effort pipeline, backend-swap-by-config, and HTTP-level Python tests that drive the built `url_shortener` binary. |
| **E2E** | `e2e` | Bash + Python mock service | Black-box scenario tests under `tests/e2e/scripts/`: server boot, create, redirect, expiration, fingerprinting, auth, permissions, admin API, error handling, concurrency. Assert final SQLite/log state via `tools/sqlite_state_assert.py`. |
| **Perf** | *(benchmarks)* | Boost.Test (C++) | Micro-benchmarks in `tests/perf/analytics/` (redirect tail latency under queue saturation, enabled-vs-disabled overhead, worker batch throughput). Run best-effort in CI; not a merge gate. |

> **"Mock" == `contract`.** The backend-agnostic contract tests are the
> project's mock/fake-driven layer: they specify behaviour independent of any
> concrete storage engine.

---

## 2. Directory layout

```
tests/
├── unit/                     # label: unit — Boost.Test, one behaviour per case
│   ├── core/                 #   validation, slugs, expiry, redirect status
│   ├── link_management/      #   status precedence, tags, campaigns, reserved slugs
│   ├── storage/              #   inmemory, sql, sqlite, psql, redis, ratelimiter, memory
│   ├── analytics/            #   click model, sanitizer, queue, worker, retention
│   ├── http/analytics/       #   redirect hook, stats handler
│   ├── config/               #   YAML storage-config parsing
│   ├── observability/        #   metric labels, log redaction
│   ├── security/             #   control set, access guard, hashing, tokens, auth broker
│   └── docs/                 #   config examples match parser
│
├── contract/
│   └── metadata/             # label: contract — backend-agnostic suite (the "mock" layer)
│
├── integration/
│   ├── core/                 # label: integration — Python HTTP flows (redirect, slug, expiry)
│   ├── link_management/      #   Python management-plane flows
│   ├── cpp/                  #   C++ integration (analytics, storage roundtrips)
│   ├── storage/              #   C++ resolve/cache-aside/invalidation, backend swap
│   ├── config/               #   C++ config-driven wiring
│   ├── sqlite/ psql/ redis/  #   backend integration + contract-suite runners
│   ├── build/ ci/            #   Python build-matrix / CI job runners
│   └── py/                   #   Python analytics HTTP tests
│
├── e2e/                      # label: e2e — shell scenarios (Linux/CI)
│   ├── scripts/              #   run_all_sections.sh, run_section.sh, common.sh, mock_service.py
│   │   └── sections/         #   01_..10_ section drivers + _run_section.sh
│   ├── expected/             #   per-section expected SQLite state (*.json)
│   ├── failures/             #   captured artifacts on failure (git-ignored)
│   └── tmp/                  #   runtime scratch: qa.sqlite, server.log (git-ignored)
│
└── perf/
    └── analytics/            # standalone micro-benchmarks
```

Supporting tooling:

- `tools/sqlite_state_assert.py` — CLI to assert SQLite rows, open/closed ports,
  running processes, and log contents. Backbone of integration/e2e assertions.
- `test/cli_parser_test/`, `test/http_client_test/` — legacy standalone targets
  still registered (labels `unit` and `integration` respectively).

---

## 3. Test architecture & design conventions

**Frameworks.** C++ tests use the **Boost.Test** framework and link against the
`url_shortener_common` static library. Python tests use only the standard library
(`http.server`/`http.client`, `sqlite3`). E2E scenarios are Bash scripts that
start a Python mock service and assert final state.

**Determinism — inject the clock.** Never sleep on the wall clock for
timing-dependent behaviour. Use `core/clock.hpp`:

```cpp
ManualClock clock{std::chrono::system_clock::now()};
clock.advance(std::chrono::seconds(3600));   // deterministically age state
```

Pass `ManualClock` into `LinkService` and storage adapters in tests;
`SystemClock` is production-only.

**Isolate externals with hand-written fakes.** In unit scope, implement the
port interfaces (`IMetadataRepository`, `ICacheStore`, `IAnalyticsSink`,
`IRateLimiter`, `IClickEventRepository`) with in-process fakes — never a real
Postgres/Redis. Real backends appear only in the `integration` layer.

**Contract reuse (the mock layer).** `tests/contract/metadata/` is compiled once
and run directly (label `contract`) and again against SQLite/PostgreSQL through
`tests/integration/sqlite/03_contract_suite_runner.cpp` and
`tests/integration/psql/05_contract_suite_runner.cpp`. Add a behaviour here to
hold **every** backend to it.

**Naming.** Numeric-prefixed filenames (`01_...`, `02_...`), one behaviour per
`BOOST_AUTO_TEST_CASE`, precise assertions on `ResolveStatus`, `LinkStatus`,
`RepoError`, HTTP status, and record fields — not "does not throw".

---

## 4. CMake / CTest registration

All test wiring lives in the top-level `CMakeLists.txt` inside the
`if(BUILD_TESTING)` block. There are four registration mechanisms:

| Sources | Target name | Label | Mechanism |
|---|---|---|---|
| `CORE_BASIC_UNIT_SOURCES`, `LINK_MANAGEMENT_UNIT_SOURCES` | **filename stem** (e.g. `01_url_validation_accept_test`) | `unit` | direct `foreach` + `add_executable` |
| All other C++ suites | **`<parent_dir>__<stem>`** (e.g. `inmemory__01_create_and_get_roundtrip`) | `unit` / `contract` / `integration` | `register_labeled_cpp_tests(<list> <label>)` |
| Python integration scripts | **filename stem** | `integration` | `add_test` running `${Python3_EXECUTABLE}`, `ENVIRONMENT SIMPLE_HTTP_BIN=$<TARGET_FILE:url_shortener>`, `TIMEOUT 180` |
| E2E sections | **`e2e_<section>`** (e.g. `e2e_03_redirects`) | `e2e` | `add_test` running `bash run_section.sh <section>`, `RUN_SERIAL`, `TIMEOUT 120` |

**E2E gating.** E2E registration is guarded so it only activates where it can
run — non-Windows hosts with a `bash` interpreter — and can be disabled
explicitly:

```cmake
option(URLSHORTENER_ENABLE_E2E_TESTS "Register shell-based e2e scenario tests with CTest" ON)
# registers only when: URLSHORTENER_ENABLE_E2E_TESTS AND NOT CMAKE_HOST_WIN32 AND find_program(bash)
```

Each e2e section is `RUN_SERIAL` because the sections share a fixed service port
(`18080`) and `tests/e2e/tmp` — CTest will not run them concurrently with any
other test.

> **Perf tests** are compiled sources under `tests/perf/` but are **not**
> currently registered as CTest tests; the `perf-analytics.yml` workflow invokes
> `ctest -L perf` best-effort (`|| true`).

---

## 5. Building the tests

### Windows (CLion / Visual Studio — this workspace)

Requires vcpkg (set `VCPKG_ROOT` or pass `-DCMAKE_TOOLCHAIN_FILE`). Use the
existing CLion build directory and always pass `-C Debug` (multi-config
generator):

```powershell
# Build the whole test tree via the shared library + individual targets
cmake --build "C:\Users\atatat\Projects\UrlShortener\cmake-build-debug-visual-studio" --config Debug

# Or build a single target before running it
cmake --build "C:\Users\atatat\Projects\UrlShortener\cmake-build-debug-visual-studio" --target inmemory__01_create_and_get_roundtrip --config Debug
```

### Linux / macOS (generic, as CI does)

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

---

## 6. Running the tests

Always point `--test-dir` at the build directory. On Windows add `-C Debug`.
Examples below use the CLion build dir; substitute `build` on Linux.

```powershell
# One test by exact name
ctest --test-dir "...\cmake-build-debug-visual-studio" -C Debug -R "^08_redirect_status_test$" --output-on-failure

# One category by label
ctest --test-dir "...\cmake-build-debug-visual-studio" -C Debug -L unit        --output-on-failure
ctest --test-dir "...\cmake-build-debug-visual-studio" -C Debug -L contract    --output-on-failure
ctest --test-dir "...\cmake-build-debug-visual-studio" -C Debug -L integration --output-on-failure
ctest --test-dir "...\cmake-build-debug-visual-studio" -C Debug -L e2e         --output-on-failure
```

### Run every category in a single command

Use a label-union regex to run unit + mock/contract + integration + e2e at once:

```powershell
ctest --test-dir "...\cmake-build-debug-visual-studio" -C Debug -L "unit|contract|integration|e2e" --output-on-failure
```

```bash
# Linux equivalent (single-config generator; no -C needed)
ctest --test-dir build -L "unit|contract|integration|e2e" --output-on-failure
```

A plain `ctest --test-dir build -C Debug --output-on-failure` (no `-L`) runs all
registered tests too — the label union is the explicit, self-documenting form.

Helpful options: `-j <N>` (parallel; `RUN_SERIAL` e2e tests still serialize),
`--output-on-failure`, `-N` (dry-run list), `--print-labels`, `--rerun-failed`,
`-VV` (verbose).

### Running e2e directly (without CTest)

The e2e sections can also be driven by their own runner (what `e2e-tests.yml`
uses). Run from the repository root:

```bash
bash tests/e2e/scripts/run_all_sections.sh                       # all sections
QA_SECTIONS="01_server_boot,03_redirects" \
    bash tests/e2e/scripts/run_all_sections.sh                   # a subset
bash tests/e2e/scripts/run_section.sh 03_redirects               # one section
```

E2E requires a POSIX shell, `python3`, and Linux runtime facilities (`/proc`,
`os.kill`); it runs on the Ubuntu CI runner, not on native Windows.

---

## 7. Continuous integration

Each category maps to a GitHub Actions workflow:

| Workflow | Trigger | Command |
|---|---|---|
| `ubuntu-unit-tests.yml` | push / PR | build, then `ctest -L "unit"` |
| `ubuntu-integration-tests.yml` | push / PR | build, then `ctest -L "integration"` |
| `e2e-tests.yml` | PR (paths) / manual dispatch | pytest for `sqlite_state_assert`, then `tests/e2e/scripts/run_all_sections.sh` |
| `perf-analytics.yml` | (perf paths) | `ctest -L "perf"` (best-effort) |

Ubuntu jobs install system dependencies via `scripts/setup_ubuntu_dependencies.sh`
and configure with Ninja/Release. Because CI jobs select by `-L`, adding the
`e2e` label to CTest does not disturb the existing unit/integration jobs.

---

## 8. Adding a new test

1. **Pick the category** and place the file under the matching directory using a
   numeric prefix consistent with its siblings (`tests/unit/...`,
   `tests/contract/metadata/...`, `tests/integration/...`, or a new
   `tests/e2e/scripts/sections/NN_*.sh`).
2. **Register it** in `CMakeLists.txt`:
   - C++: append the path to the correct source list (`*_UNIT_SOURCES`,
     `CONTRACT_METADATA_SOURCES`, `*_INTEGRATION_SOURCES`). New `.cpp` files that
     become part of `url_shortener_common` must also be added to `sources.cmake`.
   - Python integration: append to the relevant integration source list.
   - E2E: add the section name to the `E2E_SECTIONS` list and provide the
     matching `expected/NN_*.json`.
3. **Backend behaviour?** Add it to `tests/contract/metadata/` so all backends
   inherit it, rather than to a single backend suite.
4. **Build the single target, then run by exact name**:
   ```powershell
   cmake --build "...\cmake-build-debug-visual-studio" --target <target> --config Debug
   ctest --test-dir "...\cmake-build-debug-visual-studio" -C Debug -R "^<test_name>$" --output-on-failure
   ```
5. **Verify the category still passes** with `-L <label>`, and confirm coverage
   against [`docs/agent/testing-requirements.md`](../agent/testing-requirements.md).

---

## 9. Quick reference

| I want to… | Command (add `-C Debug` on Windows) |
|---|---|
| Run everything | `ctest --test-dir build -L "unit\|contract\|integration\|e2e" --output-on-failure` |
| Run unit only | `ctest --test-dir build -L unit --output-on-failure` |
| Run the mock/contract suite | `ctest --test-dir build -L contract --output-on-failure` |
| Run integration only | `ctest --test-dir build -L integration --output-on-failure` |
| Run e2e only | `ctest --test-dir build -L e2e --output-on-failure` |
| Run one test | `ctest --test-dir build -R "^<name>$" --output-on-failure` |
| List without running | `ctest --test-dir build -N` |
| Show labels | `ctest --test-dir build --print-labels` |
| Re-run only failures | `ctest --test-dir build --rerun-failed --output-on-failure` |
