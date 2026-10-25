# 15 - ITableSource port, db.tables and db.rows

**Parent task:** 03.0 Link and browse commands on the registry
**State:** ⬜ Not started
**Depends on:** 01.0/05-07 (`ArgSpec`, descriptor, `execute`); nothing from other 03.0 subtasks
**Blocks:** 16, 05.0/21, 06.0/14, 06.0/17
**Estimated size:** M
**Suggested agent:** cpp-expert (design is security-relevant: review by security-auditor in subtask 18)

## Objective

Introduce the port behind the "database tables" view: `app::ITableSource` (one logical table)
and `app::TableCatalog` (the set of tables that exist for `db.*`), plus the generic
`db.tables` and `db.rows` handlers. The table allowlist is **the catalog contents**: a table that
is not registered cannot be named, listed or read (milestone contract C6). Concrete sources
(`links`, `click_events`) are subtask 16; `jobs` is added by Task 05.0.

## Dependencies

- 01.0 registry as specified in 01.0/05-07 (`CommandHandler`, `ArgSpec` with `min`/`max`/`max_length`/`enumeration`, `Permission::read`
  enforced by `execute`); not yet implemented, follow the final headers.
- Verified logical-table facts: the real SQL tables include auth tables that must never be browsable:
  `app_users`, `auth_sessions`, `service_clients`, `auth_audit_log`
  (`src/storage/sql/migrations/005_auth_tables.sql`, `db/migrations/postgres/002_...`). The backing physical table for
  `links`/`click_events` is **not** queried by name: data comes through ports (`IMetadataRepository`,
  `IClickEventRepository`).
- `app::Result<T>` / `AppError` (verified) carry failures; codes used: `not_found`, `invalid_field`, `storage_failure`.
- Bounds (decided in the catalog 3.2): `limit` 1..500 default 50; `offset >= 0`.

## Files affected

| Path | Action | What changes |
|---|---|---|
| `include/url_shortener/app/table_source.hpp` | create | `TableColumn`, `TableQuery`, `TableRowsPage`, `ITableSource`, `TableCatalog`. No storage/http includes. |
| `src/app/table_catalog.cpp` | create | `TableCatalog` + the two serializers. Add to `COMMON_CPP_SOURCES`. |
| `include/url_shortener/commands/browse_commands.hpp` | create or modify | Declare `make_db_tables_descriptor`, `make_db_rows_descriptor` (file is shared with subtask 14; whichever lands first creates it). |
| `src/commands/db_commands.cpp` | create | The two descriptors and handlers. Add to `COMMON_CPP_SOURCES`. |
| `tests/unit/app/45_table_catalog.cpp` | create | Catalog rules + serializers. |
| `tests/unit/commands/27_db_commands.cpp` | create | Commands over fake sources. |
| `sources.cmake` | modify | `APP_UNIT_SOURCES` += 23; `COMMANDS_UNIT_SOURCES` += 27; `COMMON_CPP_SOURCES` += two .cpp. |

## Classes and functions

```cpp
// namespace url_shortener::app
struct TableColumn {
    std::string name;
    std::string type;          // "string" | "int" | "bool" | "timestamp"
    bool sortable = false;
    bool searchable = false;   // participates in the free-text `q`
};

struct TableQuery {
    std::optional<std::string> q;
    std::optional<std::string> sort;     // must be a sortable column name
    bool descending = false;
    std::size_t offset = 0;
    std::size_t limit = 50;              // already validated 1..500 by the command
};

struct TableRowsPage {
    std::vector<std::string> rows_json;  // each element: one serialized JSON object (jsonString-escaped)
    std::size_t total = 0;
};

class ITableSource {
public:
    virtual ~ITableSource() = default;
    virtual const std::string& name() const = 0;               // [a-z][a-z0-9_]*
    virtual bool writable() const = 0;
    virtual const std::vector<TableColumn>& columns() const = 0;
    virtual Result<std::size_t> row_count() const = 0;
    virtual Result<TableRowsPage> rows(const TableQuery& query) const = 0;
};

class TableCatalog {
public:
    /// false if the name is duplicated, malformed, or in the forbidden set
    /// {app_users, auth_sessions, service_clients, auth_audit_log, user_credentials}.
    bool add(std::shared_ptr<ITableSource> source);
    const ITableSource* find(std::string_view name) const;
    const std::vector<std::shared_ptr<ITableSource>>& all() const;   // registration order
};

std::string serializeTablesJson(const TableCatalog& catalog, const std::vector<std::size_t>& row_counts);
std::string serializeTableRowsJson(const ITableSource& source, const TableRowsPage& page,
                                   std::size_t offset, std::size_t limit);
```

Commands (`namespace url_shortener::commands`):

```cpp
CommandDescriptor make_db_tables_descriptor(app::TableCatalog& catalog);  // db.tables: read, sync, no args
CommandDescriptor make_db_rows_descriptor(app::TableCatalog& catalog);    // db.rows: read, sync
```

`db.rows` args: `table` string (required), `q` string (<= 256), `sort` string, `order` enum `asc,desc`
(default `asc`), `offset` int >= 0, `limit` int 1..500 default 50. Handler: `catalog.find(table)` null ->
`not_found` (message `table not found`, never echoing the name); `sort` not among `sortable` columns ->
`invalid_field` naming `sort`; build `TableQuery`; `source.rows(query)`; serialize.
Results: `db.tables` -> `{"tables":[{"name","rows","writable","columns":[{"name","type","sortable","searchable"}]}]}` (the two booleans are additive over the first catalog draft and now in catalog 3.2);
`db.rows` -> `{"table","columns":[{"name","type"}],"rows":[...],"offset","limit","total"}`.
`row_count()` failure fails the whole `db.tables` call with `storage_failure`.

## Implementation steps

1. Write `table_source.hpp` and `table_catalog.cpp` (name validation by hand, no `<regex>`).
2. Write the two serializers using `jsonString` (in the .cpp only; the header must not include `core/utils.hpp`).
3. Write the two descriptors/handlers.
4. Tests with an in-test `FakeTableSource` (configurable rows, columns, failure injection).
5. Register in `sources.cmake`; build; run.

## Commands

```powershell
cmake --build cmake-build --target app__45_table_catalog commands__27_db_commands
ctest --test-dir cmake-build -R "^(app__45_table_catalog|commands__27_db_commands)$" --output-on-failure
```

## Tests

| Test file | Target | Label | Cases |
|---|---|---|---|
| `tests/unit/app/45_table_catalog.cpp` | `app__45_table_catalog` | unit | add/find/all order; duplicate rejected; names `Links`, `1x`, empty, `a-b`, `a.b`, `a b` rejected; each forbidden auth table name rejected; `serializeTablesJson` shape for 0 and 2 tables; `serializeTableRowsJson` with empty page and with escaped content (quote, backslash, newline). |
| `tests/unit/commands/27_db_commands.cpp` | `commands__27_db_commands` | unit | `db.tables` lists exactly the registered fakes with row counts and column types; a row-count failure -> `storage_failure`; `db.rows` happy path (paging, sort asc/desc, q passthrough); `table:"users"` / `app_users` / `auth_sessions` / `""` / `"LINKS"` / `"links; DROP"` -> `not_found` (or `invalid_field` for empty, whichever the ArgSpec rule yields; assert and document); `sort` on a non-sortable or unknown column -> `invalid_field`; `limit` 0/501/negative offset -> `invalid_field`; `q` of 257 chars -> `invalid_field`; unknown arg -> `invalid_field`; source failure -> `storage_failure`; read-only caller allowed; error messages do not contain the supplied table name. |

Manual check: none.

## Constraints

- C6: allowlist-by-construction plus the forbidden-name guard; `db.*` never receives a SQL fragment or physical table name.
- Rows are serialized by sources with `jsonString` escaping; handlers never concatenate untrusted text into JSON.
- No storage/http includes in `include/url_shortener/app/table_source.hpp` and none in `include/url_shortener/commands/`.
- Read-only: `writable()` is informational for the TUI (e.g. `links` allows `link.create`); there is no write path in `db.*`.
- C++17; naming per `.clang-tidy` (`lower_case`, `m_` members); no JSON library; new `.cpp` in `COMMON_CPP_SOURCES`.

## Success criteria

1. Both tests pass.
2. `db.rows` with any table name that is not registered returns `not_found` and the message does not echo the name.
3. `TableCatalog::add` returns `false` for each of the five forbidden names (test enumerates them).
4. `git grep -n "SELECT\|FROM " src/commands src/app/table_catalog.cpp` is empty.

## Out of scope / risks

- Row-level redaction beyond what a source returns: each source decides its columns (subtask 16); the generic handler cannot know which are sensitive, so reviewers must check every new source.
- `jobs` source: Task 05.0 registers it into the same catalog.
- Writable tables / row edit: not a feature; use the typed commands.
