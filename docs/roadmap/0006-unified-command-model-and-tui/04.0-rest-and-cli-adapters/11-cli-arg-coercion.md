# 11 - CLI `--arg <name>=<value>` coercion

**Parent task:** 04.0 REST and CLI adapters
**State:** ⬜ Not started
**Depends on:** 01 (args builder), 01.0/05 (`ArgSpec`, `ArgType`)
**Blocks:** 13
**Estimated size:** M
**Suggested agent:** cpp-expert

## Objective

Turn the repeated shell-friendly form `--arg <name>=<value>` into the typed `args` JSON object a command expects, using the command's own
`ArgSpec` list as the schema. This is the "flags compiled from `ArgSpec`" half of the milestone for the generic `cmd` subcommand: one definition
(the descriptor) decides how `limit=10` becomes the integer `10` and `enabled=true` becomes a boolean. It is a pure library; parsing of the CLI
tokens and execution are subtasks 12 and 13.

## Dependencies

- `ArgSpec {name, type, required, nullable, enum_values, min, max, fields, ...}` and `ArgType {string, integer, boolean, enumeration, timestamp, string_list, string_map, object}` (01.0/05, specified).
- `commands::args_builder` (subtask 01).
- Design rules decided here (flag for review):
  1. **Scalars:** `string`, `enumeration`, `timestamp` -> JSON string as given (validation by the registry). `integer`: the value must match `^-?[0-9]{1,18}$` and is emitted as a number; otherwise it is emitted as a *string* so the registry reports `<arg>: wrong_type`. `boolean`: exactly `true` or `false`, otherwise emitted as a string (registry rejects).
  2. **Lists:** `string_list` args are built by repeating the name: `--arg tags=a --arg tags=b` -> `["a","b"]`. `--arg tags=` is a list with one empty string; empty lists and empty maps are expressed with `--json` (documented in the help text).
  3. **Maps and objects** use dotted names: `--arg metadata.utm=email` -> `{"metadata":{"utm":"email"}}`, `--arg campaign.name=spring` -> `{"campaign":{"name":"spring"}}`. The part before the first `.` must be an arg of type `string_map` or `object`; otherwise the pair is passed through as a plain string name (the registry then reports it as unknown).
  4. **Null:** for an arg with `nullable = true`, the exact value `null` is JSON null (clears a field). For non-nullable args `null` is an ordinary string.
  5. **Unknown names and unknown commands:** names that match no `ArgSpec` are emitted as strings, so the registry produces the single `invalid_field` error; the coercer never decides validity.
  6. **Duplicates:** a repeated name is an error unless the arg is a `string_list`; the same dotted map entry twice is an error. The error vocabulary is listed below; the CLI prints it and exits 2.
  7. **Arg names** must match `[a-z][a-z0-9_]*`; anything else (`Slug`, `a-b`, empty) is rejected before building (protects `args_builder`'s programming-error throw).

## Files affected

| Path | Action | What changes |
|---|---|---|
| `include/url_shortener/cli/arg_coercion.hpp` | create | Declarations. No Boost.ProgramOptions or Beast includes. |
| `src/cli/arg_coercion.cpp` | create | Implementation. Add to `COMMON_CPP_SOURCES`. |
| `tests/unit/cli/06_cli_arg_coercion.cpp` | create | Unit tests. |
| `sources.cmake` | modify | `COMMON_CPP_SOURCES` += .cpp; `CLI_UNIT_SOURCES` += the test (target `cli__06_cli_arg_coercion`). |

## Classes and functions

```cpp
namespace url_shortener::cli {

struct arg_pair { std::string name; std::string value; };   // as typed after `--arg`

/// Builds the args object text. @p specs may be empty (unknown command): everything is emitted as strings.
/// On failure returns false and sets @p error to one of the fixed strings: bad_pair (no '='), bad_name,
/// duplicate_name, map_entry_empty, map_entry_duplicate. Never echoes values.
bool coerce_args(const std::vector<commands::ArgSpec>& specs,
                 const std::vector<arg_pair>& pairs,
                 std::string* args_json,
                 std::string* error);

/// Splits "<name>=<value>" at the first '='; false when there is none or the name is empty.
bool split_arg_pair(std::string_view text, arg_pair* out);
}
```

## Implementation steps

1. Implement `split_arg_pair` and the name validation.
2. Implement `coerce_args` in two passes: group pairs by arg name (preserving first-seen order), then emit each group by its `ArgSpec::type`.
3. Write the unit tests (table-driven; compare the produced text and also re-scan it with `scan_object`).
4. Register in `sources.cmake`; build; run.

## Commands

```powershell
cmake --build cmake-build --target cli__06_cli_arg_coercion
ctest --test-dir cmake-build -R "^cli__06_cli_arg_coercion$" --output-on-failure
```

## Tests

| Test file | Target | Label | Cases |
|---|---|---|---|
| `tests/unit/cli/06_cli_arg_coercion.cpp` | `cli__06_cli_arg_coercion` | unit | each `ArgType` with a valid value (`limit=10` -> `10`, `enabled=true` -> `true`, `redirect_type=permanent`, `expires_at=2030-01-01T00:00:00Z`); integer edge cases (`0`, `-1`, `+1` -> string, `1.5` -> string, 19 digits -> string, empty -> string); boolean `True` and `1` -> string; `string_list` repeated, single, with commas (kept literal); `metadata.utm=x` and `campaign.name=y` dotted forms incl. two entries and a duplicated dotted entry -> `map_entry_duplicate`; dotted prefix that is not a map/object arg -> passed as a plain name; `nullable` arg with `null` -> JSON null, non-nullable with `null` -> string; value containing `=`, quotes, backslash, newline, UTF-8 (round-trips); no `=` -> `bad_pair`; `=x` and `Bad=1` -> `bad_name`; duplicate scalar name -> `duplicate_name`; empty `specs` -> all strings; hostile names never reach `args_builder` (no exception); the `link.create` example from the success criteria passes `validate_args` (links 01.0's validator). |

Manual check: none (exercised end-to-end in subtask 13).

## Constraints

- C1: types come from the descriptor; no per-command code in the CLI.
- C9: no JSON library; C++17; header free of Beast/Boost.ProgramOptions.
- The coercer never validates domain rules (URLs, slugs); it only chooses the JSON type. Rejections come from `validate_args` (single path).
- Error strings never include the supplied values (they may be sensitive in shell history).
- Naming per `.clang-tidy`; new `.cpp` in `COMMON_CPP_SOURCES`.

## Success criteria

1. `cli__06_cli_arg_coercion` passes.
2. For the `link.create` specs, the coerced text for `target_url=https://a.example`, `slug=a`, `tags=x`, `tags=y`, `metadata.k=v`, `campaign.name=n` passes `validate_args`.
3. No exception can escape `coerce_args` (the test feeds 1000 generated names).

## Out of scope / risks

- Shell quoting is the caller's problem; `--json` remains the escape hatch for empty lists/maps and complex values.
- The `+1` and `True` fallbacks to strings are intentional: the registry's `wrong_type` message is the one place that explains the failure.
