# 02 - Unit tests for the CLI parser

**Parent task:** 05.0 Tests
**State:** ✅ Complete
**Depends on:** [Task 02.0](/docs/roadmap/0003-cli_rest_interfaces/02.0-cli-argument-parsing/README.md)
**Blocks:** none

## Objective

Same audit role as subtask 01, but for `tests/unit/cli/01_cli_parser_link_commands.cpp`
(added across Task 02.0's four subtasks) - confirm every verb, every flag
combination, every error case (missing required flag, mutually exclusive
flags, unknown verb) has a test, and that the server-flag regression tests
(C8) are present and passing.

## Files to modify

- `tests/unit/cli/01_cli_parser_link_commands.cpp` - fill any gaps.

## Tests

```bash
ctest --test-dir cmake-build -R "cli_parser" -L unit --output-on-failure
```

## Constraints

- Includes an explicit regression test asserting every pre-existing server
  flag (`--http-port`, `--tls-*`, `--shortener-*`, `--analytics-*`,
  request-limit flags) still parses to the same `ServerConfig` values as
  before this milestone - this is the concrete enforcement of C8.

## Success criteria

- [ ] All ten verbs and their flag combinations are covered.
- [ ] A server-flag regression test exists and passes.
- [ ] All CLI-parser unit tests pass under `ctest -L unit`.
