# 01 - ParseResult command variant

**Parent task:** 02.0 CLI argument parsing
**State:** ✅ Complete
**Depends on:** none (Task 01.0's DTOs are needed by subtask 03, not this one)
**Blocks:** 02, 03

## Objective

Give `ParseResult` a way to carry a recognized `link <verb>` command instead
of (or alongside, for `--help`) a `ServerConfig`, and give `CliParser` the
top-level dispatch that recognizes the first positional token as `link`
before falling through to today's server-flag parsing.

## Files to modify

- `include/url_shortener/cli/cli_parser.h`:

```cpp
enum class LinkCliVerb
{
    create, get, update, del, enable, disable, restore, preview, stats
};

struct LinkCliCommand
{
    LinkCliVerb verb;
    // one of the app:: DTOs, populated by subtasks 02/03; represented here
    // as a discriminated holder rather than one field per DTO type, e.g.:
    std::variant<
        app::CreateLinkCommand, app::GetLinkQuery, app::UpdateLinkCommand,
        app::DeleteLinkCommand, app::SetLinkEnabledCommand,
        app::RestoreLinkCommand, app::GetLinkStatsQuery> payload;
};

struct ParseResult
{
    ServerConfig config;
    bool help_requested = false;
    std::string help_text;
    std::optional<LinkCliCommand> command;  // set only for `link <verb>` invocations
};
```

- `src/cli_parser.cpp` - `CliParser::parse()` inspects `argv[1]`; if it is
  `"link"`, delegate to the new per-verb parsing (subtasks 02/03) and
  populate `ParseResult::command`, skipping all `ServerConfig` option
  parsing entirely. Otherwise, fall through to the existing
  Boost.ProgramOptions server-flag parsing completely unchanged.

## Tests

Add `tests/unit/cli/01_cli_parser_link_commands.cpp` (new directory,
register in `CMakeLists.txt`):
- `argv = {"url_shortener", "link", "create", ...}` sets
  `command->verb == LinkCliVerb::create` and leaves `config` at
  default/unused.
- `argv = {"url_shortener", "--http-port", "9090"}` (no `link` token) leaves
  `command == std::nullopt` and parses `config` exactly as before.
- Unknown verb after `link` throws `std::invalid_argument`, matching the
  existing "unknown argument" behavior for malformed server flags.

## Constraints

- C8 (`plan.md`): every existing server flag keeps parsing identically when
  `argv[1] != "link"`.
- The `link` token is recognized only as the very first positional
  argument, so a server flag value that happens to equal the string `link`
  elsewhere on the command line is unaffected.

## Success criteria

- [ ] `ParseResult::command` exists and is `std::nullopt` for every
      existing server-flag invocation.
- [ ] `CliParser::parse()` recognizes `argv[1] == "link"` and short-circuits
      server-flag parsing.
- [ ] New unit test passes; all pre-existing `CliParser` tests (if any)
      remain green.
