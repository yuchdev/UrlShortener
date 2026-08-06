# UrlShortener Test Plan

## 1. Purpose

This document is the project-level test plan for UrlShortener. It defines the
test categories, ownership, execution commands, acceptance criteria, and the
traceability between manual scenarios and executable end-to-end analogs under
`tests/e2e`.

The latest application shape includes two transport modes:

- **Server mode**: the long-running HTTP/HTTPS service exposes REST endpoints
  registered through the route registry and dispatched by the HTTP router.
- **CLI mode**: one-shot commands execute supported link-management operations
  through the shared application command layer and exit without starting the
  HTTP server.

The test strategy verifies both modes independently and verifies that shared
business behavior remains consistent between REST and CLI adapters.

## 2. Quality objectives

| Objective | Required evidence |
|---|---|
| Preserve REST API behavior | HTTP unit characterization tests and integration flows keep status codes, JSON shapes, headers, route labels, redirects, and error mappings stable. |
| Validate CLI one-shot behavior | CLI integration and e2e tests spawn the binary, assert stdout/stderr/exit code, verify persisted state, and prove no server socket remains open. |
| Protect untrusted input boundaries | Unit, integration, and e2e tests cover target URL normalization, SSRF/private-host rejection, slug validation, oversized request protection, and malformed command/API inputs. |
| Keep storage backends interchangeable | Contract tests define metadata repository behavior once and run it against all supported adapters. |
| Keep analytics best-effort | Unit and integration tests verify analytics enqueue, aggregation, retention, disabled mode, and failure isolation without affecting redirects. |
| Maintain operational confidence | E2E scenarios cover startup, health, redirects, admin/auth flows, error handling, concurrency, and CLI/server separation. |

## 3. Test categories

### 3.1 Unit tests

| Field | Plan |
|---|---|
| CTest label | `unit` |
| Location | `tests/unit/` |
| Framework | Boost.Test |
| Scope | Pure logic and narrow component behavior with no external services. |
| Examples | URL validation, slug rules, app command service, HTTP router matching, handlers with fakes, config parsing, analytics builders, security primitives, log redaction. |
| Acceptance | Deterministic, isolated, one behavior per test case. Use fakes for repository/cache/analytics ports. |

Unit tests are the required first layer for new behavior. For the shared
application command layer, unit tests should exercise the app service directly
with fake `ILinkStore` and `ILinkStatsReader` implementations rather than going
through Beast requests or process spawning.

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
| CTest label | `perf` when registered |
| Location | `tests/perf/` |
| Scope | Best-effort latency and throughput regression checks. |
| Examples | redirect overhead, analytics queue saturation, worker batch throughput. |
| Acceptance | Performance runs are advisory unless a workflow explicitly marks them as a gate. |

## 4. Directory and ownership map

```
tests/
├── unit/                         # unit label, Boost.Test
│   ├── app/                      # shared command-layer behavior
│   ├── cli/                      # CLI parser and command dispatch boundaries
│   ├── core/                     # URL, slug, expiry, redirect status
│   ├── http/                     # route registry, router, handlers
│   ├── storage/                  # in-memory, SQL, SQLite, PostgreSQL, Redis
│   ├── analytics/                # event, queue, worker, retention, stats
│   ├── config/                   # YAML/backend config parsing
│   ├── security/                 # auth, tokens, access control
│   └── docs/                     # generated/reference documentation checks
├── contract/metadata/            # contract label, backend behavior
├── integration/
│   ├── cli/                      # process-level CLI command tests
│   ├── core/                     # Python HTTP flows
│   ├── link_management/          # REST management-plane flows
│   ├── storage/                  # storage and cache integration
│   ├── config/                   # config-driven wiring
│   ├── cpp/analytics/            # C++ analytics integration
│   └── sqlite/ psql/ redis/      # backend-specific integration
└── e2e/
    ├── scripts/
    │   ├── run_section.sh
    │   ├── run_all_sections.sh
    │   └── sections/             # executable manual-scenario analogs
    ├── expected/                 # deterministic state snapshots
    ├── failures/                 # failure artifacts, git-ignored
    └── tmp/                      # runtime scratch, git-ignored
```

Companion documents:

- [`sqlite_state_assert.md`](./sqlite_state_assert.md) documents the SQLite and
  runtime assertion helper.
- [`e2e_testing/AGENTIC_QA.md`](./e2e_testing/AGENTIC_QA.md) documents the e2e
  scenario execution model.
- Category plans:
  - [`testplans/unit.md`](./testplans/unit.md)
  - [`testplans/contract.md`](./testplans/contract.md)
  - [`testplans/integration.md`](./testplans/integration.md)
  - [`testplans/e2e.md`](./testplans/e2e.md)
  - [`testplans/cli.md`](./testplans/cli.md)
  - [`testplans/rest-api.md`](./testplans/rest-api.md)
  - [`testplans/storage.md`](./testplans/storage.md)
  - [`testplans/analytics.md`](./testplans/analytics.md)
  - [`testplans/security.md`](./testplans/security.md)
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

Detailed manual steps for these scenarios live under
`docs/testing/e2e_testing/sections/`.

## 6. CLI test plan

The CLI test package validates the one-shot command surface and its separation
from REST server mode.

| Layer | Required coverage |
|---|---|
| Unit | Parser maps `link create`, `link get`, and `link stats` argv into typed command descriptors; app command service rejects invalid URLs, reserved slugs, malformed slugs, invalid stats windows, and storage errors. |
| Integration | Spawn the binary, assert exit code, parse stdout JSON, compare created/read data, verify persisted state across invocations in one temp working directory, and verify isolated temp directories do not share state. |
| E2E | Execute sections `11_cli_link_create`, `12_cli_link_get`, and `13_cli_no_server_socket` as black-box shell scenarios. |

CLI assertions must cover:

- `link create --url <url> [--slug <slug>] [--base-domain <url>]`
- `link get --slug <slug>` and `link get --id <id>`
- `link stats --slug <slug> --from <epoch> --to <epoch> --bucket hour|day|week`
- valid JSON emitted to stdout on success;
- non-zero exit and diagnostic output on invalid input;
- persistent state changes after create;
- no HTTP listener started in command mode.

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
| Unit | Password hashing, token generation, access guard, auth broker, URL validation, private target rejection, log redaction. |
| Integration | Auth/session flows, permissions, admin APIs, analytics privacy checks. |
| E2E | Sections `05`, `06`, `07`, `08`, `09`, plus CLI invalid URL/private host rejection tests. |

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
ctest --test-dir cmake-build -C Debug -L e2e --output-on-failure
ctest --test-dir cmake-build -C Debug -L "unit|contract|integration|e2e" --output-on-failure
```

Linux single-config generators omit `-C Debug`:

```bash
ctest --test-dir build -L "unit|contract|integration|e2e" --output-on-failure
```

### Run e2e manually

```bash
bash tests/e2e/scripts/run_section.sh 01_server_boot
bash tests/e2e/scripts/run_section.sh 11_cli_link_create
QA_SECTIONS="01_server_boot,11_cli_link_create,12_cli_link_get,13_cli_no_server_socket" \
  bash tests/e2e/scripts/run_all_sections.sh
```

`run_all_sections.sh` runs its default service scenario list unless
`QA_SECTIONS` is provided. CLI e2e sections can always be run directly by
`run_section.sh` and are registered separately by CTest when e2e tests are
enabled.

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
