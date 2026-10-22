# 03 - IClickEventRepository::ListEvents and the in-memory implementation

**Parent task:** 03.0 Link and browse commands on the registry
**State:** ⬜ Not started
**Depends on:** none (analytics/storage layer only)
**Blocks:** 04, 16
**Estimated size:** S
**Suggested agent:** cpp-expert

## Objective

Add a bounded, paged read of individual click events to the analytics port so the
`click_events` logical table (`db.rows`) can exist, and implement it for
`InMemoryClickEventRepository`. The method is a **non-pure** virtual so the five test
fakes that subclass the interface keep compiling and unsupported backends fail cleanly.

## Dependencies

- `IClickEventRepository` today has exactly `InsertBatch`, `GetAggregateStats`,
  `DeleteOlderThan` (`include/url_shortener/analytics/i_click_event_repository.hpp`, namespace
  `url_shortener::analytics`). All are non-const, return `bool`, and report through
  `AnalyticsError*` (`AnalyticsErrorCode::{none,validation,queue,repository,worker,retention}`).
- Implementers (verified): `InMemoryClickEventRepository`
  (`include/url_shortener/storage/memory/in_memory_click_event_repository.hpp`), `SqlClickEventRepository`
  (subtask 04), fakes in `tests/unit/analytics/10_analytics_worker_batch_flush.cpp`,
  `11_..._interval_flush.cpp`, `12_..._retry_bounds.cpp`, `13_retention_cleanup_job.cpp`,
  `15_analytics_disabled_mode.cpp`.
- `ClickEvent` (`analytics/click_event.hpp`) stores `client_id_hash` only; no raw identifier
  exists to leak (C6).

## Files affected

| Path | Action | What changes |
|---|---|---|
| `include/url_shortener/analytics/click_event_query.hpp` | create | `ClickEventSort`, `ClickEventQuery`, `ClickEventPage`, `kMaxClickEventPageSize`. |
| `include/url_shortener/analytics/i_click_event_repository.hpp` | modify | Add non-pure `ListEvents` with a default "unsupported" body. |
| `include/url_shortener/storage/memory/in_memory_click_event_repository.hpp` | modify | Declare `ListEvents` override. |
| `src/storage/memory/in_memory_click_event_repository.cpp` | modify | Implement it (already in `COMMON_CPP_SOURCES`). |
| `tests/unit/storage/memory/17_in_memory_click_event_list_events.cpp` | create | In-memory behavior. |
| `tests/unit/analytics/16_click_event_list_events_default.cpp` | create | Default implementation on a fake. |
| `sources.cmake` | modify | Append both tests to `ANALYTICS_UNIT_SOURCES`. |

## Classes and functions

```cpp
// click_event_query.hpp   namespace url_shortener::analytics
constexpr std::size_t kMaxClickEventPageSize = 500;

enum class ClickEventSort { occurred_at, slug, status_code };

struct ClickEventQuery {
    std::optional<std::string> slug;          // exact
    std::optional<std::string> text;          // case-insensitive substring of slug OR domain
    std::optional<uint16_t> status_code;      // exact
    std::optional<Timestamp> from;            // inclusive
    std::optional<Timestamp> to;              // exclusive
    ClickEventSort sort_by = ClickEventSort::occurred_at;
    bool descending = true;                   // newest first
    std::size_t offset = 0;
    std::size_t limit = 50;                   // clamped to kMaxClickEventPageSize by every implementation
};

struct ClickEventPage {
    std::vector<ClickEvent> items;            // the requested window
    std::uint64_t total = 0;                  // matches before offset/limit
};

// i_click_event_repository.hpp (addition)
virtual bool ListEvents(const ClickEventQuery& query, ClickEventPage* page,
                        AnalyticsError* error = nullptr)
{
    (void)query; (void)page;
    if (error != nullptr) {
        error->code = AnalyticsErrorCode::repository;
        error->message = "ListEvents is not supported by this repository";
    }
    return false;
}
```

Behavior contract: `page == nullptr` -> `false`, `AnalyticsErrorCode::validation`; `from > to`
-> `false`, `validation`; sort is stable with `event_id` ascending as the final tie-break;
`limit` is clamped, `limit == 0` yields empty `items` and a correct `total`.
`InMemoryClickEventRepository::ListEvents` takes `mu_` like its siblings and copies
only the window.

## Implementation steps

1. Create `click_event_query.hpp` (includes `analytics/click_event.hpp`, `<optional>`, `<vector>`).
2. Add the default `ListEvents` to the interface header.
3. Implement and declare the in-memory override (filter -> count -> stable sort -> window).
4. Write both tests; append to `ANALYTICS_UNIT_SOURCES`.
5. Rebuild the five existing analytics fakes' tests to prove they still compile unchanged.

## Commands

```bash
cmake --build cmake-build --target memory__17_in_memory_click_event_list_events analytics__16_click_event_list_events_default
ctest --test-dir cmake-build -R "^(memory__17_in_memory_click_event_list_events|analytics__16_click_event_list_events_default)$" --output-on-failure
# regression: fakes + existing in-memory repository tests
ctest --test-dir cmake-build -R "^(analytics__1[0-5]_|memory__0[1-3]_|memory__16_)" --output-on-failure
```

## Tests

| Test file | Target | Label | Cases |
|---|---|---|---|
| `tests/unit/storage/memory/17_in_memory_click_event_list_events.cpp` | `memory__17_in_memory_click_event_list_events` | unit | empty repo -> empty page, total 0; filter by slug/status/time window (`from` inclusive, `to` exclusive boundary); `text` over slug and domain; default order newest first; each `sort_by` both directions; tie on `occurred_at` ordered by `event_id`; `offset`/`limit`/`total`; `limit = 100000` clamped to 500; `limit = 0`; `from > to` -> `validation`; `page == nullptr`; returned events contain `client_id_hash` and no other client identifier field exists. |
| `tests/unit/analytics/16_click_event_list_events_default.cpp` | `analytics__16_click_event_list_events_default` | unit | a minimal subclass without an override -> `ListEvents` returns `false` with `AnalyticsErrorCode::repository` and the "not supported" message; `error == nullptr` is safe. |

## Constraints

- C6: no new field on `ClickEvent`; nothing here can return a raw client identifier.
- C++17 only; naming per `.clang-tidy`; interfaces depend on abstractions only.
- The analytics pipeline stays best-effort and off the redirect path: this method is a
  management-plane read and must never be called from `RedirectAnalyticsHook`.
- Do not change `InsertBatch`/`GetAggregateStats`/`DeleteOlderThan` signatures.

## Success criteria

1. Both new tests pass; the five pre-existing fakes compile unmodified
   (`ctest -R "^analytics__1[0-5]_"` green).
2. Page size above 500 is clamped to 500 (test case asserts `items.size() == 500` with 600 stored events).
3. `grep -rn "ListEvents" src/http src/analytics/analytics_service.cpp` is empty (not wired into the hot path).

## Out of scope / risks

- Which repository *instance* `db.rows` reads (the process-global `analyticsClickRepository()` in
  `src/http/request_handlers.cpp` is an in-memory repository, separate from any configured
  analytics backend) is decided in subtask 16.
- SQL implementation is subtask 04. The in-memory repository keeps an unbounded vector; listing
  copies only the window, but the filter/sort is O(n).
