# 05 - ArgSpec, ArgsView and validation

**Parent task:** 01.0 Command model core
**State:** ⬜ Not started
**Depends on:** 01.0/01, 01.0/02 (`CommandError`)
**Blocks:** 06, 03.0/05, 03.0/07, 03.0/08, 03.0/09, 03.0/10, 03.0/14, 03.0/15, 03.0/17, 04.0/11, 06.0/02
**Estimated size:** M
**Suggested agent:** cpp-expert

## Objective

Implement the argument schema (`ArgSpec`), the validated read-only accessor
(`ArgsView`) and `validate_args`, which checks one `args` JSON object against a
`std::vector<ArgSpec>`. This single schema later drives CLI flag compilation,
TUI form generation and the docs catalog (ADR 0002 section 2), so it must be
purely descriptive data plus validation; it knows nothing about transports.

## Dependencies

- 01.0/01 `scan_object`, `decode_json_string`, `json_kind`.
- 01.0/02 `CommandError` (`invalid_field`).
- `url_shortener::parseRfc3339Zulu` (verified in `core/utils.hpp`) for the
  `timestamp` type, called only from the `.cpp`.
- Catalog argument types: `string`, `bool`, `int`, `timestamp`, `list<string>`,
  `map<string,string>`, `object`, `enum`.

## Files affected

| Path | Create / modify | What changes |
|---|---|---|
| `include/url_shortener/commands/arg_spec.hpp` | create | `ArgType`, `ArgSpec`, `ArgsView`, `validate_args` |
| `src/commands/arg_spec.cpp` | create | Validation and accessors |
| `tests/unit/commands/05_arg_spec_validation.cpp` | create | Tests |
| `sources.cmake` | modify | `COMMON_CPP_SOURCES` += `src/commands/arg_spec.cpp`; `COMMANDS_UNIT_SOURCES` += test |

## Classes and functions

```cpp
namespace url_shortener::commands {

enum class ArgType
{
    string, integer, boolean, enumeration, timestamp,
    string_list, string_map, object
};

struct ArgSpec
{
    std::string name;
    ArgType type = ArgType::string;
    bool required = false;
    bool nullable = false;                 // explicit JSON null allowed (PATCH-style clear)
    std::vector<std::string> enum_values;  // for ArgType::enumeration
    std::optional<std::string> default_value; // raw JSON token, documentation + help only
    std::string doc;                       // one line
    std::optional<long long> min;          // integer bounds (inclusive)
    std::optional<long long> max;
    std::size_t max_length = 2048;         // string / list item / map value length cap
    std::size_t max_items = 64;            // list entries / map entries
    std::vector<ArgSpec> fields;           // members of ArgType::object (all optional strings in v1)
    std::string one_of_group;              // args with the same non-empty group: exactly one must be present
};

class ArgsView
{
public:
    bool has(std::string_view name) const;          // present (including explicit null)
    bool is_null(std::string_view name) const;      // present and JSON null
    std::optional<std::string> get_string(std::string_view name) const;   // decoded
    std::optional<long long> get_int(std::string_view name) const;
    std::optional<bool> get_bool(std::string_view name) const;
    std::optional<std::vector<std::string>> get_string_list(std::string_view name) const;
    std::optional<std::map<std::string, std::string>> get_string_map(std::string_view name) const;
    std::optional<ArgsView> get_object(std::string_view name) const;
    std::string raw(std::string_view name) const;   // raw token or ""
private:
    std::vector<json_member> m_members;
    friend bool validate_args(const std::vector<ArgSpec>&, std::string_view,
                              ArgsView*, CommandError*);
};

/// Checks @p args_json against @p specs. On success fills @p out (if non-null)
/// and returns true. On failure fills @p error (invalid_field, message =
/// "<arg-name>: <reason>" with reason from a fixed vocabulary: unknown,
/// missing, wrong_type, out_of_range, not_in_enum, too_long, too_many_items,
/// invalid_timestamp, one_of_violation) and returns false.
bool validate_args(const std::vector<ArgSpec>& specs, std::string_view args_json,
                   ArgsView* out, CommandError* error);
}
```

Behavior details:

- Unknown top-level key -> `unknown` (message names the key, truncated to 64
  chars and stripped of control characters).
- `required` missing -> `missing`; `nullable == false` and JSON `null` ->
  `wrong_type`. Absent vs explicit null are distinguishable through `has` /
  `is_null`; this is what lets Task 03.0 preserve `app::UpdateLinkCommand`
  PATCH semantics (`std::optional<std::optional<T>>`).
- `integer`: must be a JSON integer token (no fraction/exponent), fits in
  `long long`, within `[min, max]`.
- `enumeration`: string equal to one of `enum_values` (case-sensitive).
- `timestamp`: string accepted by `url_shortener::parseRfc3339Zulu`.
- `string_list`: array of strings, <= `max_items`, each <= `max_length`.
- `string_map`: flat object of string values, same caps (matches the
  `extractJsonFlatObjectStringField` contract used by REST today).
- `object`: nested object whose members must be declared in `fields`; every
  nested member is validated recursively (depth bounded by the scanner limit).
- `one_of_group`: among specs sharing a non-empty group, exactly one must be
  present, else `one_of_violation` (used by `link.get`/`link.preview`
  `slug` vs `id`).
- Strings are checked for embedded NUL after decoding (rejected as
  `wrong_type`).

Validation does **not** apply domain rules (URL validity, slug syntax, SSRF);
those stay in `LinkCommandService`, exactly once (milestone C1).

## Implementation steps

1. Write the header with Doxygen.
2. Implement `validate_args` with a small per-type checker table.
3. Implement `ArgsView` accessors over the retained `json_member` list; string
   accessors decode via `decode_json_string`.
4. Implement the `one_of_group` pass after per-arg checks.
5. Tests, sources, build, run.

## Commands

```powershell
cmake --build cmake-build --target commands__05_arg_spec_validation
ctest --test-dir cmake-build -R "^commands__05_arg_spec_validation$" --output-on-failure
```

## Tests

| Test file | Target | Label | Cases |
|---|---|---|---|
| `tests/unit/commands/05_arg_spec_validation.cpp` | `commands__05_arg_spec_validation` | unit | below |

- One accepting and one rejecting case per `ArgType`.
- Unknown key; missing required; wrong type for each type; `null` for
  non-nullable vs nullable; `has`/`is_null` distinction.
- Integer bounds: `min`, `max`, `min-1`, `max+1`, `9223372036854775808`
  (overflow), `1.0`, `1e2`, `"5"`.
- Enum: valid, wrong case, empty.
- Timestamp: `2026-01-01T00:00:00Z` ok; `2026-13-01T00:00:00Z`, missing `Z`,
  non-string -> `invalid_timestamp`.
- List/map caps: `max_items`, `max_items+1`, element longer than `max_length`.
- Nested `object` (shape of `link.create` `campaign`): known and unknown
  members, non-string member.
- `one_of_group`: none, one, two present.
- Messages never contain argument *values* (feed a secret-looking value in a
  wrongly typed arg and assert the message lacks it).
- `ArgsView` accessors return the decoded strings (escape and `\u` cases) and
  `std::nullopt` for absent/null.
- A spec set mirroring the `link.create` / `link.update` tables from the
  catalog validates their example bodies (catches schema/doc drift early).

## Constraints

- No domain validation here; no JSON library (uses 01); C++17 only.
- Defaults (`default_value`) are documentation only; the handler applies
  defaults, so validation never mutates `args`.
- Milestone contracts: C1 (schema is the single definition), C2, C9.
- Error messages must not echo values (C6 spirit; also avoids log injection).

## Success criteria

1. `commands__05_arg_spec_validation` passes.
2. Catalog example bodies for `link.create` and `link.update` validate
   against hand-built specs matching the catalog tables.
3. A grep of `arg_spec.cpp` finds no call into storage or `app/` services.

## Out of scope / risks

- Generating CLI flags (Task 04.0) and TUI forms (Task 06.0) from `ArgSpec`.
- `object` members are strings only in v1 (enough for `campaign`); typed
  nested members would extend `fields` without breaking callers.
- `default_value` as a raw token is deliberately untyped; revisit if the TUI
  form generator needs typed defaults.
