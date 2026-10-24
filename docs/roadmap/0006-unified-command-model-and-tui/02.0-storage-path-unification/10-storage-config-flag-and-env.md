# 10 - --storage-config flag and env

**Parent task:** 02.0 Storage path unification
**State:** ⬜ Not started
**Depends on:** none
**Blocks:** 11, 03.0/17, 04.0/12, 06.0/19
**Estimated size:** M
**Suggested agent:** cpp-expert

## Objective

Let every mode select the storage configuration file: server mode and
`link <verb>` mode (and, by reusing the same helper, the future `cmd`, `tui`
and `serve-stdio` modes of Tasks 04.0, 05.0, 06.0). The path is carried in
`ServerConfig`; parsing only records it. Loading and validation happen in the
bootstrap (11), so `--help`, argument errors and the default (no flag) path
never touch the filesystem.

## Dependencies

- `ServerConfig` (`include/url_shortener/core/config.hpp`, verified).
- `CliParser::parse` and `parseLinkCommand` in `src/cli_parser.cpp` (verified):
  server options use Boost.ProgramOptions (`po::options_description`); the
  `link` branch strips nothing today and hands `verb_args` to strict per-verb
  parsers in `src/cli/link_command_args.cpp` that reject unknown flags.
- Environment-variable precedent: `SHORTENER_*` variables read with
  `std::getenv` near line 360-380 of `cli_parser.cpp`.
- A golden test pins the top-level help text byte for byte:
  `kTopLevelHelpGolden` in `tests/unit/cli/01_cli_parser_link_commands.cpp`
  (line ~1038-1054). Adding an option changes that text, so the golden must be
  updated in this subtask on purpose.

## Files affected

| Path | Create / modify | What changes |
|---|---|---|
| `include/url_shortener/core/config.hpp` | modify | `std::optional<std::string> storage_config_path;` in `ServerConfig` |
| `src/cli_parser.cpp` | modify | Server option `--storage-config`; env fallback; `link` branch extracts the global flag before verb parsing; help text |
| `tests/unit/cli/01_cli_parser_link_commands.cpp` | modify | Update `kTopLevelHelpGolden` (and the `link --help` text assertions if they compare whole text) |
| `tests/unit/cli/05_cli_storage_config_flag.cpp` | create | New flag tests |
| `sources.cmake` | modify | `CLI_UNIT_SOURCES` += `tests/unit/cli/05_cli_storage_config_flag.cpp` |

## Classes and functions

```cpp
// core/config.hpp (ServerConfig addition)
std::optional<std::string> storage_config_path;   ///< Path of the storage YAML; unset = legacy in-memory backend.

// src/cli_parser.cpp (file-local, reusable by later modes)
/// Removes "--storage-config <path>" / "--storage-config=<path>" from @p args
/// (any position) and stores it in @p config. Throws std::invalid_argument for
/// a missing/empty value or a repeated flag.
void extract_storage_config_flag(std::vector<std::string>& args, ServerConfig& config);
```

Behavior:

- Precedence: flag > environment variable `URL_SHORTENER_STORAGE_CONFIG` >
  unset. The env value is applied while loading shared configuration (same
  place as the `SHORTENER_*` variables), the flag overrides it.
- Server mode: new options group "Storage options" with
  `storage-config` (`po::value<std::string>()->value_name("FILE")`, text:
  "Path of the storage configuration YAML (metadata/cache/analytics/rate_limit
  backends). Default: in-memory."). Listed after the shortener options.
- `link <verb>`: `extract_storage_config_flag(verb_args, config)` runs before the
  per-verb parser, so `link get --slug x --storage-config /d/s.yaml` and the flag
  placed before the verb flags both work, and the strict per-verb parsers never
  see it. `link <verb> --help` output is unchanged except for one additional
  line in `buildLinkHelpText()` documenting the global flag.
- The path is stored verbatim (no `~` expansion, no existence check, no
  canonicalization at parse time; relative paths are resolved against the
  process working directory by the bootstrap, documented).
- The value is not secret but may reveal layout; it is never echoed into
  parse error messages beyond the flag name.

## Implementation steps

1. Add the `ServerConfig` field.
2. Add the file-local helper and call it from `parseLinkCommand`.
3. Add the server option and env fallback; wire into `cfg.storage_config_path`.
4. Update the help builders; regenerate the golden strings by running the test
   once and copying the new expected text, reviewing the diff by eye (only the
   storage option block and the one `link --help` line may differ).
5. Add the new tests; register; build; run the whole CLI unit set.

## Commands

```powershell
cmake --build cmake-build --target cli__01_cli_parser_link_commands cli__05_cli_storage_config_flag
ctest --test-dir cmake-build -R "^cli__0[1-5]_" --output-on-failure
```

## Tests

| Test file | Target | Label | Cases |
|---|---|---|---|
| `tests/unit/cli/05_cli_storage_config_flag.cpp` | `cli__05_cli_storage_config_flag` | unit | below |
| `tests/unit/cli/01_cli_parser_link_commands.cpp` | `cli__01_cli_parser_link_commands` | unit | golden update only; all other cases unchanged |

- Server mode: `--storage-config /x.yaml` -> `config.storage_config_path ==
  "/x.yaml"`; absent -> `nullopt`.
- Env fallback: `URL_SHORTENER_STORAGE_CONFIG=/e.yaml` -> used; flag + env ->
  flag wins (use a scoped `setenv`/`_putenv_s` helper; restore after, because
  Windows has no `setenv`).
- `link get --slug a --storage-config /x.yaml` and
  `link get --storage-config=/x.yaml --slug a`: payload parsed as before
  (`GetLinkQuery{slug,"a"}`) and path recorded.
- Missing value (`--storage-config` last, or followed by another `--flag`),
  empty value, repeated flag -> `std::invalid_argument` naming only the flag.
- All eight other verbs accept the flag (loop), unknown flags are still
  rejected by the strict per-verb parsers.
- `--help` / `link --help` include the flag text; byte golden updated.
- Absence of the flag leaves every pre-existing parser test unchanged.

## Constraints

- Parsing performs no I/O (C5 spirit: parse must stay side-effect free; `--help`
  must work without the file).
- Strictness of the per-verb parsers is preserved (the helper removes exactly
  the one flag).
- Default behavior unchanged: no flag + no env -> `nullopt`.
- Boost.ProgramOptions stays confined to `cli_parser.cpp`; the extraction helper
  uses plain string handling.
- Windows: path with drive letter and backslashes is accepted verbatim.

## Success criteria

1. `cli__05_cli_storage_config_flag` passes and the updated golden in
   `cli__01_...` passes.
2. `git diff` of the golden shows only the new option block and the one
   `link --help` line.
3. All pre-existing `cli__*` unit tests pass without other edits.

## Out of scope / risks

- Reading/validating the YAML (11); the future `--read-only`, `--yes`,
  `--theme` flags (Tasks 04.0-06.0 reuse `extract_storage_config_flag`).
- Env var naming: `URL_SHORTENER_STORAGE_CONFIG` follows
  `URL_SHORTENER_ANALYTICS_HASH_SALT` (CLAUDE.md); the older `SHORTENER_*`
  prefix is used for shortener behavior settings. Flag for review if a different
  name is preferred.
- A YAML file path on the command line is visible in process listings; the YAML
  itself holds DSNs/passwords, so permissions on that file are an operator
  concern (document in subtask 15).
