# Task 07.0 - Tests and conformance

**Parent milestone:** [Milestone 0006 - Unified Command Model and TUI](/docs/roadmap/0006-unified-command-model-and-tui/plan.md)
**Status:** ⬜ Not started
**Depends on:** [01.0](/docs/roadmap/0006-unified-command-model-and-tui/01.0-command-model-core/README.md) (registry, envelope), [03.0](/docs/roadmap/0006-unified-command-model-and-tui/03.0-link-and-browse-commands/README.md) (commands), [04.0](/docs/roadmap/0006-unified-command-model-and-tui/04.0-rest-and-cli-adapters/README.md) (REST, `cmd`), [05.0](/docs/roadmap/0006-unified-command-model-and-tui/05.0-jobs-pairs-and-stdio/README.md) (jobs, stdio), [06.0](/docs/roadmap/0006-unified-command-model-and-tui/06.0-tui/README.md) (TUI). Work is incremental: groups A-D can start as soon as 01.0 lands and grow with each task; the final gate (group G) runs after 06.0.

## Scope

Prove the milestone's central claim - "identical commands on every transport" -
as an executable check, and protect the contracts the other tasks establish:

1. A JSON **conformance corpus** (`tests/conformance/commands/*.json`) replayed
   through six adapter shims: direct registry, REST generic, REST resource
   routes, `cmd` CLI, `serve-stdio`, TUI `InProcessClient`.
2. A **catalog-drift** gate (`docs/commands/README.md` and the section 5 route
   table versus the live registry).
3. **Security** tests (allowlist, redaction, `not_authorized`, SSRF and size and
   path limits on import, no listener).
4. A **TUI snapshot harness** (mock `ICommandClient`, golden text, no terminal).
5. **Redirect fast-path** guards (structural and timing).
6. The closing **coverage-gap** run and review gates.

## Depends on / mini-graph

```text
A schema+support    01 -> 02 -> 03
B adapters          03 -> 04 (direct)            05 (py common) -> 06 cli, 07 stdio, 08 rest-generic -> 09 rest-resource
                    10 (tui shim: needs 06.0 shell + 03)
C corpus + gates    04 -> 11, 12, 13, 14 -> 15 (coverage gate, also needs shims 06-10) ; 16 (catalog drift, prose), 17 (route drift)
D security          18, 19, 20, 21   (independent of C once the registry exists)
E TUI harness       22 -> 23 -> 24, 25 ; 22 -> 26 ; 22+23 -> 27, 28 ; 22-28 + 06.0/20 -> 29 (test plan / checklist traceability)
F perf              30 -> 32 ; 31 (structural guard)
G gates             04, 06-10, 30, 32 -> 33 (CI wiring) -> 34 (coverage gap + review gates) -> 08.0/01
```

## Subtasks

| # | Document | Status | Depends on | Blocks |
|---|----------|--------|------------|--------|
| 01 | [Corpus schema and lint](01-corpus-schema-and-lint.md) | ⬜ Not started | Task 01.0 | 02, 03, 05, 11, 12, 13, 14 |
| 02 | [Matcher and normalizer vectors](02-matcher-and-normalizer-vectors.md) | ⬜ Not started | 01 | 03, 05 |
| 03 | [C++ conformance support library](03-cpp-conformance-support-library.md) | ⬜ Not started | 01, 02 | 04, 10 |
| 04 | [Direct registry shim and runner](04-direct-registry-shim-and-runner.md) | ⬜ Not started | 03, Task 01.0, Task 02.0, Task 03.0 | 10, 11, 12, 13, 14, 33 |
| 05 | [Python harness common module](05-python-harness-common-module.md) | ⬜ Not started | 01, 02 | 06, 07, 08 |
| 06 | [CLI `cmd` shim](06-cli-cmd-shim.md) | ⬜ Not started | 05, Task 02.0, Task 04.0 | 15, 33 |
| 07 | [serve-stdio shim](07-serve-stdio-shim.md) | ⬜ Not started | 05, Task 05.0 | 15, 33 |
| 08 | [REST generic shim](08-rest-generic-shim.md) | ⬜ Not started | 05, Task 04.0 | 09, 15, 33 |
| 09 | [REST resource-route shim](09-rest-resource-route-shim.md) | ⬜ Not started | 08, Task 04.0 | 15, 33 |
| 10 | [TUI InProcessClient shim](10-tui-inprocess-client-shim.md) | ⬜ Not started | 03, 04, Task 06.0 | 15, 33 |
| 11 | [Corpus: link commands](11-corpus-link-commands.md) | ⬜ Not started | 01, 04, Task 03.0 | 15 |
| 12 | [Corpus: browse and system commands](12-corpus-browse-and-system-commands.md) | ⬜ Not started | 01, 04, Task 03.0 | 15 |
| 13 | [Corpus: jobs, import and export](13-corpus-jobs-import-export.md) | ⬜ Not started | 01, 04, Task 05.0 | 15 |
| 14 | [Corpus: envelope errors and authorization](14-corpus-envelope-errors-and-authorization.md) | ⬜ Not started | 01, 04, Task 01.0, Task 03.0 | 15 |
| 15 | [Corpus coverage gate](15-corpus-coverage-gate.md) | ⬜ Not started | 06, 07, 08, 09, 10, 11, 12, 13, 14 | 34 |
| 16 | [Catalog drift: command sections](16-catalog-drift-command-sections.md) | ⬜ Not started | Task 01.0, Task 03.0 | 34, 08.0/03 |
| 17 | [Catalog drift: route table](17-catalog-drift-route-table.md) | ⬜ Not started | Task 04.0 | 34, 08.0/09 |
| 18 | [Security: allowlist and redaction](18-security-allowlist-and-redaction.md) | ⬜ Not started | Task 01.0, Task 03.0, Task 05.0 | 34 |
| 19 | [Security: authorization matrix](19-security-authorization-matrix.md) | ⬜ Not started | Task 01.0, Task 04.0, Task 05.0 | 34 |
| 20 | [Security: import SSRF, size and path limits](20-security-import-ssrf-size-and-path-limits.md) | ⬜ Not started | Task 05.0 | 34 |
| 21 | [Security: no listener, no URI store](21-security-no-listener-and-no-uri-store.md) | ⬜ Not started | Task 04.0, Task 05.0, Task 06.0 | 34 |
| 22 | [TUI mock command client (extends the 06.0 mock)](22-tui-mock-command-client.md) | ⬜ Not started | Task 06.0 | 23, 26, 27, 28, 29, 08.0/11 |
| 23 | [TUI snapshot renderer and golden compare (extends the 06.0 harness)](23-tui-snapshot-renderer-and-golden-compare.md) | ⬜ Not started | 22 | 24, 25, 27, 28, 29, 08.0/11 |
| 24 | [TUI snapshots: navigation screens](24-tui-snapshots-navigation-screens.md) | ⬜ Not started | 23, Task 06.0 | 29, 34, 08.0/10, 08.0/11 |
| 25 | [TUI snapshots: forms, jobs, palette, dialogs](25-tui-snapshots-forms-jobs-palette-dialogs.md) | ⬜ Not started | 23, Task 06.0 | 29, 34, 08.0/10, 08.0/11 |
| 26 | [FormBuilder property tests](26-form-builder-property-tests.md) | ⬜ Not started | 22, Task 01.0, Task 06.0 | 29, 34, 08.0/11 |
| 27 | [TUI state machine and non-blocking tests](27-tui-state-machine-and-nonblocking-tests.md) | ⬜ Not started | 22, 23, Task 06.0 | 29, 34, 08.0/11 |
| 28 | [TUI confirm and read-only tests](28-tui-confirm-and-read-only-tests.md) | ⬜ Not started | 22, 23, Task 06.0 | 29, 34, 08.0/11 |
| 29 | [TUI test plan and checklist traceability](29-tui-test-plan-and-checklist-traceability.md) | ⬜ Not started | 22, 23, 24, 25, 26, 27, 28, 06.0/20 | 34 |
| 30 | [Perf label registration](30-perf-label-registration.md) | ⬜ Not started | none | 32, 33 |
| 31 | [Redirect isolation structural guard](31-redirect-isolation-structural-guard.md) | ⬜ Not started | Task 01.0 | 34 |
| 32 | [Redirect fast-path timing guard](32-redirect-fast-path-timing-guard.md) | ⬜ Not started | 30, Task 02.0, Task 04.0 | 33, 34 |
| 33 | [CI wiring](33-ci-wiring.md) | ⬜ Not started | 04, 06, 07, 08, 09, 10, 30, 32 | 34 |
| 34 | [Coverage-gap run and review gates](34-coverage-gap-run-and-review-gates.md) | ⬜ Not started | 15, 16, 17, 18, 19, 20, 21, 24, 25, 26, 27, 28, 29, 31, 32, 33 | 08.0/01 |

## Conventions shared by all subtasks (read once)

**Directory layout (new):**

```text
tests/conformance/commands/*.json      corpus (data only; schema in subtask 01)
tests/conformance/commands/README.md   schema + authoring guide
tests/conformance/normalizer_vectors.json   matcher/normalizer vectors shared by C++ and Python
tests/conformance/support/*.hpp|.cpp   C++ loader, mini JSON, matcher, runner, shims
tests/conformance/runners/NN_*.cpp     C++ runners (one per in-process transport)
tests/conformance/py/*.py              Python harness + out-of-process runners
tests/unit/tui/ , tests/unit/commands/ TUI and security unit tests (plan.md file map)
tests/perf/redirect/                   redirect guard
tools/conformance_*.py                 lint / coverage tools
```

**CTest names and labels.** Existing rules (CLAUDE.md): `register_labeled_cpp_tests`
names targets `<parent_dir>__<stem>`; the `CORE_BASIC_UNIT_SOURCES` /
`LINK_MANAGEMENT_UNIT_SOURCES` / `HTTP_UNIT_SOURCES` lists use the bare stem;
`APP_UNIT_SOURCES` uses a dedicated `foreach` with extra include dirs and target
`app__<stem>`. Each list has exactly **one owner** that creates it and its
registration (plan.md "Test file prefixes and CMake list ownership"); this task
only appends to lists owned by other tasks and owns the conformance and perf lists:

| List in `sources.cmake` | Owner | Registration | Target name | Label |
|---|---|---|---|---|
| `COMMANDS_UNIT_SOURCES` (`tests/unit/commands/*.cpp`) | Task 01.0 subtask 01 | `register_labeled_cpp_tests(COMMANDS_UNIT_SOURCES "unit")` (helpers in the same folder are found by quoted includes; no extra include dir) | `commands__<stem>` | `unit` |
| `TUI_MODEL_UNIT_SOURCES`, `TUI_RENDER_UNIT_SOURCES` (`tests/unit/tui/*.cpp`), only `if(URLSHORTENER_ENABLE_TUI)` | Task 06.0 subtask 01 | `register_tui_tests(...)` | `tui__<stem>` | `unit` |
| `CONFORMANCE_CPP_SOURCES` (`tests/conformance/runners/*.cpp`) | this task, subtask 03 | dedicated loop, include `tests/conformance`, define `CONFORMANCE_CORPUS_DIR` | `conformance__<stem>` | `integration;conformance` |
| `CONFORMANCE_PY_SOURCES` (`tests/conformance/py/NN_*.py`) | this task, subtask 05 | like `CLI_INTEGRATION_SOURCES` (Python3 + `SIMPLE_HTTP_BIN`) | `conformance_<stem>` | `integration;conformance` |
| `PERF_SOURCES` (`tests/perf/**/*.cpp`) | this task, subtask 30 | `register_labeled_cpp_tests(PERF_SOURCES "perf")` + `RUN_SERIAL TRUE`, `TIMEOUT 300` | `<parent_dir>__<stem>` (`analytics__...`, `redirect__...`) | `perf` |

Test prefixes used by this task: `tests/unit/commands/70-89`, `tests/unit/tui/20-69`,
`tests/unit/docs/03-09` (plan.md table). The extra CTest label `conformance` and the
registered `PERF_SOURCES` list are open decision D8 in the decision log.

The second label `conformance` lets `ctest -L conformance` select the suite
while the documented union `-L "unit|contract|integration|e2e"` still includes it.
Stems must be unique within a list (the target name embeds only the parent
directory in the generic macro, so two files with the same stem in different
folders collide - avoid by using the numeric prefix).

**Adding a test (CLAUDE.md):** numeric-prefixed file in the sibling folder,
then append the path to the list in `sources.cmake` (all `.cpp` compiled into
`url_shortener_common` must be listed there; test executables are registered by
the lists above in `CMakeLists.txt`).

**Windows.** Build and ctest need `-C Debug`; e2e shell sections are POSIX-only
(CMake skips them on `CMAKE_HOST_WIN32`). All Python runners must work on
Windows (no `/proc`, no `fork`); only subtask 21's socket check has a POSIX-only
section script, with a portable Python twin.

**Roles.** Corpus role `admin` = `ControlSet::Admin`; `user` = `ControlSet::User`.
Local transports (direct, cli_cmd, stdio, tui) obtain `user` via `--read-only`
(or the shim's `CallerContext`); REST obtains `user` from the gated command API
(`--command-api-enabled true`, default `--command-api-role user`, Task 04.0
subtask 08) and `admin` with `--command-api-role admin` from a loopback peer.

## Key constraints

- Corpus is data, not code: a new transport adds one shim and passes the
  existing corpus (catalog section 6/7).
- Tests that need a real terminal are manual-checklist items
  (`docs/testing/tui_manual_smoke.md`, owned by Task 06.0), never CI.
- No new JSON library, not even in tests: the C++ side uses a ~300-line test-only
  DOM parser in `tests/conformance/support/` (`mini_json`, never linked into
  product code, decision log D3 - product code has exactly one tokenizer, the
  01.0/01 scanner); the Python side uses stdlib `json`.
- Drift-test ownership: Task 01.0 subtask 12 owns the generated block 3.7 of the
  catalog (`commands__12_catalog_drift`); subtask 16 here owns the hand-written
  sections 1-6 (headings, argument tables, error table); Task 04.0 subtask 10
  regenerates `docs/api/README.md` and subtask 17 here owns the section-5 route
  table drift test. No two tests assert the same text.
- No test may touch `src/` behavior; if a test reveals a defect, file it against
  the owning task, do not patch it here.
- Perf checks are advisory by default (docs/testing/README.md 3.5); only the
  structural redirect guard (31) is a hard gate.

## Exit criteria

1. `ctest --test-dir cmake-build -L conformance --output-on-failure` is green and
   every corpus case ran (not skipped) on every transport that is not listed
   with a reason in the case's `transports.skip`.
2. Coverage gate (15) reports 100 % of registry commands and 100 % of catalog
   error codes present in the corpus.
3. Catalog-drift tests (16, 17) are green: `docs/commands/README.md` equals the
   generator output.
4. Security tests 18-21 green; TUI goldens D1-D10 committed and green.
5. `-L perf` runs at least the registered redirect guard; structural guard green.
6. `/test-gap tests/conformance` and `/test-gap src/commands` produce no
   unaddressed P0 item; `/verify-subtask` reports PASS for 07.0 subtasks and
   `/pr-review` returns LGTM.
