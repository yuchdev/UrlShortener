# 02 - ICommandClient, InProcessClient, JSON view helpers and include boundary

**Parent task:** 06.0 Terminal UI
**State:** ⬜ Not started
**Depends on:** 06.0/01; 01.0/01 (`scan_object`, `scan_array`, `decode_json_string`), 01.0/03 (`CommandRequest`), 01.0/04 (`CommandResponse`), 01.0/05 (`ArgSpec` with `nullable` and object `fields`), 01.0/06 (descriptor, `CommandRegistry`, `CallerContext`), 01.0/07 (`execute`)
**Blocks:** 03, 04, 06, 07, 09, 11, 18, 19, 20, 21
**Estimated size:** M
**Suggested agent:** cpp-expert

## Objective

Create the single seam through which the TUI reaches the application
(ADR 0003 Decision 2 rule 1, contract C8): `ICommandClient`, its default
`InProcessClient` over `CommandRegistry::execute`, a recording `MockCommandClient`
for tests, TUI-local value types describing commands (`CommandInfo`/`ArgInfo`),
small JSON *view* helpers (field access, array splitting, pretty printing) for
`result` payloads built on the commands-layer scanner, and a test that enforces
the include boundary. Renders **D12** (the sequence
`Screen -> ICommandClient -> CommandRegistry`): every later screen is one
participant column of that diagram.

The TUI adds **no JSON tokenizer of its own** (plan C9, decision log D3): every
helper below delegates the scanning to `url_shortener::commands::scan_object`,
`scan_array` and `decode_json_string` (Task 01.0 subtask 01, header
`include/url_shortener/commands/json_scan.hpp`, free of Beast/storage/http
includes and therefore allowed under `src/tui/`).

## Dependencies

- Internal: envelope types `CommandRequest`/`CommandResponse` (01.0/03-04), `ArgSpec`/`ArgType` with `nullable` and object `fields`
  (01.0/05, specified), `CommandRegistry`/`CallerContext`/`CommandDescriptor` (01.0/06-07), all in namespace `url_shortener::commands`
  under `include/url_shortener/commands/`; `security::ControlSet` (`include/url_shortener/security/control_set.hpp`, verified);
  `jsonEscape` from `core/utils.hpp` only inside `.cpp` files (it pulls in Beast).
- The scanner of 01.0/01: `scan_object`, `scan_array`, `find_member`, `decode_json_string`, `json_kind`.
- Registry construction for the in-process client is **not** done here: `tui_entry.cpp` (subtask 19) calls
  `composition::build_command_runtime` (Task 03.0 subtask 18, extended for jobs by Task 05.0 subtask 20) and hands
  `*runtime.registry` to `InProcessClient`.
- External: none (model library only, no FTXUI).

## Files affected

| Path | Action | What changes |
|---|---|---|
| `include/url_shortener/tui/command_client.hpp` | create | `ICommandClient`, `SessionInfo`, `CommandInfo`, `ArgInfo`, `ArgKind` |
| `include/url_shortener/tui/in_process_client.hpp` | create | `InProcessClient` (the **only** TUI header allowed to include `commands/command_registry.hpp`) |
| `src/tui/in_process_client.cpp` | create | implementation; descriptor -> `CommandInfo` mapping; `SessionInfo` from `CallerContext` |
| `include/url_shortener/tui/json_view.hpp` | create | `raw_field`, `split_array`, `pretty_print`, `get_string`, `get_int`, `get_bool` (view helpers) |
| `src/tui/json_view.cpp` | create | thin wrappers over `commands::scan_object`/`scan_array`/`decode_json_string`; no scanning code of its own |
| `tests/unit/tui/support/mock_command_client.hpp` | create | `MockCommandClient` (scripted responses, recorded requests); Task 07.0 subtask 22 extends it, it does not create a second one |
| `tests/unit/tui/02_in_process_client_test.cpp` | create | see Tests |
| `tests/unit/tui/02_json_view_test.cpp` | create | see Tests |
| `tests/unit/tui/02_include_boundary_test.cpp` | create | scans `src/tui/` and `include/url_shortener/tui/` |
| `sources.cmake` | modify | append the two `.cpp` to `TUI_MODEL_CPP_SOURCES`, the three tests to `TUI_MODEL_UNIT_SOURCES` |

## Classes and functions

Namespace `url_shortener::tui` (NEW unless noted):

```cpp
enum class ArgKind { string, integer, boolean, enumeration, string_list, string_map, object, timestamp };

struct ArgInfo {
    std::string name;
    ArgKind kind = ArgKind::string;
    bool required = false;
    bool nullable = false;                    // from ArgSpec::nullable (01.0/05): link.update expires_at/campaign accept null
    std::vector<std::string> enum_values;
    std::vector<std::string> members;         // from ArgSpec::fields (01.0/05): campaign name,source,medium,term,content,id
    std::optional<std::string> default_value;
    std::string doc;
};
struct CommandInfo {
    std::string name, summary;
    std::string permission;                   // "read" | "write" | "migrate" | "manage_users"
    bool is_job = false, mutating = false, supports_dry_run = false;
    std::vector<ArgInfo> args;
};
struct SessionInfo { std::string principal; bool read_only = false; std::string transport = "tui"; };

class ICommandClient {
public:
    virtual ~ICommandClient() = default;
    // Blocking; MUST be called off the UI thread (see subtask 03).
    virtual commands::CommandResponse call(const commands::CommandRequest& request) = 0;
    virtual std::vector<CommandInfo> commands() const = 0;   // catalog snapshot; RemoteClient fills it from command.list
    virtual SessionInfo session() const = 0;
};

class InProcessClient final : public ICommandClient {
public:
    InProcessClient(const commands::CommandRegistry& registry, commands::CallerContext caller);
    // overrides ...
};
```

`json_view.hpp` (pure functions, `std::string_view` in/out where possible; all scanning delegated to 01.0/01):

```cpp
std::optional<std::string> raw_field(const std::string& json, const std::string& field);   // find_member(scan_object(json))->raw
std::vector<std::string> split_array(const std::string& raw_array);                         // scan_array(raw_array).elements[i].raw
std::optional<std::string> get_string(const std::string& json, const std::string& field);   // decode_json_string(raw)
std::optional<std::int64_t> get_int(const std::string& json, const std::string& field);     // number kind + std::from_chars
std::optional<bool> get_bool(const std::string& json, const std::string& field);
std::string pretty_print(const std::string& json, int indent = 2);                          // walks scan_object/scan_array recursively
                                                                                            // with a depth cap; never throws; invalid
                                                                                            // input returned verbatim
```

`MockCommandClient` (tests): `enqueue(std::string command, CommandResponse)`, `std::vector<CommandRequest> requests`, optional per-call delay for async tests, `set_session(SessionInfo)`, `set_commands(...)`.

## Implementation steps

1. `ArgKind` mapping table from the registry's `ArgType` enum (string/integer/boolean/enum/string-list/string-map/object/timestamp per ADR 0002); unknown type -> `ArgKind::string` plus no crash; copy `nullable` and `fields` names.
2. `InProcessClient::call` forwards to `registry.execute(m_caller, request)` verbatim; it never catches business errors (they are already envelopes) but converts a thrown exception into an `internal` error envelope so a handler bug cannot kill the UI.
3. Implement the view helpers as thin wrappers over the 01.0/01 scanner; pass `scan_limits{ .max_bytes = 8 MiB }` (assigned, C++17) so large `link.list` pages are accepted while hostile input is still bounded.
4. `pretty_print`: re-indent without reordering keys or changing string contents (golden-testable); recursion depth bounded by `scan_limits::max_depth`.
5. Write the boundary test: iterate `*.cpp/*.hpp` under `src/tui` and `include/url_shortener/tui`, fail on any `#include` matching `url_shortener/(app|storage|http|composition|security/.*service|uri_map_singleton)`; allow-list exactly `in_process_client.{hpp,cpp}` and `tui_entry.cpp` (subtask 19) for `commands/command_registry.hpp` and `composition/`; `commands/json_scan.hpp` and the envelope headers are allowed everywhere; fail on any file under `src/tui/` that defines its own JSON tokenizer (heuristic: a function named `scan_*`/`parse_json*` outside `json_view.cpp`).

## Commands

```powershell
cmake --build cmake-build --target tui__02_in_process_client_test tui__02_json_view_test tui__02_include_boundary_test
ctest --test-dir cmake-build -R "^tui__02_" --output-on-failure
```

## Tests

| Test file | Target | Label | Cases |
|---|---|---|---|
| `02_in_process_client_test.cpp` | `tui__02_in_process_client_test` | unit | `call` returns the registry envelope unchanged (use a tiny test registry with one echo command); unknown command -> `unknown_command` envelope passes through; handler throwing -> `internal` envelope, no exception escapes; `commands()` lists every registered descriptor with correct `ArgKind`, `nullable`, `members`, `is_job`, `mutating`; `session().read_only` true when caller `ControlSet::User`; calling with `ControlSet::User` on a write command yields `not_authorized` (not an exception) |
| `02_json_view_test.cpp` | `tui__02_json_view_test` | unit | `raw_field` for nested object/array; braces and brackets inside string values and escaped quotes do not confuse `split_array` (delegation to `scan_array` verified); empty array; missing field -> nullopt; truncated input -> nullopt (no UB); Unicode and `\u` escapes decoded; `pretty_print` golden `tests/unit/tui/golden/02_pretty_link_list.txt`; invalid JSON returned verbatim; 8 MiB cap |
| `02_include_boundary_test.cpp` | `tui__02_include_boundary_test` | unit | passes on the tree; a synthetic temp dir containing a forbidden include is rejected (test the scanner function directly) |

## Constraints

- C8: only `in_process_client.*` and `tui_entry.cpp` may see `CommandRegistry`; screens see `ICommandClient` and envelope value types.
- C9: no JSON library and no second JSON tokenizer; `json_view` only wraps the 01.0/01 scanner (decision log D3).
- Naming: ADR-given type names (`ICommandClient`, `InProcessClient`) kept; functions, members, enumerators `lower_case`, private members `m_` prefix.
- C5/C10: nothing here may include `http/` or touch `uri.txt`.

## Success criteria

1. `ICommandClient` has exactly `call`, `commands`, `session` and no registry/service types in its header.
2. With `MockCommandClient` a screen test needs no registry and no storage.
3. The boundary test passes and fails when a forbidden include is injected.
4. `split_array` round-trips 1,000 generated `link.list` items (property-style loop) without mis-splitting.
5. D12 flow verified: a request sent through `InProcessClient` to a test registry returns the same bytes as `registry.execute` called directly.
6. `src/tui/json_view.cpp` contains no character-level JSON scanning loop (review/grep: no `case '"':`/depth counter).

## Out of scope / risks

- `RemoteClient` (REST/IPC) is a later milestone; `commands()` is on the interface precisely so it can fill it from `command.list`.
- `ArgSpec::nullable` and object `fields` are specified by 01.0/05; if that spec changes, the update form cannot offer "clear" (catalog `expires_at: timestamp | null`) and the campaign sub-fields of D5 cannot be generated; fallback is a raw-JSON text field for objects.
- `CommandRequest::args_json` (raw JSON object text, 01.0/03) is the representation assumed here.
