# 01 - Args object builder

**Parent task:** 04.0 REST and CLI adapters
**State:** ⬜ Not started
**Depends on:** 01.0/01 (`scan_object`, used by the test to prove the output is well-formed)
**Blocks:** 02, 03, 04, 05, 06, 07, 11, 14 (every adapter subtask that turns native input into an `args` object)
**Estimated size:** S
**Suggested agent:** cpp-expert

## Objective

Provide the one small helper both adapters use to assemble the `args` JSON object text that
`CommandRequest::args_json` carries, without adding a JSON library (milestone C9). REST (query
string and body fields), the CLI `cmd --arg k=v` path and the `link <verb>` flag compiler all
build their requests through it, so escaping and key rules exist exactly once.

## Dependencies

- Verified: `url_shortener::jsonString(const std::string&)` in `core/utils.h` produces correctly
  escaped JSON string literals. That header includes `boost/beast/http.hpp`, so it may only be included
  from the `.cpp`, never from `include/url_shortener/commands/` (01.0/13 hygiene rule).
- `CommandRequest` (01.0/03): `{v, id, command, args_json = "{}", options}`; `args_json` is raw JSON text
  of an object, stored verbatim and validated later by `validate_args` against the descriptor.
- `scan_object` (01.0/01) is used by the unit test only.

## Files affected

| Path | Action | What changes |
|---|---|---|
| `include/url_shortener/commands/args_builder.hpp` | create | Class declaration. |
| `src/commands/args_builder.cpp` | create | Implementation; add to `COMMON_CPP_SOURCES` in `sources.cmake`. |
| `tests/unit/commands/40_args_builder.cpp` | create | Unit tests. |
| `sources.cmake` | modify | `COMMON_CPP_SOURCES` += the .cpp; `COMMANDS_UNIT_SOURCES` += the test. |

Test numbering (plan.md "Test file prefixes"): Task 04.0 owns `tests/unit/commands/40-49`.

## Classes and functions

```cpp
namespace url_shortener::commands {

/// Builds a flat JSON object text, member by member, in insertion order.
class args_builder
{
public:
    args_builder& add_string(std::string_view name, std::string_view value);
    args_builder& add_bool(std::string_view name, bool value);
    args_builder& add_int(std::string_view name, long long value);
    args_builder& add_null(std::string_view name);
    /// @p json_token must already be a valid JSON value (object, array, number...). Not validated here.
    args_builder& add_raw(std::string_view name, std::string_view json_token);
    args_builder& add_string_list(std::string_view name, const std::vector<std::string>& values);
    args_builder& add_string_map(std::string_view name, const std::map<std::string, std::string>& values);

    bool empty() const;
    bool has(std::string_view name) const;
    std::string str() const;          // "{}" when empty, else {"a":..,"b":..}

private:
    std::vector<std::pair<std::string, std::string>> m_members;   // name -> already-serialized value
};
}
```

Rules: member names must match `[a-z][a-z0-9_]*` (all catalog arg names do); a bad or duplicate name is a
**programming error** and throws `std::invalid_argument` (builders are fed with literals). Values are escaped
with `jsonString` in the `.cpp`. `add_string_map` emits keys in sorted order (deterministic text, same as the
sorted metadata emitted by `serializeLinkViewJson`). No allocation limits beyond the caller's: the REST/CLI
callers bound input size before building (request hardening / argv size).

## Implementation steps

1. Write the header and implementation (`std::vector` of pairs, joined in `str()`).
2. Write the unit test; every `str()` result is parsed back with `scan_object` and compared member by member.
3. Register in `sources.cmake`; build; run.

## Commands

```powershell
cmake --build cmake-build --target commands__40_args_builder
ctest --test-dir cmake-build -R "^commands__40_args_builder$" --output-on-failure
```

Multi-config generators: `--config Debug` / `-C Debug`.

## Tests

| Test file | Target | Label | Cases |
|---|---|---|---|
| `tests/unit/commands/40_args_builder.cpp` | `commands__40_args_builder` | unit | empty -> `{}`; each `add_*` once; insertion order kept; string with quotes, backslash, newline, tab, control char `\x01`, non-ASCII UTF-8 round-trips through `scan_object` + `decode_json_string`; empty string value; `add_null`; `add_string_list` empty and with escapes; `add_string_map` sorted and with hostile keys; `add_raw` with an object token; `has`/`empty`; bad names (`""`, `Slug`, `a-b`, `1x`, `a b`) throw `std::invalid_argument`; duplicate name throws; a 64 KiB value builds without truncation. |

Manual check: none.

## Constraints

- C9: no JSON library; C++17 only; naming per `.clang-tidy` (`lower_case` class name matches 01.0's style).
- The header must not include `core/utils.h`, Beast, storage or http headers.
- Untrusted input is escaped, never concatenated raw. `add_raw` is for tokens produced by already-validated sources only
  (documented in Doxygen; the REST layer passes tokens it has just scanned).
- Every new `.cpp` in `COMMON_CPP_SOURCES`.

## Success criteria

1. `commands__40_args_builder` passes, including the hostile-string round trip.
2. No other file in `src/http` or `src/cli` assembles `args` JSON by hand (checked by grep in subtasks 03-07 and 14: `grep -n "\"{\\\\\"" src/http src/cli` finds no hand-built args).
3. `grep -n "include" include/url_shortener/commands/args_builder.hpp` shows only standard headers.

## Out of scope / risks

- Nested object construction beyond `add_raw` (the `campaign` object is built by the REST/CLI code from six known string members using a second `args_builder` and `add_raw`).
- Parsing; this class only writes.
