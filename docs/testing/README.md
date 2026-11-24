# UrlShortener Test Plan

## 1. Purpose

This document is the project-level test plan for UrlShortener. It defines the
test categories, ownership, execution commands, acceptance criteria, and the
traceability between manual scenarios and executable end-to-end analogs under
`tests/e2e`.

The application exposes one shared command registry and four transport modes:

- **Server mode**: the long-running HTTP/HTTPS service exposes REST endpoints
  registered through the route registry and dispatched by the HTTP router.
- **`cmd` mode**: one-shot `cmd <command> [--arg key=value ...]` executes a
  single command through the registry and exits.
- **`serve-stdio` mode**: NDJSON line-per-request session over stdin/stdout;
  no HTTP listener.
- **`tui` mode**: interactive terminal UI over `InProcessClient`; screens build
  `CommandRequest`s and render `CommandResponse`s through `ICommandClient`.

The test strategy verifies all four modes and uses the **command conformance
corpus** to prove that the shared command registry produces identical results
across every transport.

## 2. Quality objectives

| Objective | Required evidence |
|---|---|
| Preserve REST API behavior | HTTP unit characterization tests and integration flows keep status codes, JSON shapes, headers, route labels, redirects, and error mappings stable. |
| Validate CLI one-shot behavior | CLI integration and e2e tests spawn the binary, assert stdout/stderr/exit code, verify persisted state, and prove no server socket remains open. |
| Protect untrusted input boundaries | Unit, integration, and e2e tests cover target URL normalization, SSRF/private-host rejection, slug validation, oversized request protection, and malformed command/API inputs. |
| Keep storage backends interchangeable | Contract tests define metadata repository behavior once and run it against all supported adapters. |
| Keep analytics best-effort | Unit and integration tests verify analytics enqueue, aggregation, retention, disabled mode, and failure isolation without affecting redirects. |
| Maintain operational confidence | E2E scenarios cover startup, health, redirects, admin/auth flows, error handling, concurrency, and CLI/server separation. |
| Keep transports identical | The command conformance corpus is replayed through six transport shims (direct, tui, cmd, stdio, rest\_generic, rest\_resource); lint, coverage, catalog-drift, and authz-matrix gates prevent divergence. |
| Protect the redirect fast path | A structural isolation guard (`commands__80`) is a unit gate; an advisory timing guard (`redirect__redirect_fast_path_guard`) measures on Release and enforces a C-registry-presence ceiling. |
| Keep TUI headless-testable | Headless render harness, mock `ICommandClient`, and conformance TUI shim prove every screen, flow, and rule without a real terminal. |

## 3. Test categories

### 3.1 Unit tests

| Field | Plan |
|---|---|
| CTest label | `unit` |
| Location | `tests/unit/` |
| Framework | Boost.Test |
| Scope | Pure logic and narrow component behavior with no external services. |
| Examples | URL validation, slug rules, app command service, HTTP router matching, handlers with fakes, config parsing, analytics builders, security primitives, log redaction, command registry and pipeline, TUI screen models and goldens, conformance corpus lint and matcher vectors. |
| Acceptance | Deterministic, isolated, one behavior per test case. Use fakes for repository/cache/analytics ports. |

Unit tests are the required first layer for new behavior. For the shared
application command layer, unit tests should exercise the app service directly
with fake `ILinkStore` and `ILinkStatsReader` implementations rather than going
through Beast requests or process spawning. For the command registry, tests
live under `tests/unit/commands/` and use fake registry descriptors. TUI tests
live under `tests/unit/tui/` and render into headless `Screen` buffers, never
requiring a real terminal.

### 3.2 Mock / contract tests

| Field | Plan |
|---|---|
| CTest label | `contract` |
| Location | `tests/contract/metadata/` |
| Framework | Boost.Test |
| Scope | Backend-agnostic storage behavior. |
| Examples | create/get roundtrip, duplicate short code, update/delete, list filtering, expiry semantics, concurrency. |
| Acceptance | Every backend must satisfy the same behavior. New storage behavior belongs here first. |

The project uses the `contract` label for the mock/fake-driven backend behavior
suite. SQLite and PostgreSQL integration runners also execute these contracts
against concrete adapters.

### 3.3 Integration tests

| Field | Plan |
|---|---|
| CTest label | `integration` |
| Location | `tests/integration/` |
| Framework | Boost.Test and Python standard library |
| Scope | Multiple real components wired together, including process-level CLI and HTTP flows. |
| Examples | cache-aside, storage backend wiring, analytics repository roundtrips, Python HTTP flows, CLI create/get/stats process tests. |
| Acceptance | Tests may use temp directories and temp databases, but must not require external SaaS or uncontrolled shared state. |

CLI integration tests spawn the built `url_shortener` binary through
`SIMPLE_HTTP_BIN`, run commands in isolated temporary working directories, assert
stdout JSON, and validate persistence by running a follow-up command against the
same state.

### 3.4 End-to-end tests

| Field | Plan |
|---|---|
| CTest label | `e2e` |
| Location | `tests/e2e/scripts/sections/` |
| Framework | Bash + Python helper scripts |
| Scope | Black-box operational scenarios and manual-scenario analogs. |
| Examples | server boot, create, redirects, expiration, fingerprinting, auth, permissions, admin API, errors, concurrency, CLI commands, CLI/server separation. |
| Acceptance | Every manual scenario documented in this plan must map to an executable section script under `tests/e2e/scripts/sections/`. |

E2E sections are Linux/CI-oriented because they require Bash, Python 3, process
inspection, and POSIX runtime behavior. They are registered with CTest only on
non-Windows hosts with a Bash interpreter.

### 3.5 Performance and regression tests

| Field | Plan |
|---|---|
| CTest label | `perf` |
| Location | `tests/perf/` |
| Scope | Latency and throughput checks for the redirect fast path and analytics pipeline. |
| Examples | `redirect__redirect_fast_path_guard` (Release-measured timing guard and C-registry-presence ratio); `analytics__*` placeholder benchmarks. |
| Acceptance | The `perf` label is **excluded** from the default `-L "unit|contract|integration|e2e"` union; select it explicitly with `-L perf`. The redirect timing guard is advisory on Debug (skips with `skipped: not an optimized build`); on Release it enforces a C-registry-presence ratio ≤ 1.10. The structural isolation guard (`commands__80_redirect_isolation_test`) is a `unit`-label gate, not `perf`. |

The `perf` label is deliberately kept out of routine developer runs. The redirect
guard's baseline in `tests/perf/redirect/baseline.json` is recorded only by a
human on a quiet Release machine; the committed placeholder (zeroed samples)
leaves the guard advisory until a real baseline is written.

### 3.6 Conformance tests

| Field | Plan |
|---|---|
| CTest label | `conformance` (also carries `integration`) |
| Location | `tests/conformance/` |
| Framework | Boost.Test (C++ runners), Python 3 (harness), run with `python3 -I` |
| Scope | Replay the command corpus through six transport shims and assert identical `result`/`error.code` across all of them. |
| Examples | `conformance__01_direct_registry_corpus`, `conformance__02_tui_client_corpus`, `conformance_10_cli_cmd_corpus`, `conformance_11_stdio_corpus`, `conformance_13_rest_generic_corpus`, `conformance_14_rest_resource_corpus`. |
| Acceptance | Every corpus case must produce the same `ok`, `result` structure, and `error.code` on every transport that the case does not explicitly skip; lint clean; coverage gate passes; authz-matrix drift check green. |

Conformance tests run inside the `-L "unit|contract|integration"` union because
they also carry the `integration` label. Use `-L conformance` to select them
alone.

## 4. Directory and ownership map

```
tests/
├── unit/                         # unit label, Boost.Test
│   ├── app/                      # shared command-layer behavior
│   ├── cli/                      # CLI parser and command dispatch boundaries
│   ├── commands/                 # command registry, envelope, ArgSpec, catalog
│   ├── core/                     # URL, slug, expiry, redirect status
│   ├── http/                     # route registry, router, handlers
│   ├── storage/                  # in-memory, SQL, SQLite, PostgreSQL, Redis
│   ├── analytics/                # event, queue, worker, retention, stats
│   ├── config/                   # YAML/backend config parsing
│   ├── security/                 # auth, tokens, access control
│   ├── docs/                     # generated/reference documentation checks
│   └── tui/                      # TUI models, goldens, flows (headless only)
│       └── golden/               # render goldens (d01_*..d13_*, request JSON)
├── contract/metadata/            # contract label, backend behavior
├── conformance/                  # conformance + integration labels
│   ├── commands/                 # corpus JSON suites (one file = one suite)
│   ├── normalizer_vectors.json   # single normative source for matcher rules
│   ├── py/                       # Python harness + shims (10..14, 20, 30..32)
│   ├── runners/                  # C++ runners (direct, tui)
│   └── support/                  # C++ support library (never linked to product)
├── integration/
│   ├── cli/                      # process-level CLI command tests
│   ├── core/                     # Python HTTP flows
│   ├── link_management/          # REST management-plane flows
│   ├── storage/                  # storage and cache integration
│   ├── config/                   # config-driven wiring
│   ├── cpp/analytics/            # C++ analytics integration
│   └── sqlite/ psql/ redis/      # backend-specific integration
├── perf/                         # perf label, advisory timing guards
│   ├── analytics/                # analytics pipeline benchmarks (placeholders)
│   └── redirect/                 # redirect fast-path guard + baseline.json
└── e2e/
    ├── scripts/
    │   ├── common.sh             # shared helpers incl. run_with_timeout
    │   ├── run_section.sh
    │   ├── run_all_sections.sh
    │   └── sections/             # executable manual-scenario analogs
    ├── expected/                 # deterministic state snapshots
    ├── failures/                 # failure artifacts, git-ignored
    └── tmp/                      # runtime scratch, git-ignored
```

Corpus linting and coverage tooling lives alongside the corpus:

- `tools/conformance_lint.py` — lint rules 1-14; also run as
  `conformance_00_corpus_lint`.
- `tools/conformance_coverage.py` — coverage gate (also run as
  `conformance_20_coverage_gate`); reads per-transport summaries from
  `$URLSHORTENER_CONFORMANCE_OUT`; waivers in
  `tools/conformance_waivers.json`.
- `tools/conformance_gen_authz.py` — authz-matrix generator.

Companion documents:

- [`sqlite_state_assert.md`](./sqlite_state_assert.md) documents the SQLite and
  runtime assertion helper.
- [`e2e_testing/AGENTIC_QA.md`](./e2e_testing/AGENTIC_QA.md) documents the e2e
  scenario execution model.
- [`cli_link_commands.md`](./cli_link_commands.md) is the manual checklist for
  the `link` one-shot CLI subcommands.
- [`tui_manual_smoke.md`](./tui_manual_smoke.md) is the manual smoke checklist
  (S1-S21) and D1-D13 diagram sign-off for the interactive `tui` mode (owned
  by Task 06.0).
- Category plans:
  - [`testplans/unit.md`](./testplans/unit.md)
  - [`testplans/contract.md`](./testplans/contract.md)
  - [`testplans/integration.md`](./testplans/integration.md)
  - [`testplans/conformance.md`](./testplans/conformance.md)
  - [`testplans/e2e.md`](./testplans/e2e.md)
  - [`testplans/cli.md`](./testplans/cli.md)
  - [`testplans/rest-api.md`](./testplans/rest-api.md)
  - [`testplans/storage.md`](./testplans/storage.md)
  - [`testplans/analytics.md`](./testplans/analytics.md)
  - [`testplans/security.md`](./testplans/security.md)
  - [`testplans/tui.md`](./testplans/tui.md) (owned by Task 07.0/29)
- `docs/agent/testing-requirements.md` defines the project quality bar for new
  changes.

## 5. Manual scenario to e2e traceability

Every manual scenario must have an executable e2e analog. If a scenario is not
yet automated, the change that introduces it is incomplete.

| Manual scenario category | Manual scenario | E2E analog |
|---|---|---|
| Server lifecycle | Start service, prove readiness, stop cleanly, prevent duplicate startup. | `tests/e2e/scripts/sections/01_server_boot.sh` |
| REST link creation | Create a short URL, verify response data, verify persisted state. | `tests/e2e/scripts/sections/02_create_short_url.sh` |
| Redirect behavior | Resolve prefixed/root redirects and verify target/status behavior. | `tests/e2e/scripts/sections/03_redirects.sh` |
| Expiration | Create expiring links and verify expired links stop redirecting. | `tests/e2e/scripts/sections/04_expiration.sh` |
| Fingerprinting | Capture request fingerprint signals without storing unsafe raw identifiers. | `tests/e2e/scripts/sections/05_fingerprinting.sh` |
| Authentication | Login/session behavior and failed-auth handling. | `tests/e2e/scripts/sections/06_authentication.sh` |
| Permissions | Role-gated read/write/admin operations. | `tests/e2e/scripts/sections/07_permissions.sh` |
| Admin API | Admin operational endpoints and audit-visible effects. | `tests/e2e/scripts/sections/08_admin_api.sh` |
| Error handling | Malformed input, unsupported operations, and stable error responses. | `tests/e2e/scripts/sections/09_error_handling.sh` |
| Concurrency | Concurrent create/redirect/analytics behavior. | `tests/e2e/scripts/sections/10_concurrency.sh` |
| CLI link create | Run `link create`, assert console JSON, and verify command success without service startup. | `tests/e2e/scripts/sections/11_cli_link_create.sh` |
| CLI link get | Run `link create`, then `link get --slug` and `link get --id`, and compare persisted console data. | `tests/e2e/scripts/sections/12_cli_link_get.sh` |
| CLI/server separation | Run a CLI command and prove port `8000` remains closed after process exit. | `tests/e2e/scripts/sections/13_cli_no_server_socket.sh` |
| `cmd`/`serve-stdio`/`tui` no-listener | Prove that `cmd`, `serve-stdio` (while alive), and a non-TTY `tui` refusal leave no LISTEN socket; `uri.txt` sentinel unchanged. | `tests/e2e/scripts/sections/19_cmd_serve_stdio_no_listener.sh` |
| `serve-stdio` NDJSON session | Full request/response NDJSON session with error handling, job dispatch, and clean EOF. | `tests/e2e/scripts/sections/30_serve_stdio.sh` |

Detailed manual steps for these scenarios live under
`docs/testing/e2e_testing/sections/`.

## 6. CLI test plan

The CLI test package validates the one-shot command surface and its separation
from REST server mode.

| Layer | Required coverage |
|---|---|
| Unit | Parser maps all nine `link <verb>` argv into typed command descriptors; `cmd` parser maps `cmd <command> [--arg k=v ...]` into an envelope; app command service rejects invalid URLs, reserved slugs, malformed slugs, invalid stats windows, and storage errors. |
| Integration | Spawn the binary, assert exit code, parse stdout JSON, compare created/read data, verify persisted state across invocations (SQLite backend), and verify isolated temp directories do not share state. The `serve-stdio` integration tests (`cli_40`, `cli_41`) run NDJSON sessions and verify EOF/job behavior. |
| Conformance | The corpus covers `link.*` commands through all six transports including `cli_cmd` and `stdio` shims; conformance assertions run via `python3 -I`. See [`testplans/conformance.md`](./testplans/conformance.md). |
| E2E | Execute sections `11_cli_link_create`..`18_cli_link_preview` as black-box shell scenarios; section `19_cmd_serve_stdio_no_listener` for `cmd`/`serve-stdio`/`tui` no-listener guarantees; section `30_serve_stdio` for the full NDJSON session. |

CLI assertions must cover:

- `link create --url <url> [--slug <slug>] [--base-domain <url>]`
- `link get --slug <slug>` and `link get --id <id>`
- `link update`, `link delete`, `link enable`, `link disable`, `link restore`,
  `link preview`, `link stats`
- `cmd <command> [--arg key=value ...] [--read-only]`
- `serve-stdio` (NDJSON session over stdin/stdout)
- `tui` (non-TTY refusal exits `2` without binding a listener)
- valid JSON emitted to stdout on success;
- non-zero exit and diagnostic output on invalid input;
- persistent state changes after create with a configured SQLite backend;
- no HTTP listener started in `cmd`, `serve-stdio`, or `tui` mode.

**Storage note (updated for 02.0):** CLI mode uses `LinkBackend`, which selects
the backend from `--storage-config`. With the default in-memory backend, state
is per-process: a `link create` in one invocation is not visible to a separately
launched process. With a SQLite backend (`--storage-config <file>`), state is
durable and two invocations sharing the same database file do share state (see
integration test `cli_18_server_and_cli_share_sqlite`). Cross-invocation tests
must use a SQLite backend; in-memory tests are scoped to single-invocation paths
or same-process unit tests.

## 7. REST API test plan

REST API tests validate the server-mode adapter over the same command layer.

| Layer | Required coverage |
|---|---|
| Unit | Route registry completeness, router matching, handler error mapping, JSON response serialization, compatibility aliases, redirect handler isolation. |
| Integration | Python HTTP tests create/read/update/delete links, exercise redirect status behavior, and validate analytics/stat endpoints. |
| E2E | Sections `01` through `10` run black-box service scenarios with runtime and state assertions. |

REST tests must preserve the redirect fast path: redirect sections should remain
narrow and must not depend on management-plane CLI code.

## 8. Storage and database test plan

Storage tests prove that command/API behavior produces durable state through the
configured backend.

| Layer | Required coverage |
|---|---|
| Unit | In-memory maps, SQL row mapping, SQLite/PostgreSQL dialect SQL, Redis serialization, cache TTL behavior. |
| Contract | Metadata repository behavior shared across backends. |
| Integration | SQLite/PostgreSQL contract runners, backend swap by config, cache-aside/invalidation, CLI create/get state persistence where command mode uses local state. |
| E2E | State snapshots in `tests/e2e/expected/` for service scenarios; CLI sections validate persisted command-visible state through follow-up commands. |

Use `tools/sqlite_state_assert.py` for deterministic database and runtime
assertions when a scenario writes SQLite state.

## 9. Analytics test plan

| Layer | Required coverage |
|---|---|
| Unit | Event builders, sanitizers, client ID hashing, queue overflow, worker retry, stats handler validation, retention cleanup. |
| Integration | Redirect events, terminal not-found events, repository failure isolation, configured runtime wiring, aggregate stats responses. |
| E2E | Redirect and stats behavior through sections `03`, `09`, `10`, and CLI stats integration where available. |

Analytics failures must never fail redirect responses or one-shot CLI link
operations unrelated to stats.

## 10. Security and privacy test plan

| Layer | Required coverage |
|---|---|
| Unit | Password hashing, token generation, access guard, auth broker, URL validation, private target rejection, log redaction, command allowlist/redaction (`commands__70`..`72`), authz descriptor matrix (`commands__74`), authz before side effects (`commands__75`), import row validation parity (`commands__76`), import limits (`commands__77`), path argument policy (`commands__78`), no-listener/no-URI-store guard (`commands__79`), redirect isolation (`commands__80`), TUI no-secrets guard (`tui__34`). |
| Integration | Auth/session flows, permissions, admin APIs, analytics privacy checks. Authz-matrix process-level check: `conformance_authz_matrix_check`. Import SSRF/size/path: `conformance_31_import_limits_process_level`. No-listener: `conformance_32_no_listener_process_level`. |
| Conformance | Authorization corpus (`41_authorization.json`, `42_authorization_matrix.json`); process-level authz: `conformance_30_authz_process_level`. |
| E2E | Sections `05`, `06`, `07`, `08`, `09`, plus section `19_cmd_serve_stdio_no_listener` for the no-listener/URI-store guarantees. |

Never assert on or log raw secrets, tokens, TLS keys, DSNs, private key
passphrases, or analytics salts.

## 11. Build and execution commands

### Configure and build

Windows:

```powershell
cmake -S . -B cmake-build -G "Visual Studio 17 2022" -DCMAKE_TOOLCHAIN_FILE="C:\Program Files\Microsoft Visual Studio\2022\Community\VC\vcpkg\scripts\buildsystems\vcpkg.cmake" -DVCPKG_TARGET_TRIPLET=x64-windows
cmake --build cmake-build --config Debug
```

Linux/macOS:

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

### Run by category

```powershell
ctest --test-dir cmake-build -C Debug -L unit --output-on-failure
ctest --test-dir cmake-build -C Debug -L contract --output-on-failure
ctest --test-dir cmake-build -C Debug -L integration --output-on-failure
ctest --test-dir cmake-build -C Debug -L conformance --output-on-failure
ctest --test-dir cmake-build -C Debug -L e2e --output-on-failure
ctest --test-dir cmake-build -C Debug -L perf --output-on-failure
ctest --test-dir cmake-build -C Debug -L "unit|contract|integration|e2e" --output-on-failure
```

Linux single-config generators omit `-C Debug`:

```bash
ctest --test-dir build -L "unit|contract|integration|e2e" --output-on-failure
# conformance tests also carry the integration label, so they run in the union above;
# to select them alone:
ctest --test-dir build -L conformance --output-on-failure
# perf is excluded from the union; select explicitly:
ctest --test-dir build -L perf --output-on-failure
```

### Conformance linting

```bash
# Lint the corpus directly (no build required):
python3 tools/conformance_lint.py tests/conformance/commands/*.json
# On Windows/PowerShell pass the directory (the tool globs):
python tools/conformance_lint.py tests/conformance/commands
# Or via CTest:
ctest --test-dir cmake-build -R "^conformance_00_corpus_lint$" --output-on-failure
```

### TUI golden update and FormModel properties

```bash
# Regenerate screen goldens after an intentional layout change:
UPDATE_GOLDEN=1 ctest --test-dir cmake-build -R "^tui__" --output-on-failure
# Reproduce a FormModel property failure from its seed:
FORM_PROP_SEED=<seed> ctest --test-dir cmake-build -R "^tui__30_" --output-on-failure
```

### Run e2e manually

```bash
bash tests/e2e/scripts/run_section.sh 01_server_boot
bash tests/e2e/scripts/run_section.sh 11_cli_link_create
bash tests/e2e/scripts/run_section.sh 19_cmd_serve_stdio_no_listener
QA_SECTIONS="01_server_boot,11_cli_link_create,12_cli_link_get,13_cli_no_server_socket" \
  bash tests/e2e/scripts/run_all_sections.sh
```

`run_all_sections.sh` runs its default service scenario list unless
`QA_SECTIONS` is provided. CLI e2e sections can always be run directly by
`run_section.sh` and are registered separately by CTest when e2e tests are
enabled.

E2E sections depend on a Bash interpreter with POSIX tooling. They use
`run_with_timeout` from `tests/e2e/scripts/common.sh`, which prefers GNU
`timeout`, then `gtimeout`, then a pure-shell watchdog — so wall-clock bounds
are consistent whether or not `timeout` is on PATH. Sections that carry
wall-clock bounds may fail under ThreadSanitizer overhead.

### ThreadSanitizer (TSan)

Build a separate sanitizer tree; never re-configure `cmake-build`:

```bash
cmake -S . -B cmake-build-tsan -G Ninja \
      -DCMAKE_CXX_FLAGS="-fsanitize=thread -g" \
      -DCMAKE_C_FLAGS="-fsanitize=thread -g"
cmake --build cmake-build-tsan
ctest --test-dir cmake-build-tsan -L "unit|conformance" --output-on-failure
```

Under TSan the Python corpus runners and any test with a wall-clock bound may
hit timeouts; run them individually (`-R "^<name>$"`) to verify they pass in
isolation.

### Coverage (lcov / llvm-cov)

The CMake `coverage` target shells out to `lcov` and `genhtml`:

```bash
cmake -S . -B build-cov -G Ninja \
      -DCMAKE_BUILD_TYPE=Debug \
      -DENABLE_COVERAGE=ON
cmake --build build-cov
ctest --test-dir build-cov -L "unit|integration|conformance"
cmake --build build-cov --target coverage
# HTML report: build-cov/coverage_html/index.html
```

On macOS where `lcov` is unavailable, use the llvm-cov fallback:

```bash
# Configure with profiling flags instead of ENABLE_COVERAGE:
cmake -S . -B build-cov -G Ninja \
      -DCMAKE_CXX_FLAGS="-fprofile-instr-generate -fcoverage-mapping" \
      -DCMAKE_C_FLAGS="-fprofile-instr-generate -fcoverage-mapping" \
      -DCMAKE_EXE_LINKER_FLAGS="-fprofile-instr-generate"
cmake --build build-cov
env LLVM_PROFILE_FILE="$PWD/build-cov/profraw/%p-%m.profraw" \
    ctest --test-dir build-cov -L "unit|integration|conformance"
xcrun llvm-profdata merge -sparse \
    $(find build-cov/profraw -name '*.profraw') \
    -o build-cov/merged.profdata
xcrun llvm-cov report build-cov/url_shortener \
    $(find build-cov -name '*.o' | head -1) \
    -instr-profile=build-cov/merged.profdata \
    src/commands src/transport src/tui src/app src/composition src/http
```

Temp file names for per-process profraw output must be unique per-process
(`%p-%m` in `LLVM_PROFILE_FILE`); a fixed name causes one process to overwrite
another's data under parallel `ctest -j`.

## 12. Adding or changing tests

1. Add the narrowest unit test first.
2. Add contract coverage when behavior belongs to storage adapter semantics.
3. Add integration coverage when behavior depends on multiple real components
   or process execution.
4. Add or update the e2e section for the corresponding manual scenario.
5. Update the traceability table in this document.
6. Register new tests in `CMakeLists.txt`; add new common C++ sources to
   `sources.cmake` when they are part of `url_shortener_common`.
7. Run the smallest affected target, then the relevant category label.

New C++ test filenames use numeric prefixes. Most registered C++ tests use the
target name `<parent_dir>__<filename_stem>`; legacy core/link-management unit
lists use the filename stem directly.

### Recipe: new command

1. Write a corpus case in the matching suite file under
   `tests/conformance/commands/` following the schema in
   `tests/conformance/commands/README.md`. Run
   `python3 tools/conformance_lint.py tests/conformance/commands/*.json` to
   confirm the case is lint-clean.
2. Add a unit test under `tests/unit/commands/` for the happy path and the main
   error branches.
3. Run `ctest --test-dir cmake-build -L conformance` to prove all six transport
   shims agree.

### Recipe: new transport shim

1. Implement `ITransportAdapter` and register it in the factory (see
   `docs/commands/adding-a-transport.md`).
2. Add a runner script/file to `tests/conformance/py/` or
   `tests/conformance/runners/` following the existing shim naming convention.
3. Run the full `-L conformance` suite to confirm all corpus cases pass on the
   new transport.

### Recipe: new TUI screen

1. Add a golden render test under `tests/unit/tui/` using the headless
   `Screen` harness; commit the initial golden with `UPDATE_GOLDEN=1`.
2. Add a row to the D1-D13 diagram table in
   [`testplans/tui.md`](./testplans/tui.md).
3. If the screen introduces a new state-machine edge, add it to the router edge
   table tested by `tui__31_screen_transition_requests_test`.

### Recipe: new `ArgSpec` type or form field

1. The `FormModel` property test (`tui__30_form_model_property_test`) exercises
   every registered command's `ArgSpec` through a property-based round-trip.
   After adding the type, run:

   ```bash
   FORM_PROP_SEED=0 ctest --test-dir cmake-build -R "^tui__30_" --output-on-failure
   ```

2. If a seed fails, reproduce it with `FORM_PROP_SEED=<seed>` and add a
   deterministic unit case covering that input.
