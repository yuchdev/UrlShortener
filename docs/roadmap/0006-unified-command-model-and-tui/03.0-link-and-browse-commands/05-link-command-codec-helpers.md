# 05 - Link command codec helpers

**Parent task:** 03.0 Link and browse commands on the registry
**State:** ⬜ Not started
**Depends on:** 01.0/02 (error table, `from_app_error`, `make_error`), 01.0/05 (`ArgSpec`, `ArgsView`), 01.0/06 (`CommandDescriptor`, `CommandResult`, handler type), 01.0/07 (`execute`, used by the probe-descriptor tests)
**Blocks:** 07, 08, 09, 10, 05.0/08
**Estimated size:** S
**Suggested agent:** cpp-expert

## Objective

Put the pieces that every link command handler needs in exactly one place: the
`app::AppError` -> `CommandError` conversion, the "Result<View> -> serialized result"
wrappers that call the **existing** REST serializers, and the decoders for the two
awkward argument shapes (`campaign` object and PATCH three-state fields). Handlers in
subtasks 07-10 then contain no mapping code of their own (milestone contract C1).

## Dependencies

- 01.0/02 provides (specified, not yet implemented): `CommandError`, `CommandErrorCode`,
  `make_error(code, message)`, `from_app_error(app::AppErrorCode)`.
- 01.0/05-06 (verified in the 01.0 specs): `ArgType`, `ArgSpec {name, type, required, nullable, enum_values, min, max, max_length,
  max_items, fields, one_of_group}`, `ArgsView {has, is_null, get_string, get_int, get_bool, get_string_list, get_string_map,
  get_object (returns optional<ArgsView>), raw}`, `CommandContext {caller, request, dry_run}`, `CommandResult {result_json, error,
  audit_target}`, `CommandHandler = std::function<CommandResult(CommandContext&, const ArgsView&)>`, `CommandDescriptor`
  (`supports_dry_run`, `maps_to`, `async_capable`), `Permission`, `ExecutionKind`. These are specified, not yet implemented: follow the final 01.0 headers if a
  name moved; nothing here depends on private members.
- Existing symbols reused (verified): `app::AppError {code, detail}`, `app::Result<T>`,
  `app::LinkView`, `app::LinkStatsView`, `app::serializeLinkViewJson`,
  `app::serializeLinkPreviewJson`, `app::serializeLinkStatsJson`
  (`include/url_shortener/app/link_command_service.hpp`), `Link::Campaign` (`core/types.h`).

## Files affected

| Path | Action | What changes |
|---|---|---|
| `include/url_shortener/commands/link_command_codec.hpp` | create | Declarations below. Must not include `core/utils.h` (it pulls in Beast) nor storage/http headers (01.0/13 hygiene test). |
| `src/commands/link_command_codec.cpp` | create | Definitions; may include `core/utils.h`. Add to `COMMON_CPP_SOURCES` in `sources.cmake`. |
| `tests/unit/commands/20_link_command_codec.cpp` | create | Unit tests via a probe descriptor on a real `CommandRegistry`. |
| `sources.cmake` | modify | `COMMON_CPP_SOURCES` += codec .cpp; `COMMANDS_UNIT_SOURCES` += the test. |

## Classes and functions

`namespace url_shortener::commands`:

```cpp
/// AppError -> CommandError. code = from_app_error(e.code); message = e.detail
/// (service details are fixed strings, never request values).
CommandError to_command_error(const app::AppError& error);

/// Stats detail tokens that today's REST layer promotes to error codes
/// (`codeForAppError`). Exposed so Task 04.0 can reproduce the legacy code.
inline constexpr std::string_view detail_invalid_timestamp = "invalid_timestamp";
inline constexpr std::string_view detail_invalid_bucket = "invalid_bucket";

/// Result wrappers: call the shared serializer, never re-format. `audit_target` is the slug/id the
/// caller addressed (used when the call fails); on success the view's own slug is used.
CommandResult link_view_result(const app::Result<app::LinkView>& result, std::string_view audit_target);       // serializeLinkViewJson
CommandResult link_preview_result(const app::Result<app::LinkView>& result, std::string_view audit_target);    // serializeLinkPreviewJson
CommandResult link_stats_result(const app::Result<app::LinkStatsView>& result, std::string_view audit_target); // serializeLinkStatsJson

/// Decodes the `campaign` object arg (six optional string members declared in the ArgSpec `fields`;
/// the registry already rejected unknown inner keys and non-string members, so this cannot fail on
/// shape; the `CommandError*` is kept for future fields). Returns false only on internal inconsistency.
bool decode_campaign(const ArgsView& args, std::string_view name,
                     std::optional<Link::Campaign>* out, CommandError* error);

/// PATCH tri-state for nullable args (`expires_at`, `campaign`):
enum class patch_state { absent, null_value, value };
patch_state patch_state_of(const ArgsView& args, std::string_view name);

/// Selector shared by link.get / link.preview. The registry enforces "exactly one of slug/id" through
/// `ArgSpec::one_of_group = "slug_or_id"` (error `invalid_field`, `<arg>: one_of_violation`); this
/// helper only picks the one that is present.
bool decode_slug_or_id(const ArgsView& args, app::GetLinkQuery* out, CommandError* error);
```

Ownership: all functions are pure and stateless; outputs are written through pointers
(repo style: `bool` + out-param for expected failures, no exceptions).

## Implementation steps

1. Create the header and .cpp; implement `to_command_error` first (one line over `from_app_error`).
2. Implement the three result wrappers: on `result.ok() && result.value` return
   `CommandResult{result_json = <serializer output, unchanged>, audit_target = view slug}`; otherwise
   `to_command_error(result.error)`. An `ok()` result without a value maps to `internal`
   ("service returned no value").
3. Implement `decode_campaign`, `patch_state_of`, `decode_slug_or_id`.
4. Write the probe-descriptor tests; register; build; run.

## Commands

```powershell
cmake --build cmake-build --target commands__20_link_command_codec
ctest --test-dir cmake-build -R "^commands__20_link_command_codec$" --output-on-failure
```

Multi-config generators: `--config Debug` / `-C Debug`.

## Tests

| Test file | Target | Label | Cases |
|---|---|---|---|
| `tests/unit/commands/20_link_command_codec.cpp` | `commands__20_link_command_codec` | unit | `to_command_error` for every `app::AppErrorCode` (incl. `none` handled defensively); detail preserved as message and contains no arg value; result wrappers return byte-identical text to calling the serializer directly for a populated `LinkView`/`LinkStatsView` (build views by hand); `ok()` without value -> `internal`; `decode_campaign`: full object, empty object `{}`, only some fields (absent members stay `nullopt`); `null` is handled by `patch_state_of`, not here; `patch_state_of`: absent / `null` / value for `expires_at` and `campaign`; `decode_slug_or_id`: slug only, id only; through a probe descriptor with `one_of_group`, both -> `invalid_field` and neither -> `invalid_field` come from the registry (assert the message ends with `one_of_violation`). |

Manual check: none.

## Constraints

- C1/C3: serializers are called, not copied; string equality against the REST serializer is a test.
- No JSON library; JSON text is produced only by the existing serializers.
- Error messages are fixed strings or arg **names**; never arg values (C6).
- Header hygiene: no `http`, `storage`, Beast, Asio, ProgramOptions or FTXUI includes.
- C++17; `lower_case` functions and enumerators (matches 01.0's naming), `m_` for private members.

## Success criteria

1. `commands__20_link_command_codec` passes.
2. `link_view_result` output equals `app::serializeLinkViewJson` for the same view, byte for byte (test).
3. `grep -n "#include" include/url_shortener/commands/link_command_codec.hpp` shows no
   `core/utils.h`, `http/`, `storage/` or `boost/`.

## Out of scope / risks

- REST-specific per-field 400 codes (`invalid_enabled`, `invalid_tags`, ...) are produced by
  the REST projection in Task 04.0, not here.
- **REST difference to handle in Task 04.0:** today the REST handler silently ignores unknown keys *inside* `campaign`
  (`extractCampaign` reads only name/source/medium/term/content/id) whereas the registry rejects them
  (01.0/05: object members must be declared). The REST projection rebuilds the `campaign` object from the six known keys
  before calling `execute`, so REST behavior is unchanged; generic/CLI callers get the stricter check.
