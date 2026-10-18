# Task 01.0 - Command model core

**Parent milestone:** [Milestone 0006 - Unified Command Model and TUI](/docs/roadmap/0006-unified-command-model-and-tui/plan.md)
**Status:** ⬜ Not started
**Depends on:** none (runs in parallel with [Task 02.0](/docs/roadmap/0006-unified-command-model-and-tui/02.0-storage-path-unification/README.md))
**Blocks:** Task 03.0 (all subtasks), Task 04.0, Task 05.0 (via 03.0), Task 07.0 subtask 01

## Scope

Build the transport-agnostic core of [ADR 0002](/docs/adr/0002-unified-command-model.md)
in a new `url_shortener::commands` namespace:

- a depth- and size-bounded JSON object scanner (the existing `core/utils.h`
  helpers cannot enumerate keys or read numbers, see the design note below);
- the single error-code / HTTP-status / CLI-exit-code table;
- the request and response envelope types, parser and serializer, including
  the `Job` stub (`JobInfo`, `JobState`) and the job-submission seam;
- `ArgSpec`, `ArgsView` and argument validation;
- `CommandDescriptor` and `CommandRegistry` with the full
  validate -> authorize -> arg-check -> options -> handler -> audit pipeline;
- the built-in `command.list`, the catalog/help renderers and the generator
  tool with a drift test.

No link command is migrated here (that is Task 03.0). The task ships the
machinery plus `command.list` so the pipeline is exercised end to end by tests
with fake handlers.

### Design note: why a scanner subtask exists

ADR 0002 says `ArgsView` is "a thin accessor over the `core/utils.h` helpers".
Reading the code shows that is not enough:

- `extractJsonValueToken` / `extractJsonStringField` find the first textual
  occurrence of `"key"` anywhere in the body, including inside nested objects
  and string values, so `{"args":{"command":"x"},"command":"link.get"}` is
  mis-read.
- `extractJsonValueToken` returns `nullopt` for bare numbers, `true` and
  `false`, so the envelope `v` field (an integer) cannot be read with it.
- Nothing can enumerate keys, so "unknown keys are rejected (`invalid_field`)"
  cannot be implemented.

Subtask 01 therefore adds one small depth-aware top-level object scanner in
`src/commands/`. It is not a JSON library (no DOM, no dependency, objects only,
leaf decoding reuses `core/utils.h` where it can). This is the smallest
deviation from "no new JSON library" that makes the ADR implementable; it is
recorded as a risk below and in the hand-off notes.

## Subtasks

| # | Document | Status | Depends on | Blocks |
|---|----------|--------|------------|--------|
| 01 | [JSON object scanner](01-json-object-scanner.md) | ⬜ Not started | none | 03, 05, 04.0/01, 05.0/14, 06.0/02 |
| 02 | [Error-code / status / exit-code table](02-error-code-status-table.md) | ⬜ Not started | none | 03, 04, 05, 03.0/05, 04.0/02, 04.0/15 |
| 03 | [Request envelope parser](03-request-envelope-parser.md) | ⬜ Not started | 01, 02 | 06, 04.0/09, 04.0/12, 05.0/14, 06.0/02 |
| 04 | [Response envelope and Job stub](04-response-envelope-and-job-stub.md) | ⬜ Not started | 02 | 06, 08, 04.0/09, 04.0/13, 04.0/15, 05.0/14, 06.0/02 |
| 05 | [ArgSpec, ArgsView and validation](05-argspec-argsview-validation.md) | ⬜ Not started | 01, 02 | 06, 03.0/05, 03.0/07, 03.0/08, 03.0/09, 03.0/10, 03.0/14, 03.0/15, 03.0/17, 04.0/11, 06.0/02 |
| 06 | [Descriptor and registry lookup](06-descriptor-and-registry-lookup.md) | ⬜ Not started | 03, 04, 05 | 07, 03.0/05, 03.0/07, 03.0/08, 03.0/09, 03.0/10, 03.0/14, 03.0/15, 03.0/17, 04.0/08, 05.0/04, 05.0/12, 06.0/02 |
| 07 | [Execute pipeline and authorization](07-execute-pipeline-and-authorization.md) | ⬜ Not started | 06 | 08, 09, 10, 03.0/05, 03.0/07, 03.0/08, 03.0/09, 03.0/10, 03.0/14, 03.0/15, 03.0/17, 04.0/02, 04.0/09, 04.0/13, 05.0/04, 05.0/12, 05.0/14, 06.0/02, 06.0/18 |
| 08 | [dry_run, async and job-submitter seam](08-dry-run-async-and-job-seam.md) | ⬜ Not started | 04, 07 | 13, 05.0/04 |
| 09 | [Audit sink](09-audit-sink.md) | ⬜ Not started | 07 | 13, 03.0/18 |
| 10 | [command.list built-in](10-command-list-builtin.md) | ⬜ Not started | 07 | 11, 03.0/18, 04.0/10 |
| 11 | [Catalog and help renderers](11-catalog-and-help-renderers.md) | ⬜ Not started | 10 | 12, 04.0/13 |
| 12 | [Catalog dump tool and drift test](12-catalog-dump-tool-and-drift-test.md) | ⬜ Not started | 11 | 13 |
| 13 | [Error-code coverage and include hygiene](13-error-code-coverage-and-include-hygiene.md) | ⬜ Not started | 08, 09, 12 | none |

The catalog generator is subtasks 11 (renderers) and 12 (dump tool and drift
test); subtask 03 is the request envelope parser. All cross-task references
use the current numbers.

### Dependency graph

```text
01 scanner ---------+--> 03 request parser --+
                    |                        |
02 error table -----+--> 04 response/job ----+--> 06 descriptor/lookup --> 07 execute
                    |                        |                              |
01 ---> 05 argspec -+------------------------+       +----------+-----------+----------+
                                                      v          v                      v
                                              08 dry-run/    09 audit            10 command.list
                                                 async/job        |                     |
                                                      |           |                11 renderers
                                                      |           |                     |
                                                      |           |             12 dump tool + drift
                                                      +-----------+---------+-----------+
                                                                            v
                                                                 13 coverage + hygiene
```

04 depends on 02 only (it needs `CommandError`), 05 depends on 01 only.
01, 02 can start in parallel; 03, 04, 05 can then run in parallel.

## Key constraints

- No JSON library (CLAUDE.md, milestone C9). Only `src/commands/json_scan.cpp`
  scans JSON text; everything else goes through it or `core/utils.h`.
- `include/url_shortener/commands/` must not include Beast, Boost.Asio,
  Boost.ProgramOptions, FTXUI, `url_shortener/http/` or `url_shortener/storage/`
  headers. Enforced by a test in subtask 13.
- The error/status/exit-code mapping exists exactly once (subtask 02); REST and
  CLI read it later (Task 04.0). Existing `link_handlers.cpp` /
  `link_command_dispatch.cpp` mappings are not edited in this task.
- C++17 only; naming per `.clang-tidy` (lower_case functions, members and
  enumerators, `m_` prefix for private members). Type names `CommandRegistry`,
  `CommandDescriptor`, ... follow ADR 0002 and the other milestone documents.
- Every new `.cpp` is added to `COMMON_CPP_SOURCES` in `sources.cmake`
  (only that list feeds `url_shortener_common`); every new test goes into the
  new `COMMANDS_UNIT_SOURCES` list and is registered with
  `register_labeled_cpp_tests(COMMANDS_UNIT_SOURCES "unit")` in `CMakeLists.txt`.
- Nothing in this task touches the redirect fast path (C10) or any REST/CLI
  behavior (C3, C4).

## Exit criteria

1. `ctest --test-dir cmake-build -R "^commands__" --output-on-failure` passes
   (14 test executables, label `unit`; subtask 13 owns the last two).
2. `CommandRegistry::execute` implements the documented order
   (lookup, permission, arg validation, options, handler, audit) and each
   step has a failing-case test.
3. Every `error.code` in [docs/commands/README.md](/docs/commands/README.md)
   section 1 is produced by at least one test and round-trips through
   `serialize_response` / `parse_response`.
4. `command_catalog_dump` output matches the generated block in
   `docs/commands/README.md` (drift test green); adding a descriptor without
   regenerating fails the test.
5. `ctest --test-dir cmake-build -L unit` shows no regressions in the
   pre-existing suites.

## Risks

- Scanner correctness (escapes, depth, duplicates) is security-relevant input
  handling; subtask 01 carries the fuzz-style table tests and a depth cap.
- ADR 0002 type names are CamelCase while `.clang-tidy` requires lower_case;
  the repository already deviates (`LinkCommandService`). The task follows the
  ADR for type names and `.clang-tidy` for everything else.
- The catalog hand-written sections 3.1-3.6 and the generated block will
  overlap until Task 08.0 decides which one stays (see subtask 12).
