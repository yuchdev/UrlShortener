# 26 - FormModel property tests

**Parent task:** 07.0 Tests and conformance
**State:** ⬜ Not started
**Depends on:** 22; Task 06.0 (`FormModel`), Task 01.0 (`ArgInfo`/`ArgType`, `commands::validate_args`)
**Blocks:** 29, 34, 08.0/11
**Estimated size:** M
**Suggested agent:** testing-expert

> **Amended 2026-11-20 (as-built reconciliation).** The class is
> **`url_shortener::tui::FormModel`** (`include/url_shortener/tui/form_model.hpp`),
> not `FormBuilder`. It is **index-based**, not field-path-based:
> `set_text(i, text)`, `set_checked(i, bool)`, `set_choice(i, int)`,
> `set_member(i, m, text)`, `clear_field(i)`, `set_dry_run(bool)`,
> `validate() -> bool`, `to_args_json()`, `to_request(id)`,
> `prefill_from_result(result_json)`, `validation_summary()`, `fields()`. There
> is no `submit()`/`populate()`/`equivalentLine()` on the model (the equivalent
> line lives in `equivalent_cmd.hpp`) and **no fake clock**. The validator is
> `commands::validate_args` (`arg_spec.hpp`); the type tag is `ArgType`. 06.0
> shipped example-based tests (`tui__07_form_model_test`,
> `tui__07_field_parsers_test`, `tui__07_form_model_request_test`); the **real
> gap** is the seeded **property** generator. New stem: `tui__30` (free).

## Objective

Prove that `FormModel` turns **any** `ArgInfo` list into per-field editing state
whose `to_args_json()` output validates against the same `commands::validate_args`
the registry uses - for every `ArgType` (string, integer, boolean, enum,
string-list, string-map, object, timestamp), with required/optional/default/
nullable variations - using seeded pseudo-random specs plus a fixed edge-case
table.

## Coverage already delivered (06.0)

| Need | Delivered by (consumed) |
|---|---|
| example-based field parsing, request building | `tui__07_form_model_test`, `tui__07_field_parsers_test`, `tui__07_form_model_request_test` |
| widget rendering per type | `tui__08_form_widgets_render_test` |

## Dependencies

- Subtask 22 (`tui_test_support.hpp`). `FormModel` from 06.0/07; `ArgInfo`/`ArgType`/`CommandInfo` from `command_client.hpp`.
- 01.0: `commands::validate_args(specs, args_json)` (the pure validator `execute` calls) - reuse, never re-implement.

## Files affected

| Path | create/modify | What changes |
|---|---|---|
| `tests/unit/tui/30_form_model_property_test.cpp` | create | seeded property cases (stem `30` is free) |
| `tests/unit/tui/30_form_model_edge_cases.inc` | create | fixed edge-case specs/values (data file included by the test) |
| `tests/unit/tui/spec_generator.hpp` | create | `SpecGenerator` (seeded) producing `CommandInfo`/`ArgInfo` |
| `sources.cmake` | modify | Append test to `TUI_MODEL_UNIT_SOURCES` (owned by 06.0/01; `FormModel` is a model-library type) |

## Classes and functions

```cpp
namespace tui_test {
class SpecGenerator {                       // deterministic: std::mt19937 with seed from env FORM_PROP_SEED (default 20261019)
public:
    explicit SpecGenerator(std::uint32_t seed);
    // A CommandInfo whose args cover a random mix of ArgType values.
    url_shortener::tui::CommandInfo random_command(int max_fields = 8, int max_depth = 2);
    // A typing script (index -> set_text/set_checked/set_choice/set_member calls)
    // that is VALID for the command, and one INVALID variant.
    FieldScript valid_values(const url_shortener::tui::CommandInfo&);
    FieldScript invalid_values(const url_shortener::tui::CommandInfo&);  // wrong enum index, non-integer, malformed k=v, bad timestamp
};
}
```

Each script applies to a `FormModel` built from the generated `CommandInfo` via
the index-based mutators. Properties (N = 200 iterations; failure prints the seed
and the command JSON):

| # | Property |
|---|---|
| P1 | Valid values: `validate()` true and `commands::validate_args(specs, to_args_json())` reports no error |
| P2 | Missing required field: `validate()` false, the field's `error` names it, and `to_args_json()` omits/flags it (no bad value reaches the envelope) |
| P3 | Optional empty field: key absent in `to_args_json()` (never `""`, never `null`, except an explicit `clear_field` in edit mode, P8) |
| P4 | Default: an untouched field with a documented default is **omitted** (the handler applies it); `ArgInfo::default_value` surfaces to the view only |
| P5 | Type fidelity: integers serialize as JSON numbers; a touched boolean as `true`/`false` and an untouched one is omitted; timestamps as RFC 3339 Zulu; enum as one listed string |
| P6 | List/map parsing: `a, b ,c` -> `["a","b","c"]` (trim, drop empties); `k=v, k2=v2` -> object; malformed pair `k` -> field error; duplicate keys -> last-wins with a non-blocking hint (as-built) |
| P7 | Object (e.g. `campaign`): all members empty -> key omitted; any member -> object with only filled members |
| P8 | Edit round trip: `prefill_from_result(args)` then `to_args_json()` yields the untouched-field omission + locked-identity re-send; a `clear_field` on a nullable field emits `null` |
| P9 | Invalid value for any type yields exactly that field's `error` and leaves other fields intact |
| P10 | The `equivalent_cmd.hpp` line for the model parses to the same args as `to_args_json()` and names the command (footer = proof of identical envelope) |
| P11 | Strings with quotes, backslashes, newlines (paste), emoji and 4 KiB length survive into JSON correctly escaped via `jsonEscape` (compare parsed value); a buffer at `k_max_field_bytes` is flagged and omitted |
| P12 | Field order equals `ArgInfo` descriptor order; required marker `*` shown only for required fields (render check via the shipped widgets, first 80 cols) |

Edge-case table (`.inc`): integer bounds vs `ArgInfo` limits; timestamps
(`2026-02-29T00:00:00Z` invalid, leap day valid, non-Zulu rejected); empty enum
list; enum with 1 member; 64-char vs 65-char string; a `RESERVED`-like slug is
NOT rejected client-side (the model does not duplicate server rules - `validate()`
stays true and the value reaches `to_args_json()`).

## Implementation steps

1. Implement `SpecGenerator` (about 120 lines); no external property-testing library.
2. Implement P1-P12 as Boost.Test cases looping over seeds; `BOOST_TEST_CONTEXT("seed=" << seed)`.
3. Run with the default seed in CI; document `FORM_PROP_SEED=<n>` for reproducing.
4. Verify P10 against the real `link.create` `CommandInfo` from `command_list_infos()` (subtask 22), one concrete case tying to the corpus.

## Commands

```bash
ctest --test-dir cmake-build -R "^tui__30_" --output-on-failure
FORM_PROP_SEED=42 ctest --test-dir cmake-build -R "^tui__30_" --output-on-failure   # POSIX; PowerShell: $env:FORM_PROP_SEED=42
```

## Tests

| Test file | Target | CTest label | Cases |
|---|---|---|---|
| `tests/unit/tui/30_form_model_property_test.cpp` | `tui__30_form_model_property_test` | `unit` | P1-P12 (12 cases, 200 iterations each) + edge-case table + one real `link.create` case |

## Constraints

- Deterministic by default (fixed seed); runtime under 5 s.
- Uses only the public `FormModel` interface and `commands::validate_args`; no FTXUI event simulation here (rendering is in the shipped `tui__08`).
- No new dependency (no RapidCheck).

## Success criteria

1. All 8 `ArgType` values are exercised in P1, P3, P5, P9.
2. Adding a new `ArgType` in 01.0 without `FormModel` support fails P1 (the generator enumerates the `ArgType` tag values).
3. A seed failure prints enough to reproduce (seed + command JSON).

## Out of scope / risks

- Keyboard-level editing quirks of FTXUI `Input` (cursor, IME): manual checklist (29).
- Risk: if `validate_args` needs a registry instance, construct a stub registry containing a single synthetic descriptor.
