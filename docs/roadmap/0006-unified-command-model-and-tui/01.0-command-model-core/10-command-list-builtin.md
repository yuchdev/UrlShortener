# 10 - command.list built-in

**Parent task:** 01.0 Command model core
**State:** ⬜ Not started
**Depends on:** 01.0/07
**Blocks:** 11, 03.0/18, 04.0/10
**Estimated size:** S
**Suggested agent:** cpp-expert

## Objective

Register the first real command, `command.list`, whose result is the
registry's descriptor set (catalog section 3.5). It proves the pipeline end to
end with a production handler, and its JSON form of a descriptor
(`descriptor_to_json`) is what the TUI palette/forms, CLI `--help` and client
codegen consume later.

## Dependencies

- 01.0/07 `execute` (so the command can be invoked through the registry).
- 01.0/05 `ArgSpec` (serialized as `args:[{name,type,required,enum?,doc}]`).
- Catalog 3.5 result shape:
  `{ "commands": [ { name, summary, perm, kind, mutating, dry_run, args:[{name,type,required,enum?,doc}] } ] }`.

## Files affected

| Path | Create / modify | What changes |
|---|---|---|
| `include/url_shortener/commands/builtin_commands.hpp` | create | `register_builtin_commands`, `descriptor_to_json`, type-name helpers |
| `src/commands/builtin_commands.cpp` | create | `command.list` descriptor and serializer |
| `tests/unit/commands/10_command_list.cpp` | create | Tests |
| `sources.cmake` | modify | `COMMON_CPP_SOURCES` += `src/commands/builtin_commands.cpp`; `COMMANDS_UNIT_SOURCES` += test |

## Classes and functions

```cpp
namespace url_shortener::commands {

std::string_view permission_name(Permission p);        // "read","write","migrate","manage_users"
std::string_view kind_name(ExecutionKind k);           // "sync","job"
std::string_view arg_type_name(ArgType t);             // "string","int","bool","enum","timestamp",
                                                       // "list<string>","map<string,string>","object"
/// One descriptor as a JSON object (stable key order, ArgSpec order preserved).
std::string descriptor_to_json(const CommandDescriptor& descriptor);

/// Registers command.list (read, sync, non-mutating, no args).
/// The handler captures `&registry`; the registry must outlive the handler,
/// which is guaranteed because the handler lives inside the registry
/// (non-copyable, non-movable).
void register_builtin_commands(CommandRegistry& registry);
}
```

Result JSON: `{"commands":[<descriptor_to_json>,...]}` in `list()` order (name
sorted). `dry_run` field is `descriptor.supports_dry_run`; `args[].enum` is
present only for enumeration args; `default` is included when set. `maps_to`
and the handler are not serialized.

`arg_type_name` follows the catalog vocabulary (`int`, `bool`, `list<string>`,
`map<string,string>`), not the C++ enumerator names.

## Implementation steps

1. Add the name helpers (exhaustive switches, no default).
2. Implement `descriptor_to_json` with `url_shortener::jsonString` for strings
   (called from the `.cpp`).
3. Implement `register_builtin_commands` building the descriptor
   (`summary`: "List all commands and their argument schemas") and handler
   lambda.
4. Tests, sources, build, run.

## Commands

```powershell
cmake --build cmake-build --target commands__10_command_list
ctest --test-dir cmake-build -R "^commands__10_command_list$" --output-on-failure
```

## Tests

| Test file | Target | Label | Cases |
|---|---|---|---|
| `tests/unit/commands/10_command_list.cpp` | `commands__10_command_list` | unit | below |

- Executing `command.list` on a registry that has only the built-in returns an
  envelope whose `commands` array has exactly one element named `command.list`
  with `perm":"read"`, `kind":"sync"`, `mutating":false`.
- After adding three fake descriptors, `command.list` result equals the
  descriptor set: names, order, `perm`, `kind`, `mutating`, `dry_run`, and every
  `ArgSpec` field serialized (set-equality test comparing parsed members).
- Re-scan with `scan_object` proves the output is a well-formed object; an enum
  arg exposes its `enum` values; a nullable/required flag round-trips.
- `User` can call `command.list` (read permission); an unknown extra arg ->
  `invalid_field` (it takes no args).
- Deterministic: two calls return byte-identical output.
- Name helpers: every enumerator has a distinct non-empty name.

## Constraints

- Handler is pure (no I/O, no service access); must not reveal handlers,
  `maps_to`, or anything not in the catalog shape.
- Output stays within `max_request_body_bytes`-scale sizes; no pagination is
  needed for tens of commands (documented).
- C++17; no JSON library.
- Milestone contracts: C1, C2.

## Success criteria

1. `commands__10_command_list` passes.
2. `command.list` result equals the descriptor set (the original acceptance
   criterion of the task) - test compares every descriptor field.
3. The handler contains no reference to `app::`/storage types (grep).

## Out of scope / risks

- `system.info` (Task 03.0 subtask 17) and all link/db/job commands.
- A versioned/filtered `command.list` (e.g. hide `manage_users` commands from a
  `User`) is not implemented: list shows what exists; execution is what is
  gated. Revisit if the TUI should hide unauthorized commands.
