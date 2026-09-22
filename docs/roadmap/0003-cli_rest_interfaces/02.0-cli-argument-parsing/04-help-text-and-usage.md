# 04 - Help text and usage

**Parent task:** 02.0 CLI argument parsing
**State:** ✅ Complete
**Depends on:** 02, 03
**Blocks:** none

## Objective

Extend CLI help so `link --help` and `link <verb> --help` describe the new
subcommands, while `--help` with no `link` token keeps printing exactly
today's server-flag usage text unchanged.

## Files to modify

- `src/cli_parser.cpp` - add a `link`-scoped help branch that sets
  `ParseResult::help_requested`/`help_text` the same way the existing
  top-level `--help` does, listing all ten verbs and their flags.

## Tests

Extend `tests/unit/cli/01_cli_parser_link_commands.cpp`:
- `argv = {"url_shortener", "--help"}` produces byte-identical
  `help_text` to before this milestone (regression guard).
- `argv = {"url_shortener", "link", "--help"}` sets `help_requested = true`
  and lists all ten verbs.
- `argv = {"url_shortener", "link", "create", "--help"}` describes
  `create`'s specific flags.

## Constraints

- Top-level `--help` output is unchanged - verified by an exact-string
  comparison test, not a substring check, so any accidental edit is caught.

## Success criteria

- [ ] `link --help` and `link <verb> --help` produce useful, accurate usage
      text for all ten commands.
- [ ] Top-level `--help` (server mode) output is byte-identical to before
      this task.
- [ ] Manual QA test case 11 in `docs/testing/cli_link_commands.md`
      ("`--help` still works in server mode") passes as-is.
