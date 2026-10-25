# 14 - `link <verb>` flags to `args` JSON (converters and ArgSpec coverage test)

**Parent task:** 04.0 REST and CLI adapters
**State:** ⬜ Not started
**Depends on:** 01 (args builder), 03.0/11 (the nine commands registered, so their `ArgSpec`s exist)
**Blocks:** 15
**Estimated size:** M
**Suggested agent:** cpp-expert

## Objective

Give the existing `link <verb>` sugar a way to produce the `args` object of the matching command **without changing a single flag**: the current
`parse*Args` functions keep owning the flag grammar and keep producing the shared `app::` DTOs; this subtask adds pure DTO -> `args` JSON converters and a test that proves, against the
registered `ArgSpec`s, that every command argument is reachable from some flag and that no converter emits an unknown argument. That coverage test is the executable form of "one source of
truth for flag names".

## Dependencies

- Verified flag grammar (`src/cli/link_command_args.cpp`, `include/url_shortener/cli/link_command_args.hpp`) that must not change (C4):
  - `create`: `--url` (required), `--slug`, `--redirect-type temporary|permanent`, `--expires-at`, `--enabled BOOL`, `--tag` (repeatable), `--metadata K=V` (repeatable), `--campaign-name/-source/-medium/-term/-content/-id`, plus config flags `--base-domain`, `--allow-private-targets`.
  - `get` / `preview`: exactly one of `--slug`, `--id`.
  - `update`: `--slug` plus optional `--enabled`, `--expires-at <RFC3339|clear>`, `--tags A,B,C` (`""` = empty list), `--metadata K=V,...` (`""` = empty map), `--campaign-*`, `--clear-campaign`.
  - `delete|enable|disable|restore`: `--slug`.
  - `stats`: `--slug --from --to --bucket`, all required (stricter than the command, where the range is optional).
- **Design deviation (flag for review):** ADR 0002 says verb flags are "compiled from the command's `ArgSpec`". That cannot reproduce today's grammar: `--expires-at clear`, `--clear-campaign`, comma lists, six `--campaign-*` flags and two config-only flags have no `ArgSpec` representation, and C4 forbids changing the flags. So flags stay hand-parsed into the existing DTOs, the DTOs are converted to `args`, and drift is prevented by the coverage test below instead of by generation.
- DTO shapes (verified, `link_command_service.hpp`): `CreateLinkCommand {target_url, slug, redirect_type, expires_at, enabled, tags, metadata, campaign}`, `GetLinkQuery {by, value}`, `UpdateLinkCommand` (double-optionals), `DeleteLinkCommand`/`SetLinkEnabledCommand`/`RestoreLinkCommand {slug,...}`, `GetLinkStatsQuery {slug, from, to, bucket}`.
- `redirectTypeToString` is in `core/utils.hpp` (Beast-including): call it only from the `.cpp`.

## Files affected

| Path | Action | What changes |
|---|---|---|
| `include/url_shortener/cli/link_args_json.hpp` | create | Converter declarations (no Beast include). |
| `src/cli/link_args_json.cpp` | create | Implementations. Add to `COMMON_CPP_SOURCES`. |
| `tests/unit/cli/09_link_args_json.cpp` | create | Converter + coverage tests. |
| `sources.cmake` | modify | `COMMON_CPP_SOURCES` += .cpp; `CLI_UNIT_SOURCES` += the test (target `cli__09_link_args_json`). |

`link_command_args.cpp/.hpp`, `cli_parser.cpp` are **not** modified.

## Classes and functions

```cpp
namespace url_shortener::cli {
std::string create_args_json(const app::CreateLinkCommand& command);   // omits empty tags/metadata/absent options
std::string get_args_json(const app::GetLinkQuery& query);             // {"slug":..} or {"id":..}; also used for preview
std::string update_args_json(const app::UpdateLinkCommand& command);   // absent / null / value preserved
std::string slug_args_json(const std::string& slug);                   // delete, enable, disable, restore
std::string stats_args_json(const app::GetLinkStatsQuery& query);      // slug, from, to, bucket (all given)

/// Command name for a verb: create -> "link.create", del -> "link.delete", enable -> "link.enable", ...
std::string_view command_name_for(LinkCliVerb verb);
}
```

Mapping rules: `CreateLinkCommand::target_url` -> `target_url`; `redirect_type` -> `"temporary"|"permanent"`; `campaign` -> object with only the present members; `UpdateLinkCommand::expires_at`
outer-absent -> omitted, inner-empty -> `null`, value -> string; same for `campaign`; `tags`/`metadata` present (even empty) -> list/map; all JSON built with `commands::args_builder`.

## Implementation steps

1. Implement the converters and `command_name_for` (exhaustive `switch` over `LinkCliVerb`, no `default`, so a new verb is a compile prompt).
2. Write the converter tests (each DTO shape).
3. Write the coverage test using a real registry: `register_link_commands(registry, service)` over `FakeLinkStore` (`#include "../app/app_test_fakes.hpp"`).
4. Register; build; run.

## Commands

```powershell
cmake --build cmake-build --target cli__09_link_args_json
ctest --test-dir cmake-build -R "^cli__09_link_args_json$" --output-on-failure
```

## Tests

| Test file | Target | Label | Cases |
|---|---|---|---|
| `tests/unit/cli/09_link_args_json.cpp` | `cli__09_link_args_json` | unit | **converters:** create minimal; create with every field incl. two tags, two metadata pairs, campaign with one and with six members; update with each tri-state combination (`expires_at` absent / cleared / value, `campaign` absent / cleared / value, `tags` empty list, `metadata` empty map); get by slug and by id; slug-only verbs; stats; hostile strings (quotes, newline, UTF-8) round-trip through `scan_object`; `command_name_for` for all nine verbs. **coverage against ArgSpec (registered `link.*` descriptors):** for every verb, the converter output built from a "maximal" DTO passes `validate_args` against the descriptor's `ArgSpec`s; the set of keys it can emit equals the descriptor's arg names (create: all 8; update: all 6; get/preview: both `slug` and `id` across the two variants; stats: all 4; lifecycle: `slug`); a hand-listed exemption set documents args without a flag (none expected); a descriptor that gains a new arg makes the test fail until a flag/converter is added. **end-to-end through the parser:** for each verb take a real argv, run `CliParser::parse` then the converter, and compare with the expected JSON text. |

Manual check: none.

## Constraints

- C4: no change to flag names, parsing, help text, stdout or exit codes of `link <verb>`.
- C1: converters contain no validation beyond choosing JSON types; validation stays in the registry/service.
- Untrusted input: all strings pass through `args_builder` escaping.
- C++17; naming per `.clang-tidy`; header free of Beast; new `.cpp` in `COMMON_CPP_SOURCES`.

## Success criteria

1. `cli__09_link_args_json` passes, including the ArgSpec coverage cases.
2. `git diff --stat` shows `link_command_args.cpp`, `link_command_args.hpp` and `cli_parser.cpp` untouched.
3. Adding a throwaway `ArgSpec` to `link.update` in a local experiment makes the coverage test fail (documented in the PR description).

## Out of scope / risks

- Generating `link --help` from `ArgSpec`s (help stays hand-written; ADR 0002's help generator serves `cmd <name> --help`).
- The stricter `link stats` flag requirement (all four) is kept; the command accepts an optional range. The sugar is stricter than the command by design (C4).
