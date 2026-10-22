# 12 - ITransportAdapter interface and composition factory

**Parent task:** 05.0 Jobs, pair import/export and stdio transport
**State:** ⬜ Not started
**Depends on:** 01.0/06 (`CommandRegistry`, `CallerContext`), 01.0/07 (`execute`)
**Blocks:** 15, 17, 19
**Estimated size:** S
**Suggested agent:** cpp-expert (interface review: app-architect)

## Objective

Introduce the transport seam promised by ADR 0002 section 4: the `ITransportAdapter` interface and one composition function that creates an adapter by name. The interface lives in the transport-agnostic `commands/` include directory, the factory in `src/composition/`, which is where backend and adapter selection already happens (`StorageFactory`/`BuildStorageAdapters`). Only `stdio` is created here; REST and CLI keep their current entry points and are expressed as adapters by Task 04.0 only if it chooses to.

## Dependencies

- 01.0/06: `CommandRegistry` (header `command_registry.hpp`), `CallerContext`; 01.0/07: `execute`.
- The registry an adapter runs is always the one from `composition::build_command_runtime` (03.0/18, 05.0/20); adapters never build one.
- Verified pattern to follow: `include/url_shortener/composition/storage_factory.hpp` / `src/composition/storage_factory.cpp` (single selection point) and `link_command_service_factory.hpp` (struct-of-shared_ptr bundle).
- Scope reduction versus the earlier draft ("register REST/CLI/stdio through it"): wrapping `HttpServer` and the one-shot CLI in adapter classes would change `main.cpp` startup order and the TLS reload path for no user-visible gain; it is deferred and recorded in the 05.0 README risks and as open decision D7 in the decision log.

## Files affected

| Path | Action | What changes |
|---|---|---|
| `include/url_shortener/commands/transport_adapter.hpp` | create | `ITransportAdapter` |
| `include/url_shortener/composition/transport_factory.hpp` | create | `TransportOptions`, `make_transport_adapter` |
| `src/composition/transport_factory.cpp` | create | returns `nullptr` for unknown names; stdio branch is filled by subtask 15 |
| `tests/unit/commands/60_transport_factory.cpp` | create | cases below |
| `sources.cmake` | modify | add to `COMMON_CPP_SOURCES` (feeds `url_shortener_common`; the `URL_SHORTENER_CPP` list at the top of the file is legacy): `src/composition/transport_factory.cpp`; test into `COMMANDS_UNIT_SOURCES` |

## Classes and functions

```cpp
// include/url_shortener/commands/transport_adapter.hpp  (namespace url_shortener::commands)
class CommandRegistry;                         // forward declaration only

class ITransportAdapter                        // exactly the ADR 0002 shape
{
public:
    virtual ~ITransportAdapter() = default;
    virtual std::string_view name() const = 0;         // "stdio", later "ipc"
    virtual int run(CommandRegistry& registry) = 0;    // owns its loop; returns a process exit code
};
```

```cpp
// include/url_shortener/composition/transport_factory.hpp  (namespace url_shortener::composition)
struct TransportOptions
{
    bool read_only = false;                  // downgrade local caller to ControlSet::User
    std::size_t max_line_bytes = 65536;      // ServerConfig::max_request_body_bytes
    std::chrono::milliseconds drain_timeout{30000};
    JobRunner* job_runner = nullptr;         // for EOF drain; may be null
};
std::unique_ptr<commands::ITransportAdapter>
make_transport_adapter(std::string_view name, const TransportOptions& options,
                       std::istream& in, std::ostream& out);   // nullptr if name unknown
```

Contract documented in the header comment: `run` must not construct `HttpServer`, bind a socket, or touch `uri.txt` (C5) unless the adapter is explicitly a network transport; it returns 0 on clean shutdown, a catalog CLI exit code (2 usage, 4 internal) otherwise; it must be callable exactly once.

## Implementation steps

1. Add the two headers; keep `transport_adapter.hpp` free of Beast, Asio, iostream and storage headers (forward declare `CommandRegistry`).
2. Add the factory with a `std::string_view` switch (`"stdio"` returns `nullptr` until 15 lands, which is asserted by the test as the TDD starting point and flipped there).
3. Add the test: a trivial fake adapter implementing the interface proves it is implementable with no extra includes.
4. Build; run.

## Commands

```text
cmake --build cmake-build --target commands__60_transport_factory
ctest --test-dir cmake-build -R "^commands__60_transport_factory$" --output-on-failure
```
Windows/multi-config: `--config Debug` / `-C Debug`.

## Tests

| Test file | Target | Label | Cases |
|---|---|---|---|
| `tests/unit/commands/60_transport_factory.cpp` | `commands__60_transport_factory` | unit | fake adapter compiles against the header alone (include-only translation unit check) and `run` is invoked once with the registry; unknown name -> `nullptr`; empty name -> `nullptr`; name matching is case-sensitive; `name()` of the fake round-trips; after 15: `"stdio"` returns an adapter whose `name()=="stdio"` |

Manual check: none.

## Constraints

- ADR 0002: adapters own frame-in / call / frame-out only; no business rules, no per-command code.
- C5 (no listener for non-network adapters), C9 (no new dependency), C++17.
- `include/url_shortener/commands/` must not include Beast, Boost.ProgramOptions, FTXUI or storage headers (01.0 constraint); `commands/transport_adapter.hpp` includes only `<string_view>`.
- Naming per `.clang-tidy`; `.cpp` in `sources.cmake`.

## Success criteria

1. `commands__60_transport_factory` passes.
2. `grep -n "#include" include/url_shortener/commands/transport_adapter.hpp` lists only `<string_view>`.
3. The interface signature matches ADR 0002 section 4 character for character (`name()`, `run(CommandRegistry&)`).

## Out of scope / risks

- REST/CLI/TUI adapter wrappers, adapter registry plug-ins, dynamic loading.
- Risk: `TransportOptions` couples the factory to iostreams and `JobRunner`; acceptable because the factory is composition code, not part of the transport-agnostic headers.
