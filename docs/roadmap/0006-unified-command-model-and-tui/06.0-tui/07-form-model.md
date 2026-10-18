# 07 - FormModel: ArgSpec to form state to request envelope

**Parent task:** 06.0 Terminal UI
**State:** ⬜ Not started
**Depends on:** 06.0/02, 06.0/06 (for `equivalent_*`)
**Blocks:** 08, 11, 20
**Estimated size:** L
**Suggested agent:** cpp-expert, testing-expert (property tests)

## Objective

Implement the FTXUI-free half of `FormBuilder` (ADR 0003 Decision 2 rule 2,
"generic first"): a `FormModel` that is constructed from one `CommandInfo` (its
`ArgInfo` list), holds per-field editing state for every `ArgSpec` type
(string, integer, boolean, enum, string-list, string-map, object, timestamp),
validates advisory-only on the client, and produces the exact
`CommandRequest` the registry expects. This is the data model behind **D5**
(New/edit link form) and the "GenericForm" node of **D11**/**D9**; its
`to_request()` is the only place the TUI constructs `args` JSON.

## Dependencies

- Internal: 06.0/02 (`CommandInfo`, `ArgInfo`, `ArgKind`, `json_view`), 06.0/06 (`equivalent_cli`, `equivalent_envelope`), 06.0/03 (`RequestIdGenerator`). From `core/utils.h` (include from the `.cpp` only, it pulls in Beast): `jsonEscape`, `jsonString` (verified); the RFC 3339 parser the REST handlers use for `expires_at`, `url_shortener::parseRfc3339Zulu` (verified in `link_handlers.cpp::createLinkFromRequest`; reuse it, do not write a second parser).
- External: none (model lib).

## Files affected

| Path | Action | What changes |
|---|---|---|
| `include/url_shortener/tui/form_model.hpp` / `src/tui/form_model.cpp` | create | `FormModel`, `FormField`, `FieldState`, `FormOptions` |
| `include/url_shortener/tui/field_parsers.hpp` / `src/tui/field_parsers.cpp` | create | `parse_string_list`, `parse_string_map`, `parse_integer`, `format_*` inverse functions |
| `tests/unit/tui/07_field_parsers_test.cpp` | create | parser unit + round-trip tests |
| `tests/unit/tui/07_form_model_test.cpp` | create | per-type behaviour |
| `tests/unit/tui/07_form_model_request_test.cpp` | create | exact-bytes request tests for each catalog command |
| `tests/unit/tui/golden/07_request_link_create.json`, `07_request_link_update_clear.json`, `07_request_link_list_all.json` | create | byte-exact expected `args` |
| `sources.cmake` | modify | register |

## Classes and functions

```cpp
enum class FieldState { untouched, set, cleared };      // cleared => emit explicit null (only if ArgInfo.nullable)

struct FormField {
    ArgInfo spec;
    std::string text;                                   // string/integer/timestamp/list/map editing buffer
    bool checked = false;                               // boolean
    int choice = -1;                                    // enumeration index, -1 = unset
    std::vector<std::string> members_text;              // object members, parallel to spec.members
    FieldState state = FieldState::untouched;
    std::optional<std::string> error;                   // advisory client-side message
};

struct FormOptions { bool include_dry_run_toggle = true; std::string request_id_prefix = "tui"; };

class FormModel {
public:
    FormModel(const CommandInfo& command, FormOptions options = {});
    const CommandInfo& command() const;
    std::vector<FormField>& fields();
    void set_text(std::size_t i, std::string text);     // marks set; empty text on optional field -> untouched
    void set_checked(std::size_t i, bool v);
    void set_choice(std::size_t i, int index);
    void set_member(std::size_t i, std::size_t m, std::string text);
    void clear_field(std::size_t i);                    // nullable only; else no-op returns via bool overload
    void set_dry_run(bool v);
    bool validate();                                    // fills FormField::error; returns true if no errors
    std::string to_args_json() const;                   // only untouched-omitted, set, cleared(null) fields; stable key order = ArgSpec order
    commands::CommandRequest to_request(const std::string& request_id) const;   // v=1, command, args, options.dry_run
    std::string validation_summary() const;             // D5 footer text: "target_url ok - slug ok"
    void prefill_from_result(const std::string& result_json);   // edit flow: LinkView JSON -> fields (tags join ", ", metadata k=v)
};
std::vector<std::string> parse_string_list(const std::string& s);                    // "a, b ,c" -> {a,b,c}; empty items dropped
std::optional<std::vector<std::pair<std::string,std::string>>> parse_string_map(const std::string& s);  // "k=v, k2=v2"; nullopt on missing '='
```

Encoding rules (mirror the catalog and `app::UpdateLinkCommand` semantics):
`untouched` => key absent; `set` => value; `cleared` => `null`; empty list/map
on `set` => `[]`/`{}` (explicit replace-with-empty); `enabled` default `true` is
sent only when the user touched it; integers emitted unquoted; enum as the
selected string; timestamp passed through after advisory parse; object emitted
as `{"name":"x",...}` with only non-empty members, omitted entirely if all empty.

## Implementation steps

1. Build fields from `CommandInfo::args` preserving order; pre-set defaults from `ArgInfo.default_value` as *displayed placeholders* only (do not send untouched defaults - the server applies them).
2. Implement parsers and their inverse formatters (`format_string_list`, `format_string_map`) for edit prefill; values containing `,` or `=` are not representable in the comma form: detect and set an error message "use the JSON form (palette -> cmd)" rather than corrupt data.
3. Advisory validation: required present; integer parse and `min/max` if present in `ArgInfo.doc` is NOT parsed - bounds come from server error; enum in set; timestamp parses; `exactly one of` rules (`link.get` slug/id) are expressed in `doc` only and left to the server - never duplicate server validation beyond cheap type checks.
4. `to_args_json` builds the object with `jsonString`/`jsonEscape`; no JSON library.
5. `prefill_from_result` for `link.update`: slug filled and marked read-only (`FormField` gains `locked` flag), other fields prefilled but left `untouched` unless edited, so unchanged fields are not re-sent (PATCH semantics).
6. `validation_summary` lists `name ok`/`name: message` for touched fields, as in D5.

## Commands

```powershell
cmake --build cmake-build --target tui__07_field_parsers_test tui__07_form_model_test tui__07_form_model_request_test
ctest --test-dir cmake-build -R "^tui__07_" --output-on-failure
```

## Tests

| Test file | Target | Label | Cases |
|---|---|---|---|
| `07_field_parsers_test.cpp` | `tui__07_field_parsers_test` | unit | list: empty string, single, spaces, trailing comma, unicode; map: `a=1,b=2`, missing `=`, duplicate keys (last wins, warn), empty key rejected; round-trip `parse(format(x)) == x` over 200 generated inputs; value containing `,` flagged |
| `07_form_model_test.cpp` | `tui__07_form_model_test` | unit | one case per `ArgKind`: untouched omitted, set encoded, required missing -> error, enum out of range, integer `abc` and overflow, timestamp invalid, boolean tri-state not applicable (checked only emitted when touched); `clear_field` on non-nullable is rejected; object with members -> nested JSON; object without members -> raw JSON text field accepted only if it parses as `{...}` |
| `07_form_model_request_test.cpp` | `tui__07_form_model_request_test` | unit | built from `link.create`, `link.update`, `link.list`, `link.import` descriptors (hand-written `CommandInfo` copies of the catalog tables, plus a conformance check that fails if `docs/commands/README.md` arg names drift - read file at test time via `TUI_REPO_ROOT`): exact `args` bytes vs goldens; `link.update` clearing `expires_at` yields `"expires_at":null`; `dry_run` toggle sets `options.dry_run` only for commands with `supports_dry_run`; special characters in slug/URL escaped (`"`, `\`, newline, non-ASCII) |

## Constraints

- Client validation is advisory; the registry's `ArgSpec` check stays authoritative (C1, C7). A form must still display `invalid_field` returned by the server.
- Unknown/future `ArgKind` values degrade to a string field, never crash (new commands must "appear with a working form, with zero TUI code", D9).
- The model must not know any specific command name (grep test: `form_model.cpp` contains no `"link."` literal).
- C9: no JSON library; C++17.

## Success criteria

1. For every command in the catalog (hand-copied `CommandInfo` fixtures), a form can be built and a valid request produced with only generic code paths.
2. Request bytes for `link.create`/`link.update` match goldens exactly.
3. PATCH semantics preserved: untouched = absent, cleared = `null`.
4. `form_model.cpp` has no command-name literals (test enforces).

## Out of scope / risks

- FTXUI widgets (08), confirmation (06), submission (08/11).
- **Dependency on Task 01.0 subtask 05:** `ArgSpec::nullable` and object `fields` are specified there (surfaced as `ArgInfo::nullable` / `members` by 06.0/02); if that spec changes, the update form cannot clear fields and campaign renders as a raw-JSON field.
- Risk: values with `,`/`=` in tags/metadata need a JSON-entry escape hatch; the palette form offers a "raw JSON args" toggle (implemented in 11).
