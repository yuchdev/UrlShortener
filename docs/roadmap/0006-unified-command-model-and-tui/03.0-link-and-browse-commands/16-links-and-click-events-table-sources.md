# 16 - Links and click-events table sources

**Parent task:** 03.0 Link and browse commands on the registry
**State:** ⬜ Not started
**Depends on:** 03.0/03, 03.0/04 (`ListEvents`), 03.0/12 (`ILinkLister`), 03.0/15 (`ITableSource`, `TableCatalog`)
**Blocks:** 18, 06.0/14
**Estimated size:** M
**Suggested agent:** cpp-expert

## Objective

Implement the two port-backed logical tables of milestone decision Q7: `links` (over `ILinkLister`) and
`click_events` (over `IClickEventRepository::ListEvents`). Each source fixes its own column list, so the
columns that exist are exactly the columns that may be exposed; `click_events` exposes `client_id_hash` and
no other client identifier.

## Dependencies

- `ITableSource`/`TableCatalog` (subtask 15), `ILinkLister` (subtask 12), `ListEvents`/`ClickEventQuery` (subtasks 03-04).
- The third allowlisted table, `jobs`, is a separate `ITableSource` added to the same catalog by Task 05.0 subtask 21
  (which depends on subtask 15, not on this one).
- Verified `ClickEvent` fields (`analytics/click_event.hpp`): `event_id, occurred_at, slug, link_id, domain,
  status_code, referrer, user_agent, client_id_hash`. Free-text fields (`referrer`, `user_agent`) are untrusted
  and length-bounded upstream by `ClickEventSanitizer`; they must be JSON-escaped.
- Verified timestamp helper: `url_shortener::formatTimestamp(std::chrono::system_clock::time_point)` (`core/utils.h`;
  include only from the .cpp because that header pulls in Beast).
- **Open wiring question (verified facts):** the click repository the running server writes to is the
  process-global `analyticsClickRepository()` (an `InMemoryClickEventRepository`, `src/http/request_handlers.cpp`);
  `StorageConfig::analytics.backend` is only `noop` today and no composition code constructs a
  `SqlClickEventRepository` for the server. Consequence: in CLI/TUI/stdio processes the `click_events` table reads that
  process's own (empty) in-memory repository, and cross-process viewing of clicks needs a configured persistent
  click repository (not planned by 02.0 as written). Decision recorded here: register `click_events` with whatever
  `IClickEventRepository&` the composition root hands over; the composition root for this milestone passes
  `analyticsClickRepository()`. Raise with the product owner (see hand-off notes).

## Files affected

| Path | Action | What changes |
|---|---|---|
| `include/url_shortener/app/links_table_source.hpp` | create | `LinksTableSource final : ITableSource`. |
| `src/app/links_table_source.cpp` | create | Implementation. Add to `COMMON_CPP_SOURCES`. |
| `include/url_shortener/app/click_events_table_source.hpp` | create | `ClickEventsTableSource final : ITableSource`. |
| `src/app/click_events_table_source.cpp` | create | Implementation. Add to `COMMON_CPP_SOURCES`. |
| `tests/unit/app/46_links_table_source.cpp` | create | Links source over a fake lister. |
| `tests/unit/app/47_click_events_table_source.cpp` | create | Click source over `InMemoryClickEventRepository`. |
| `sources.cmake` | modify | `APP_UNIT_SOURCES` += 24, 25; `COMMON_CPP_SOURCES` += two .cpp. |

## Classes and functions

```cpp
// namespace url_shortener::app
class LinksTableSource final : public ITableSource {
public:
    explicit LinksTableSource(const ILinkLister& lister);
    const std::string& name() const override;            // "links"
    bool writable() const override;                      // true (link.* commands exist)
    const std::vector<TableColumn>& columns() const override;
    Result<std::size_t> row_count() const override;      // lister with status=all, limit 1 -> total
    Result<TableRowsPage> rows(const TableQuery& query) const override;
private:
    const ILinkLister& m_lister;
    std::vector<TableColumn> m_columns;
};

class ClickEventsTableSource final : public ITableSource {
public:
    explicit ClickEventsTableSource(analytics::IClickEventRepository& repository);
    // name "click_events", writable() == false
    ...
};
```

Columns (fixed, in this order):

| table | columns (type; S = sortable, Q = searchable) |
|---|---|
| `links` | `id` string, `slug` string S Q, `url` string Q, `status` string, `enabled` bool, `redirect_type` string, `created_at` timestamp S, `updated_at` timestamp S, `expires_at` timestamp, `deleted_at` timestamp |
| `click_events` | `event_id` string, `occurred_at` timestamp S, `slug` string S Q, `link_id` string, `domain` string Q, `status_code` int S, `referrer` string, `user_agent` string, `client_id_hash` string |

`links` rows use `status=all` (the TUI wants to see everything), map `sort` (`slug|created_at|updated_at`) and
`q` to `LinkListQuery`, and emit flat scalar objects (no tags/metadata/campaign, no `short_url`; keys equal the
column names). `click_events` rows map to `ClickEventQuery` (`text = q`, `sort_by`, `descending`, window) and
format `occurred_at` with `formatTimestamp`. `null` for absent optional values. Source errors map to
`AppErrorCode::storage_failure` (never leaking `AnalyticsError::message`, which may contain driver text; use a
fixed detail such as `click event listing failed`).

## Implementation steps

1. Implement `LinksTableSource` (small adapter over `ILinkLister`).
2. Implement `ClickEventsTableSource`; clamp `limit` to `kMaxClickEventPageSize`.
3. Tests (below); register; build; run.
4. Do not register the sources anywhere yet: composition is subtask 18.

## Commands

```powershell
cmake --build cmake-build --target app__46_links_table_source app__47_click_events_table_source
ctest --test-dir cmake-build -R "^app__2[45]_" --output-on-failure
```

## Tests

| Test file | Target | Label | Cases |
|---|---|---|---|
| `tests/unit/app/46_links_table_source.cpp` | `app__46_links_table_source` | unit | metadata (`name`, `writable`, exact column list and order); rows contain exactly the column keys (assert key set per row); `status` of deleted/disabled/expired links appears (status=all); sort and q forwarded; `row_count` equals lister `total`; lister error -> `storage_failure`; a hostile slug/URL with quotes and control characters stays valid JSON. |
| `tests/unit/app/47_click_events_table_source.cpp` | `app__47_click_events_table_source` | unit | exact column list; seeded events appear newest first; row key set contains `client_id_hash` and **no** key matching `ip`, `address`, `client_id`, `fingerprint`; sort/q/status window forwarded; `limit` above 500 clamped; empty repository -> total 0; a repository whose `ListEvents` is unsupported (default impl) -> `storage_failure` with a fixed detail that does not contain the repository message; `referrer`/`user_agent` with quotes and newlines stay valid JSON; `occurred_at` rendered as RFC 3339 UTC. |

Manual check (after 16 and a server): `db.rows {"table":"click_events","limit":5}` shows hashed client ids only.

## Constraints

- C6: column lists are literal constants in the source; never derived from the repository result or from `ClickEvent`
  reflection. No `owner`, `attributes` or raw identifiers.
- Extend ports, do not bypass them: no SQL, SOCI, or driver include in these files.
- C10: nothing here touches the redirect path or `RedirectAnalyticsHook`.
- C++17; `lower_case`/`m_` naming; every new `.cpp` in `COMMON_CPP_SOURCES`.

## Success criteria

1. Both tests pass.
2. The click-event row key-set test fails if anyone adds a non-hashed client identifier column.
3. `git grep -n "include.*\(soci\|sqlite\|libpq\)" src/app` shows nothing new.

## Out of scope / risks

- Persistent click storage wiring (see Dependencies) - needs a product decision; without it the TUI's click table is empty for
  CLI/TUI processes. Flagged to the milestone owner.
- `links.writable == true` is informational; there is no row-edit path in `db.*`.
- Large `click_events` tables: `LIKE` scans (subtask 04 risk).
