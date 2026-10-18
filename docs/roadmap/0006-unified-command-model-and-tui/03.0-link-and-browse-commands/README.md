# Task 03.0 - Link and browse commands on the registry

**Parent milestone:** [Milestone 0006 - Unified Command Model and TUI](/docs/roadmap/0006-unified-command-model-and-tui/plan.md)
**Status:** ⬜ Not started
**Depends on:** [01.0](/docs/roadmap/0006-unified-command-model-and-tui/01.0-command-model-core/README.md) (registry, `ArgSpec`, envelope; needed by subtasks 05 and 07-16), [02.0](/docs/roadmap/0006-unified-command-model-and-tui/02.0-storage-path-unification/README.md) (repository-backed `ILinkStore` and its `Link` <-> `LinkRecord` mapping; needed by subtasks 11, 12, 15 and 16 only)

## Scope

Two halves that can run in parallel:

1. **Ports (subtasks 01-04)** - extend the storage ports so a browse command can
   filter, sort, page and count: `ListLinksQuery` plus a new
   `IMetadataRepository::ListLinksPage`, and a new bounded
   `IClickEventRepository::ListEvents`. These touch only `include/url_shortener/storage`,
   `include/url_shortener/analytics` and their implementations; they do **not**
   need 01.0 or 02.0 and can start immediately.
2. **Commands (subtasks 05-18)** - register the nine existing link commands as
   descriptors/handlers over `app::LinkCommandService` (results byte-identical to
   today's REST bodies), then add the read-side commands the TUI and later web
   clients need: `link.list`, `db.tables`, `db.rows`, `system.info`.

### Verified findings that shape the specs (2026-10-19, read from the code)

| Finding | Where verified | Consequence |
|---|---|---|
| Only two `IMetadataRepository` implementations exist: `InMemoryMetadataRepository` and `SqlMetadataRepository` (one class serving SQLite and Postgres through `SqlDialect` + `ISqlSession`). There is **no Redis metadata repository**; Redis is `RedisCacheStore` + `RedisRateLimiter` only. | `src/storage/**`, `include/url_shortener/storage/**` | The old "Redis passthrough" item is void. List/count is always an authoritative repository read; the cache is never consulted. |
| `IMetadataRepository` is declared in the **global** namespace and returns `LinkRecord` (no tags/campaign/enabled/deleted fields: those live in `attributes`/`is_active`/`deleted_at`). A second, unrelated `url_shortener::IMetadataRepository` (Link-based, no list) lives in `storage/link_repository.h`. | `i_metadata_repository.hpp`, `link_repository.h` | `link.list` needs 02.0's `Link` <-> `LinkRecord` codec (`record_to_link`); the legacy Link-based interface has no list, so default mode needs a `snapshot()` (subtask 13). |
| `SqlMetadataRepository::ListLinks` ignores `include_deleted`; the SQL schema (`SqliteSqlDialect::BootstrapSchemaSql`, Postgres migration `001`) has **no `deleted_at` column** and `SqlMetadataRowMapper` never writes/reads it. A soft-deleted record round-trips as not deleted. | `sql_metadata_row_mapper.cpp`, dialects | 02.0/02 stores deletion in the `lnk_deleted` attribute (and sets `is_active=false`), so at record level "disabled" and "deleted" are indistinguishable: lifecycle status is evaluated by the lister after decoding (subtask 12), not by the repository. `include_deleted` contract cases run in-memory only (subtask 02). |
| `ISqlSession::List` has a fixed signature implemented by `SqliteSession`, `PostgresSession` and two test stubs (`tests/unit/storage/sql/05_...`, `tests/unit/storage/psql/psql_test_common.hpp`). | `i_sql_session_factory.hpp` | Do not change it. Extended queries are evaluated in C++ over a bounded scan (subtask 01, answers the "what would a native extension require" question); SQL push-down is a follow-up. |
| `IClickEventRepository` has two implementations (`InMemoryClickEventRepository`, `SqlClickEventRepository` for SQLite+Postgres) and five test fakes (`tests/unit/analytics/10,11,12,13,15`). The live click repository is the process-global `analyticsClickRepository()` (in-memory) in `src/http/request_handlers.cpp`, not a configured backend. | `i_click_event_repository.hpp`, `request_handlers.cpp` | `ListEvents` is a **non-pure** virtual (fakes keep compiling). Which instance `db.rows click_events` reads is an open wiring question (subtask 16). |
| REST create accepts `url` **and** `target_url` (`url` first), `code` only on the compat route, rejects `deleted_at`; response body names the target `url`. | `link_handlers.cpp::createLinkFromRequest` | Decision-log Q18 updated; subtask 07 (command) and Task 04.0 subtask 04 (REST key mapping). |
| Preview REST/CLI body is the **reduced** `serializeLinkPreviewJson`, not `LinkView`; REST stats has two shapes (range mode via `GetLinkStats`, and a lifetime-counters body when `from`/`to`/`bucket` are not all present). The catalog says "result is LinkView / LinkStatsView". | `link_handlers.cpp`, `link_command_service.hpp` | Subtask 08 pins both shapes to keep C3; catalog 3.1 has been corrected accordingly. |
| REST maps `reserved_slug` to **409** and error codes `repository` / `invalid_request` / `internal_error`; the catalog table says `reserved_slug` 400 and uses `storage_failure` / `invalid_field` / `internal`. | `link_handlers.cpp::statusForAppError/codeForAppError` | Envelope codes are the AppErrorCode names (`from_app_error` in [01.0/02](/docs/roadmap/0006-unified-command-model-and-tui/01.0-command-model-core/02-error-code-status-table.md), which already uses 409 for `reserved_slug` and carries the legacy REST columns). Commands never emit HTTP codes; Task 04.0 reads the legacy columns. The catalog error table has been corrected (409, plus a legacy resource-route column); Task 08.0 subtask 03 re-checks it. |

## Subtasks

| # | Document | Status | Depends on | Blocks |
|---|----------|--------|------------|--------|
| 01 | [ListLinksQuery extension, eval helper, `ListLinksPage`](01-list-links-query-and-page.md) | ⬜ Not started | none | 02, 12, 05.0/10 |
| 02 | [Contract tests for extended listing](02-list-links-page-contract-tests.md) | ⬜ Not started | 01 | 12 |
| 03 | [`IClickEventRepository::ListEvents` and in-memory impl](03-click-event-listing-interface-and-memory.md) | ⬜ Not started | none | 04, 16 |
| 04 | [`ListEvents` for the SQL repository](04-click-event-listing-sql.md) | ⬜ Not started | 03 | 16 |
| 05 | [Link command codec helpers](05-link-command-codec-helpers.md) | ⬜ Not started | 01.0/02, 01.0/05, 01.0/06, 01.0/07 | 07, 08, 09, 10, 05.0/08 |
| 06 | [Dry-run validation methods on `LinkCommandService`](06-service-dry-run-validation.md) | ⬜ Not started | none | 07, 09, 05.0/08 |
| 07 | [`link.create` command](07-link-create-command.md) | ⬜ Not started | 05, 06, 01.0/05, 01.0/06, 01.0/07 | 11, 05.0/08 |
| 08 | [`link.get`, `link.preview`, `link.stats` commands](08-link-get-preview-stats-commands.md) | ⬜ Not started | 05, 01.0/05, 01.0/06, 01.0/07 | 11, 04.0/03, 06.0/13 |
| 09 | [`link.update` command](09-link-update-command.md) | ⬜ Not started | 05, 06, 01.0/05, 01.0/06, 01.0/07 | 11, 04.0/05 |
| 10 | [`link.delete`, `link.enable`, `link.disable`, `link.restore`](10-link-lifecycle-commands.md) | ⬜ Not started | 05, 01.0/05, 01.0/06, 01.0/07 | 11, 04.0/06 |
| 11 | [Register the nine commands and REST parity gate](11-register-link-commands-and-rest-parity.md) | ⬜ Not started | 07, 08, 09, 10, 02.0/07 | 14, 18, 04.0/03, 04.0/04, 04.0/05, 04.0/14, 06.0/12, 06.0/13 |
| 12 | [`ILinkLister` port and repository-backed lister](12-link-lister-port-and-repository-impl.md) | ⬜ Not started | 01, 02, 02.0/02 | 13, 14, 16, 05.0/10 |
| 13 | [Lister for the default (legacy) store](13-legacy-link-lister.md) | ⬜ Not started | 12 | 18 |
| 14 | [`link.list` command](14-link-list-command.md) | ⬜ Not started | 11, 12, 01.0/05, 01.0/06, 01.0/07 | 18, 04.0/07, 05.0/10, 06.0/12, 06.0/17 |
| 15 | [`ITableSource` port, `db.tables`, `db.rows`](15-table-source-port-and-db-commands.md) | ⬜ Not started | 01.0/05, 01.0/06, 01.0/07 | 16, 05.0/21, 06.0/14, 06.0/17 |
| 16 | [Links and click-events table sources](16-links-and-click-events-table-sources.md) | ⬜ Not started | 03, 04, 12, 15 | 18, 06.0/14 |
| 17 | [`system.info` command](17-system-info-command.md) | ⬜ Not started | 01.0/05, 01.0/06, 01.0/07, 02.0/10 | 18, 06.0/17 |
| 18 | [Register browse commands and security regression tests](18-register-browse-commands-and-security-tests.md) | ⬜ Not started | 11, 13, 14, 16, 17, 01.0/09, 01.0/10, 02.0/12 | 04.0/02, 04.0/07, 04.0/09, 04.0/13, 04.0/15, 05.0/20, 06.0/19, Task 07.0 |

Dependency mini-graph (`->` = must finish first; `02.0/x` and `01.0/x` are other tasks' subtasks):

```text
Ports:      01 -> 02 ------------------+
            03 -> 04 ---------------+   |
Link cmds:  05 -+-> 07 -+           |   |
            06 -+-> 09 -+-> 11 -----|---|--+
            05 ---> 08 -+           |   |  |
            05 ---> 10 -+           |   |  |
Browse:     01,02,02.0/02 -> 12 -> 13 -> 18 ; 12 -> 14 -> 18      (11 also -> 14, 18)
            01.0/05-07 -> 15 -> 16 (needs 03, 04, 12) -> 18 ; 15 -> 05.0/21 (jobs table)
            01.0/05-07, 02.0/10 -> 17 -> 18
            01.0/09-10, 02.0/12 -> 18 (one registry builder + audit sink) -> 04.0, 05.0/20, 06.0/19
```

Subtasks 01-04 and 05-06 and 15 can start in parallel; 07-10 need 05 (and 06 for 07 and 09).

## Key constraints

- C1/C3: each command is one descriptor + one handler; `result` of the nine
  existing commands is produced by the **same serializer functions** the REST
  handlers call (`app::serializeLinkViewJson`, `serializeLinkPreviewJson`,
  `serializeLinkStatsJson`), never re-implemented. Characterization tests
  (`10_link_handlers` etc.) stay green.
- C6: `db.*` is allowlist-by-construction (only registered `ITableSource`s exist);
  never auth/session tables; `click_events` exposes `client_id_hash` only;
  `system.info` emits backend **names** only (no paths, addresses, DSNs, salts).
- C8/C9: no JSON library, C++17 only, every new `.cpp` appended to
  `COMMON_CPP_SOURCES` in `sources.cmake`, every new test appended to a source list
  in `sources.cmake` (the lists live there, not in `CMakeLists.txt`; only the
  `register_labeled_cpp_tests(...)` call for a *new* list goes into `CMakeLists.txt`).
- C10: nothing under `src/http/handlers/redirect_handlers.cpp` or `LinkService::Resolve` changes.
- Extend ports, do not bypass them: no command handler includes a SOCI, SQLite or
  Postgres header.
- `ArgSpec` caps on the nine existing commands are set to the body cap (`max_length = 65536`, `max_items = 4096`) so the service, not the registry,
  produces the validation errors REST returns today (subtasks 07, 09; contract C3).
- Test numbering (plan.md "Test file prefixes"): `tests/unit/commands/20-39` (uses 20-30; 01.0 owns 01-19, 04.0 40-49,
  05.0 50-69, 07.0 70-89); `tests/unit/app/40-59` (uses 40-48; existing 01-18, Task 02.0 takes 19-39, Task 05.0 60-69);
  `tests/contract/metadata/09-11`; `tests/unit/storage/listing/01-02` (new dir, list `LISTING_UNIT_SOURCES` owned by subtask 01).
- C11: `composition::build_command_runtime` (subtask 18) is the only registry builder; it installs the
  `LoggerAuditSink` (Task 01.0 subtask 09) and is extended for jobs by Task 05.0 subtask 20.

## Exit criteria

1. `ctest --test-dir cmake-build -L "unit|contract" --output-on-failure` is green with
   all new tests, including contract suites on in-memory and SQLite (Postgres when
   `URL_SHORTENER_TEST_POSTGRES_DSN` is set).
2. For each of the nine existing commands the registry output equals the REST
   handler body for the same state (parity test, subtask 11).
3. `link.list` returns filtered, sorted, paged results with a correct `total` on every
   backend; page size is capped at 500.
4. `db.rows {"table":"users"}` and any non-registered table yield `not_found`;
   `system.info` output contains no DSN, path, key or salt (subtask 16 tests).
5. `grep -rn "soci\|sqlite3\|libpq" src/commands include/url_shortener/commands` is empty.
