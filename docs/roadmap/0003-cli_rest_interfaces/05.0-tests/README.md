# Task 05.0 - Tests

**Parent milestone:** [Milestone 0003 - CLI & REST Command Interfaces](/docs/roadmap/0003-cli_rest_interfaces/plan.md)
**Status:** ⬜ Not started
**Depends on:** [01.0](/docs/roadmap/0003-cli_rest_interfaces/01.0-command-layer-completion/README.md), [02.0](/docs/roadmap/0003-cli_rest_interfaces/02.0-cli-argument-parsing/README.md), [03.0](/docs/roadmap/0003-cli_rest_interfaces/03.0-cli-dispatch-and-lifecycle/README.md), [04.0](/docs/roadmap/0003-cli_rest_interfaces/04.0-cli-output-and-error-contract/README.md)

## Scope

`link create`/`get`/`stats` already have unit coverage
(`tests/unit/app/01-12_link_command_service_*.cpp`), integration coverage
(`tests/integration/cli/01-10_*.py`), and e2e coverage
(`tests/e2e/scripts/sections/11-13_cli_*.sh`). Critically,
`tests/integration/cli/cli_integration_common.py`'s `_probe_cli_support`
currently **self-skips** that whole integration suite because the binary
doesn't yet exit promptly on `link create` - Task 03.0 removes that
precondition, so this task's first job is confirming the existing suite
actually runs (not skips) once 01.0-04.0 land, then extending the same
three layers to the six new commands.

## Subtasks

| # | Document | Status | Blocks |
|---|----------|--------|--------|
| 01 | [Unit tests for new command-service methods](01-unit-tests-for-new-command-service-methods.md) | ✅ Complete | none |
| 02 | [Unit tests for the CLI parser](02-unit-tests-for-cli-parser.md) | ✅ Complete | none |
| 03 | [Integration tests for remaining commands](03-integration-tests-for-remaining-commands.md) | ⬜ Not started | 04 |
| 04 | [E2E sections for remaining commands](04-e2e-sections-for-remaining-commands.md) | ⬜ Not started | none |

## Key constraints

- `_probe_cli_support`'s self-skip must be confirmed **not triggering**
  (i.e. the suite runs for real) as part of subtask 03 - a passing-by-skip
  suite is not acceptance evidence.
- Task 01.0's characterization tests (its own subtask 04) are a
  prerequisite gate for this task's REST non-regression claim, not
  duplicated here.
- New test files follow the existing numeric-prefix convention in their
  respective directories (`tests/unit/app/`, `tests/integration/cli/`,
  `tests/e2e/scripts/sections/`) and are registered in `CMakeLists.txt`
  (`sources.cmake` where applicable) per `CLAUDE.md`'s Tests section.
- Validation commands from `plan.md` must pass, including the combined
  label run (`unit|contract|integration|e2e`).
