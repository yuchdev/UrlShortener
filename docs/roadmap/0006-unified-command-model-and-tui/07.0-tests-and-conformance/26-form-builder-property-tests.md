# 26 - FormBuilder property tests

**Parent task:** 07.0 Tests and conformance
**State:** ⬜ Not started
**Depends on:** 22; Task 06.0 (FormBuilder), Task 01.0 (`ArgSpec` types, validator)
**Blocks:** 29, 34, 08.0/11
**Estimated size:** M
**Suggested agent:** testing-expert

## Objective

Prove that `FormBuilder` turns **any** `ArgSpec` list into a working form whose
output envelope args validate against the same `ArgSpec` validator the
registry uses - for every `ArgSpec` type (string, integer, boolean, enum,
string-list, string-map, object, timestamp), with required/optional/default
variations - using seeded pseudo-random specs plus a fixed table of edge cases.

## Dependencies

- Subtask 22 (mock client, CMake). FormBuilder API from 06.0 (names **UNVERIFIED**; the contract needed: build from `std::vector<ArgSpec>`, programmatic `setText(field_path, text)`/`toggle(field_path)`/`select(field_path, index)`, `submit() -> SubmitResult{ok, args_json | field_errors}`, `populate(args_json)` for edit mode, `equivalentLine()`).
- 01.0: the pure function that validates an args object against `ArgSpec` (the one `execute` calls) - reuse, never re-implement.

## Files affected

| Path | create/modify | What changes |
|---|---|---|
| `tests/unit/tui/40_form_builder_property_test.cpp` | create | seeded property cases |
| `tests/unit/tui/40_form_builder_edge_cases.inc` | create | table of fixed edge-case specs/values (data file included by the test) |
| `tests/unit/tui/spec_generator.hpp` | create | `SpecGenerator` (seeded) |
| `sources.cmake` | modify | Append test to `TUI_MODEL_UNIT_SOURCES` (owned by Task 06.0 subtask 01; `FormModel` is a model-library type) |

## Classes and functions

```cpp
namespace tui_test {
class SpecGenerator {                       // deterministic: std::mt19937 with seed from env FORM_PROP_SEED (default 20261019)
public:
    explicit SpecGenerator(std::uint32_t seed);
    std::vector<ArgSpec> randomSpecs(int max_fields = 8, int max_depth = 2);
    // Returns a value (as text-typing script) that is VALID for the spec, and one INVALID variant
    ValueScript validValue(const ArgSpec&);
    ValueScript invalidValue(const ArgSpec&);   // wrong enum member, non-integer, malformed k=v, bad timestamp
};
}
```

Properties (each runs N = 200 iterations; failure message prints the seed and the spec):

| # | Property |
|---|---|
| P1 | Valid values for all fields: `submit()` ok and `validateArgs(specs, args_json)` is empty |
| P2 | Missing required field: `submit()` fails with a field error naming that field; **no request is sent** (mock `expectNoCalls`) |
| P3 | Optional empty field: key absent in args (never `""`, never `null`, except explicit-clear fields in edit mode, P8) |
| P4 | Default: untouched field with `default` is sent as the default only if the descriptor says defaults are client-side; otherwise omitted (pin the 01.0 rule; test reads a flag from `ArgSpec`) |
| P5 | Type fidelity: integer fields serialize as JSON numbers, booleans as `true`/`false` (an untouched optional checkbox is omitted, a touched one sent even if `false`), timestamps as RFC 3339 UTC strings, enum as one of the listed strings |
| P6 | List/map parsing: `a, b ,c` -> `["a","b","c"]` (trim, drop empties); `k=v, k2=v2` -> object; malformed pair `k` -> field error; duplicate keys -> last wins or error (pin one) |
| P7 | Object (e.g. `campaign`): subfields all empty -> key omitted; any subfield -> object with only filled subfields |
| P8 | Edit round trip: `populate(args)` then immediate `submit()` yields args equal to the original (as parsed JSON), for valid random args; with a cleared nullable field emits `null` (e.g. `expires_at`) |
| P9 | Invalid value for any field type yields exactly that field's error and leaves other fields' values intact |
| P10 | `equivalentLine()` contains a JSON string that parses to the same args as `submit()` and the command name (footer = proof of identical envelope) |
| P11 | Strings with quotes, backslashes, newlines (paste), emoji and 4 KiB length survive into JSON correctly escaped (compare parsed value) |
| P12 | Field order equals `ArgSpec` order; required marker `*` shown only for required fields (render check on the first 80 cols) |

Edge-case table (`.inc`): unsigned boundary ints (`0`, `500`, `501` vs `ArgSpec` bounds), timestamps (`2026-02-29T00:00:00Z` invalid, leap day valid, non-UTC offsets normalized or rejected per 01.0), empty enum list, enum with 1 member, 64-char vs 65-char `id`-like string, `slug`-type strings are NOT validated client-side beyond ArgSpec (server decides - assert the form does not duplicate server rules by submitting `RESERVED`-like slug and expecting a request to be sent).

## Implementation steps

1. Implement `SpecGenerator` (about 120 lines); no external property-testing library.
2. Implement P1-P12 as Boost.Test cases looping over seeds; use `BOOST_TEST_CONTEXT("seed=" << seed)`.
3. Run with the default seed in CI; document `FORM_PROP_SEED=<n>` for reproducing failures.
4. Verify P10 against the real `link.create` ArgSpec from the registry too (one concrete case, ties to the corpus).

## Commands

```bash
ctest --test-dir cmake-build -R "^tui__40_" --output-on-failure
FORM_PROP_SEED=42 ctest --test-dir cmake-build -R "^tui__40_" --output-on-failure   # POSIX; PowerShell: $env:FORM_PROP_SEED=42
```

## Tests

| Test file | Target | CTest label | Cases |
|---|---|---|---|
| `tests/unit/tui/40_form_builder_property_test.cpp` | `tui__40_form_builder_property_test` | `unit` | P1-P12 (12 cases, 200 iterations each) + edge-case table + one real `link.create` spec case |

## Constraints

- Deterministic by default (fixed seed); runtime under 5 s.
- Uses only the public `FormBuilder` interface and the 01.0 validator; no FTXUI event simulation here (rendering is in 25).
- No new dependency (no RapidCheck).

## Success criteria

1. All 8 `ArgSpec` types are exercised in P1, P3, P5, P9.
2. Adding a new `ArgSpec` type in 01.0 without FormBuilder support fails P1 (the generator enumerates the enum values of the type tag).
3. A seed failure prints enough to reproduce (seed + spec JSON).

## Out of scope / risks

- Keyboard-level editing quirks of FTXUI `Input` (cursor, IME): manual checklist (29).
- Risk: validator purity - if the 01.0 validator needs a registry instance, construct a stub registry containing a single synthetic descriptor.
