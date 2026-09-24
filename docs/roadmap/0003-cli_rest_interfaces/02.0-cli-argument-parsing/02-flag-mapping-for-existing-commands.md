# 02 - Flag mapping for existing commands (create/get/stats)

**Parent task:** 02.0 CLI argument parsing
**State:** ✅ Complete
**Depends on:** 01
**Blocks:** 04

## Objective

Implement argv-to-DTO mapping for the three commands that already have a
`LinkCommandService` method: `link create`, `link get`, `link stats` -
reusing the flag names already drafted in `docs/testing/testplans/cli.md`
(corrected/finalized in Task 06.0, but usable as the starting shape now):

- `link create --url <url> [--slug <slug>]`
- `link get --slug <slug>` / `link get --id <id>`
- `link stats --slug <slug> --from <epoch> --to <epoch> --bucket hour|day|week`

## Files to add

- `include/url_shortener/cli/link_command_args.hpp` - per-verb argv parsing
  functions, e.g. `app::CreateLinkCommand ParseCreateArgs(...)`,
  `app::GetLinkQuery ParseGetArgs(...)`, `app::GetLinkStatsQuery ParseStatsArgs(...)`.
- `src/cli/link_command_args.cpp` - implementations, using
  Boost.ProgramOptions per-verb option groups (consistent with the existing
  parser's library choice).

## Files to modify

- `src/cli_parser.cpp` - call into the new parsing functions for these three
  verbs.
- `sources.cmake` - register the new header/source.

## Tests

Extend `tests/unit/cli/01_cli_parser_link_commands.cpp`:
- `link create --url https://example.com/x --slug custom` produces
  `CreateLinkCommand{target_url="https://example.com/x", slug="custom"}`.
- `link get --slug foo` vs. `link get --id bar` set `GetLinkQuery::by`
  correctly; passing both or neither is a parse error.
- `link stats --slug foo --from 100 --to 200 --bucket day` maps fields
  verbatim into `GetLinkStatsQuery`.

## Constraints

- Field mapping targets the *existing* DTOs from
  `app::link_command_service.hpp` - no new struct is introduced here.
- Error messages for missing/conflicting flags (e.g. `get` given neither
  `--slug` nor `--id`) are clear enough to satisfy the manual QA checklist
  being corrected in Task 06.0.

## Success criteria

- [ ] `link create`/`get`/`stats` argv parse into the exact existing DTOs.
- [ ] Mutually exclusive/required-flag validation matches the intent in
      `docs/testing/testplans/cli.md`.
- [ ] New/extended unit tests pass.
