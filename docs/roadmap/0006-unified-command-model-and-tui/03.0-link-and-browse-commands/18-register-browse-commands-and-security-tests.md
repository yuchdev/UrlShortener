# 18 - Register browse commands and security regression tests

**Parent task:** 03.0 Link and browse commands on the registry
**State:** ⬜ Not started
**Depends on:** 03.0/11 (link commands registered), 03.0/13 (legacy lister), 03.0/14 (`link.list`), 03.0/16 (table sources; transitively 15), 03.0/17 (`system.info`); 02.0/12 (`main.cpp` wiring / backend bootstrap); 01.0/09 (`LoggerAuditSink`), 01.0/10 (`command.list`)
**Blocks:** 04.0/02, 04.0/07, 04.0/09, 04.0/13, 04.0/15 (REST/CLI need the runtime builder), 05.0/20 (adds the process-wide job runtime, job/pair commands and the `jobs` table to it), 06.0/19 (in-process client), Task 07.0
**Estimated size:** M
**Suggested agent:** cpp-expert (composition), security-auditor (review of the test list), testing-expert

## Objective

Create the one composition function that every transport will call to obtain a fully populated
`CommandRegistry` (nine link commands, `link.list`, `db.tables`, `db.rows`, `system.info`, and the
`command.list` built-in), and add the security regression suite for the browse surface so the C6 rules
(allowlisted tables, hashed client ids only, no secrets in `system.info`, authorization in the registry)
are executable and cannot drift.

## Dependencies

- All earlier 03.0 subtasks; 01.0 registry (`CommandRegistry`, `command.list` built-in from 01.0/10).
- 02.0 backend wiring (specified in the 02.0 README and subtasks 03-04, 07-12; not yet implemented): a process-wide `app::LinkBackend`
  returns the `app::ILinkStore` in use - `LegacyLinkStore` by default, `RepositoryLinkStore` (built from
  `BuildStorageAdapters(const StorageConfig&, const IClock&)`, verified in `composition/storage_factory.hpp`) when `--storage-config` is given.
  `build_command_runtime` needs one thing 02.0/07 does not specify: access to the underlying `std::shared_ptr<::IMetadataRepository>`
  (null in legacy mode) so it can choose `RepositoryLinkLister` vs `LegacyLinkLister`. **Cross-task request to 02.0/07:** expose
  `repository()` on the holder (or return a small struct from the bootstrap). Until then this is **UNVERIFIED**.
- Existing `app::BuildLegacyLinkCommandService(const ServerConfig&)` (`composition/link_command_service_factory.hpp`, verified) builds a
  `LinkCommandService` bundle over `LegacyLinkStore`/`LegacyLinkStatsReader`; 02.0/09 replaces its use in the REST handlers.
- Click repository: `analyticsClickRepository()` (verified process-global in-memory instance); see the open
  question in subtask 16.
- Registry-level authorization: `ControlSet::{Admin,User}` and `canWrite` (verified in `security/control_set.hpp`).

## Files affected

| Path | Action | What changes |
|---|---|---|
| `include/url_shortener/composition/command_runtime.hpp` | create | `CommandRuntime`, `build_command_runtime`. |
| `src/composition/command_runtime.cpp` | create | Wires services, lister, catalog, sources, registry. Add to `COMMON_CPP_SOURCES`. |
| `include/url_shortener/commands/browse_commands.hpp` | modify | Add `register_browse_commands`. |
| `src/commands/browse_commands.cpp` | create | Calls the factories of subtasks 14, 15, 17 and `registry.add`. Add to `COMMON_CPP_SOURCES`. |
| `tests/unit/commands/29_command_runtime_wiring.cpp` | create | Registry content and end-to-end calls. |
| `tests/unit/commands/30_browse_security_regression.cpp` | create | Security cases. |
| `sources.cmake` | modify | `COMMON_CPP_SOURCES` += 2 .cpp; `COMMANDS_UNIT_SOURCES` += 29, 30. |

## Classes and functions

```cpp
// commands::browse_commands.hpp
void register_browse_commands(CommandRegistry& registry,
                              app::ILinkLister& lister,
                              app::TableCatalog& catalog,
                              app::SystemInfo info);   // adds link.list, db.tables, db.rows, system.info

// composition/command_runtime.hpp   namespace url_shortener::composition
struct CommandRuntimeOptions {
    /// null = production default: a LoggerAuditSink on Component("audit") (01.0/09).
    /// Tests inject a recording sink. There is no way to build a runtime without a sink.
    std::shared_ptr<commands::IAuditSink> audit_sink;
};
struct CommandRuntime {
    // Owns every object the registry's handlers reference (declare in dependency order; destruction reverses).
    std::shared_ptr<app::LinkCommandService> link_service;
    std::shared_ptr<app::ILinkLister> link_lister;
    std::shared_ptr<app::TableCatalog> tables;
    std::shared_ptr<commands::IAuditSink> audit_sink;
    std::shared_ptr<commands::CommandRegistry> registry;
    // Task 05.0 subtask 20 adds: std::shared_ptr<commands::JobRunner> job_runner (process-wide, shared).
};
/// Cheap to call (wraps process-wide holders; opens no connections), so REST builds one per request
/// (Task 04.0 subtask 02) while CLI/TUI/stdio build one per process.
/// The ONLY registry builder of the product (plan C11): no transport or mode builds its own registry.
CommandRuntime build_command_runtime(const ServerConfig& server, const CommandRuntimeOptions& options = {});
```

`build_command_runtime` order: obtain the `ILinkStore` from `app::LinkBackend` (and its `::IMetadataRepository`, if any) -> build
`LinkCommandService` (+ stats reader) -> lister = repository present ? `RepositoryLinkLister(repo, *link_service)` : `LegacyLinkLister(linkRepository(), *link_service)`
-> `TableCatalog` with `LinksTableSource(*lister)` and `ClickEventsTableSource(analyticsClickRepository())` -> `make_system_info(storage, server)` (both already in scope: `server` is `build_command_runtime`'s own parameter)
-> `CommandRegistry` + `register_link_commands` + `register_browse_commands` (+ `command.list` from 01.0/10)
-> `registry->set_audit_sink(options.audit_sink ? options.audit_sink : std::make_shared<commands::LoggerAuditSink>(LoggerFactory::Component("audit")))`
(milestone ADR 0002 "every mutating command emits one audit line"; the logger's sink is the process's logging
configuration, which each mode initializes **explicitly before** calling this function (decision log D14): server = its configured
sink; `cmd` = disabled unless `--log-file` (file) or `--verbose` (stderr); `link <verb>` = disabled; TUI = file (default
`url_shortener-tui.log`); `serve-stdio` = stderr, never stdout). This function does not initialize logging itself. The `SystemInfo` snapshot comes from the
process-wide `app::process_system_info()` (added in subtask 17; the bootstrap that parses `--storage-config`, 02.0/11-12, calls
`app::set_process_system_info(app::make_system_info(storage_config, server_config))`; the default is the legacy snapshot `inmemory/none/noop/disabled` with default-constructed `shortener`). Task 05.0 subtask 20 extends this function (in place, same file) with the process-wide `JobRunner`, the job and pair commands and the `jobs` table source (05.0/21). Errors during wiring throw
`std::runtime_error` (unrecoverable startup failure, per repo rule); expected conditions never throw.

## Implementation steps

1. Implement `register_browse_commands`.
2. Implement `build_command_runtime` using the 02.0 factory; keep it free of transport includes.
3. Write test 29 (what the registry contains and works) and test 30 (security cases).
4. Register in `sources.cmake`; build; run the whole `commands__` and `app__` sets.

## Commands

```powershell
cmake --build cmake-build --target commands__29_command_runtime_wiring commands__30_browse_security_regression
ctest --test-dir cmake-build -R "^commands__(29|30)_" --output-on-failure
ctest --test-dir cmake-build -R "^(commands__|app__)" --output-on-failure
ctest --test-dir cmake-build -L "unit|contract" --output-on-failure
```

## Tests

| Test file | Target | Label | Cases |
|---|---|---|---|
| `tests/unit/commands/29_command_runtime_wiring.cpp` | `commands__29_command_runtime_wiring` | unit | `build_command_runtime` in default (legacy) mode registers exactly the expected names (9 link commands, `link.list`, `db.tables`, `db.rows`, `system.info`, `command.list`); create -> list -> get -> disable -> list(status=disabled) -> delete -> list(status=deleted) flow through `execute` on the in-memory backend; `db.tables` reports `links` and `click_events` with row counts matching `link.list` total and `ListEvents` total; SQLite-backed `StorageConfig` (temp file) runs the same flow (`persistent=true` in `system.info`) and a second runtime built on the same file sees the link (the 02.0 promise); `command.list` contains every descriptor's `permission`/`mutating`; **audit wiring:** a runtime built with default options has a non-null `audit_sink` of type `LoggerAuditSink`, and one built with a recording sink records exactly one record for `link.create` (outcome `ok`, target = slug) and none for `link.get`/`link.list`. |
| `tests/unit/commands/30_browse_security_regression.cpp` | `commands__30_browse_security_regression` | unit | `db.rows` for `users`, `app_users`, `auth_sessions`, `service_clients`, `auth_audit_log`, `sqlite_master`, `information_schema.tables`, `links;--`, `LINKS`, `../links` all -> `not_found` and the response never contains the supplied name; `db.tables` names are exactly `{links, click_events}`; `click_events` row key sets contain `client_id_hash` and no `ip`/`address`/`client_id`; seeded event with `user_agent` containing `"`, `\`, newline and a script tag stays valid JSON; `system.info` with a `StorageConfig` containing a DSN with password, a redis address with credentials and a file path yields none of those strings; `link.list` items contain no `owner`/`attributes` keys; `ControlSet::User` can call every browse command but gets `not_authorized` for all write commands and the store is unchanged; `limit` 501 and 0 rejected on `link.list` and `db.rows`; oversized `q` rejected; the recording audit sink receives no argument values (target URL, metadata) for `link.create`. |

Manual check: where available (Linux/macOS), rebuild with `-DCMAKE_CXX_FLAGS=-fsanitize=address,undefined`
in a separate build directory and re-run tests 29-30; on Windows run the normal Debug build.

## Constraints

- C6: every assertion above is a hard failure, not a log line.
- C7: authorization is exercised through `execute`, never by calling handlers directly.
- C8-style layering: `command_runtime.cpp` is in `src/composition/` (the only place that knows concrete types); `src/commands/` stays free of storage/SOCI includes (grep in success criteria).
- No listener, no `uri.txt`, no `UriMapSingleton` access in the runtime (milestone C5).
- C++17; `.cpp` files in `COMMON_CPP_SOURCES`; tests in `sources.cmake`.

## Success criteria

1. `ctest -R "^commands__(29|30)_"` passes; the full unit/contract sets stay green.
2. `grep -rn "soci\|sqlite3\|libpq\|hiredis" src/commands include/url_shortener/commands` is empty.
3. A static test (or the 01.0/13 hygiene test) confirms `include/url_shortener/commands/*.hpp` includes no `storage/`, `http/` or Beast headers.
4. A reviewer can answer "which tables can `db.rows` read?" from `command_runtime.cpp` alone (two `add` calls).

## Out of scope / risks

- Persistent click-event storage for CLI/TUI processes (subtask 16 open question).
- The runtime is built per process; hot reload of `StorageConfig` is not supported.
- If 02.0's holder shape differs, only `build_command_runtime` changes.
