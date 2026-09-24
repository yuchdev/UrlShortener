# 03 - Flag mapping for new commands (update/delete/enable/disable/restore/preview)

**Parent task:** 02.0 CLI argument parsing
**State:** ✅ Complete
**Depends on:** 01, and [Task 01.0 subtask 01](/docs/roadmap/0003-cli_rest_interfaces/01.0-command-layer-completion/01-define-update-delete-lifecycle-preview-dtos.md) (needs the new DTOs)
**Blocks:** 04

## Objective

Implement argv-to-DTO mapping for the six commands added by Task 01.0:

- `link update --slug <slug> [--enabled true|false] [--expires-at <rfc3339>|clear] [--tags a,b,c] [--metadata k=v,...] [--campaign-name ...] [--clear-campaign]`
- `link delete --slug <slug>`
- `link enable --slug <slug>`
- `link disable --slug <slug>`
- `link restore --slug <slug>`
- `link preview --slug <slug>`

The hardest part is `link update`'s field-presence semantics: a flag must
be able to represent "not given" (leave field untouched), "given as
explicit clear" (e.g. `--expires-at clear`), and "given with a value" -
matching the three-state `UpdateLinkCommand` shape from Task 01.0 subtask
01 exactly.

## Files to modify

- `include/url_shortener/cli/link_command_args.hpp` /
  `src/cli/link_command_args.cpp` (added in subtask 02) - add parsing
  functions for the six new verbs.
- `src/cli_parser.cpp` - wire the six new verbs into dispatch.

## Tests

Extend `tests/unit/cli/01_cli_parser_link_commands.cpp`:
- `link update --slug foo --enabled false` sets only `enabled` in
  `UpdateLinkCommand`, leaving other optional fields at "absent".
- `link update --slug foo --expires-at clear` sets the explicit-null case
  for `expires_at`.
- `link update --slug foo --tags a,b` parses a comma-separated list into
  `std::vector<std::string>`.
- `link delete`/`enable`/`disable`/`restore`/`preview --slug foo` each
  produce their one-field DTO; missing `--slug` is a parse error for all
  five.

## Constraints

- The three-state field semantics for `link update` must exactly match
  `UpdateLinkCommand`'s double-`optional` design from Task 01.0 - do not
  simplify to "flag present = value given" and lose the explicit-clear
  case, or PATCH-equivalent CLI behavior silently diverges from the REST
  PATCH semantics.
- Comma-separated list/map parsing (`--tags`, `--metadata`) is validated the
  same way the REST body parser validates them (reuse
  `validateTags`/`validateMetadata` inside `LinkCommandService`, not a
  separate CLI-side validator).

## Success criteria

- [ ] All six new verbs parse into their Task-01.0 DTOs with correct
      field-presence semantics.
- [ ] `link update`'s explicit-clear vs. absent vs. value-given cases are
      each covered by a distinct test.
- [ ] New/extended unit tests pass.
