# Task 02.0 - CLI argument parsing

**Parent milestone:** [Milestone 0003 - CLI & REST Command Interfaces](/docs/roadmap/0003-cli_rest_interfaces/plan.md)
**Status:** ⬜ Not started
**Depends on:** [01.0 Command layer completion](/docs/roadmap/0003-cli_rest_interfaces/01.0-command-layer-completion/README.md) (needs the DTOs it adds)

## Scope

`CliParser`/`ParseResult` (`include/url_shortener/cli/cli_parser.h`,
`src/cli_parser.cpp`) currently produces only a `ServerConfig` for server
startup. This task extends `ParseResult` to optionally carry a recognized
`link <verb>` command plus its parsed DTO (one of the ten from the
inventory table in `plan.md`), while leaving every existing server flag's
parsing behavior untouched. This task adds parsing only - no dispatch, no
storage access, no process-lifecycle change (that is Task 03.0).

## Subtasks

| # | Document | Status | Blocks |
|---|----------|--------|--------|
| 01 | [ParseResult command variant](01-parse-result-command-variant.md) | ✅ Complete | 02, 03 |
| 02 | [Flag mapping for existing commands (create/get/stats)](02-flag-mapping-for-existing-commands.md) | ✅ Complete | 04 |
| 03 | [Flag mapping for new commands (update/delete/enable/disable/restore/preview)](03-flag-mapping-for-new-commands.md) | ⬜ Not started | 04 |
| 04 | [Help text and usage](04-help-text-and-usage.md) | ⬜ Not started | none |

## Key constraints

- Every flag `CliParser::parse()` accepts today (`--http-port`,
  `--tls-*`, `--shortener-*`, `--analytics-*`, request-limit flags) keeps
  parsing identically - covered by regression tests in Task 05.0, subtask
  02, but each subtask here must not touch that code path.
- The command DTOs produced here are exactly the DTO types from
  `app::LinkCommandService` (existing ones for create/get/stats, new ones
  from Task 01.0) - no CLI-local duplicate struct.
- `ParseResult::help_requested` keeps its current meaning for server-mode
  `--help`; `link <verb> --help` is a separate, additive concern (subtask
  04).
- No storage/repository access happens during parsing - `CliParser` stays a
  pure argv-to-DTO mapper, matching its current single responsibility.
