# 06 - Descriptor and registry lookup

**Parent task:** 01.0 Command model core
**State:** ⬜ Not started
**Depends on:** 01.0/03, 01.0/04, 01.0/05
**Blocks:** 07, 03.0/05, 03.0/07, 03.0/08, 03.0/09, 03.0/10, 03.0/14, 03.0/15, 03.0/17, 04.0/08, 05.0/04, 05.0/12, 06.0/02
**Estimated size:** S
**Suggested agent:** cpp-expert

## Objective

Define the descriptor side of the model: `Permission`, `ExecutionKind`,
`CallerContext`, `CommandContext`, `CommandResult`, `CommandHandler`,
`CommandDescriptor`, and a `CommandRegistry` that can `add`, `find` and `list`
descriptors with deterministic order and strict registration checks. `execute`
is declared here but implemented in subtask 07 (it returns `internal` until
then, so 06 can be reviewed and merged on its own).

## Dependencies

- 01.0/03 `CommandRequest`; 01.0/04 `CommandResponse`; 01.0/05 `ArgSpec`,
  `ArgsView`; 01.0/02 `CommandError`.
- `ControlSet` from `include/url_shortener/security/control_set.hpp`
  (verified: global-namespace `enum class ControlSet { Admin, User }`).
- `AccessGuard` is used in subtask 07, not here.

## Files affected

| Path | Create / modify | What changes |
|---|---|---|
| `include/url_shortener/commands/command_descriptor.hpp` | create | Header-only value types (no `.cpp`) |
| `include/url_shortener/commands/command_registry.hpp` | create | `CommandRegistry` declaration |
| `src/commands/command_registry.cpp` | create | `add`, `find`, `list`, stub `execute` |
| `tests/unit/commands/06_registry_lookup.cpp` | create | Tests |
| `sources.cmake` | modify | `COMMON_CPP_SOURCES` += `src/commands/command_registry.cpp`; `COMMANDS_UNIT_SOURCES` += test |

## Classes and functions

`namespace url_shortener::commands`:

```cpp
enum class Permission { read, write, migrate, manage_users };
enum class ExecutionKind { sync, job };

struct CallerContext
{
    std::string principal;        // "local-operator", session user, "stdio-parent", ...
    ControlSet control_set = ControlSet::User;   // global-namespace enum, verified
    std::string transport;        // "rest", "cli", "tui", "stdio", "test"
};

struct CommandContext
{
    const CallerContext& caller;
    const CommandRequest& request;
    bool dry_run = false;
};

struct CommandResult
{
    std::string result_json;      // handler's success payload (verbatim JSON text)
    CommandError error;           // set when the handler fails (expected failures only)
    std::string audit_target;     // slug / id for the audit line; never an arg value
    bool ok() const { return error.ok(); }
};

using CommandHandler = std::function<CommandResult(CommandContext&, const ArgsView&)>;

struct CommandDescriptor
{
    std::string name;             // "link.create"
    std::string summary;          // one line
    std::vector<ArgSpec> args;
    Permission permission = Permission::read;
    ExecutionKind kind = ExecutionKind::sync;
    bool mutating = false;        // TUI confirmation + audit
    bool supports_dry_run = false;
    bool async_capable = false;   // may run as a job when options.async is true
    std::vector<std::string> maps_to;   // doc only: "POST /api/v1/links", "link create"
    CommandHandler handler;
};

class CommandRegistry
{
public:
    CommandRegistry() = default;
    CommandRegistry(const CommandRegistry&) = delete;            // handlers capture `this`
    CommandRegistry& operator=(const CommandRegistry&) = delete;

    /// Throws std::invalid_argument on a programming error: bad name, empty
    /// summary, null handler, duplicate name, duplicate arg names,
    /// supports_dry_run with !mutating.
    void add(CommandDescriptor descriptor);

    const CommandDescriptor* find(std::string_view name) const;
    std::vector<const CommandDescriptor*> list() const;   // sorted by name

    CommandResponse execute(const CallerContext& caller, const CommandRequest& request) const;
private:
    std::map<std::string, CommandDescriptor, std::less<>> m_commands;
};
}
```

Notes:

- `add` throwing is consistent with CLAUDE.md ("exceptions are reserved for
  programming errors and unrecoverable startup failures"); registration happens
  at startup.
- The registry is populated once at startup and then read-only, so `find` /
  `list` / `execute` take no lock (document this; registration after
  start-of-serving is a programming error and is not synchronized).
- `list()` order is name order (`std::map`), which makes the generated catalog
  and `command.list` deterministic.
- `maps_to` is documentation-only data consumed by the catalog renderer; the
  REST route table in Task 04.0 remains the source for routing.
- `kind == job` does not imply `mutating`: `link.export` is a read job
  (catalog 3.3). The only cross-field rule is `supports_dry_run` requires
  `mutating`.

## Implementation steps

1. Header-only `command_descriptor.hpp`.
2. `command_registry.hpp` / `.cpp` with the checks in `add`; a private static
   `is_valid_descriptor(const CommandDescriptor&, std::string* why)` helper so
   tests can assert reasons.
3. `execute` stub: returns `make_failure(request.id, internal "not implemented")`.
4. Tests, sources, build, run.

## Commands

```powershell
cmake --build cmake-build --target commands__06_registry_lookup
ctest --test-dir cmake-build -R "^commands__06_registry_lookup$" --output-on-failure
```

## Tests

| Test file | Target | Label | Cases |
|---|---|---|---|
| `tests/unit/commands/06_registry_lookup.cpp` | `commands__06_registry_lookup` | unit | below |

- `add` + `find` round trip; `find` unknown and empty name -> `nullptr`.
- `list()` is sorted by name regardless of insertion order; stable across calls.
- Rejections (`std::invalid_argument`): duplicate name, invalid name
  (`Link.create`, `link`), empty summary, null handler, duplicate arg name,
  `supports_dry_run` on a non-mutating command.
- A `kind == job` read-only descriptor is accepted.
- Pointers returned by `find` stay valid after later `add` calls
  (`std::map` node stability; test holds a pointer across 100 insertions).
- Registry is non-copyable (`static_assert(!std::is_copy_constructible_v<...>)`).

## Constraints

- Header `command_descriptor.hpp` includes only std, `arg_spec.hpp`,
  `envelope.hpp`, `control_set.hpp`; no `app/`, `storage/`, `http/` headers
  (handlers live in `.cpp` files that include `app/` themselves; enforced in
  subtask 13).
- Registration is startup-only; no global/static registry instance (the
  process creates one in `src/composition/`, later tasks).
- Milestone contracts: C1 (one descriptor per command), C7 (permission is data
  on the descriptor).
- Naming: members `m_` prefix; struct fields plain.

## Success criteria

1. `commands__06_registry_lookup` passes.
2. `execute` stub is the only unimplemented function and is covered by a test
   asserting it returns `internal` (removed in 07).
3. Descriptor field list matches ADR 0002 section 2 plus `supports_dry_run`,
   `async_capable`, `maps_to` (documented additions).

## Out of scope / risks

- `execute` behavior (07), job submission (08), audit (09).
- The ADR sketch shows `Permission` / `ExecutionKind` without namespaces;
  they live in `url_shortener::commands`.
- `ControlSet` has only `Admin`/`User`; `manage_users` and `migrate` permissions
  map through `canManageUsers` / `canMigrate` (verified in `control_set.hpp`).
