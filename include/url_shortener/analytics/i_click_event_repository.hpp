#pragma once

#include <chrono>
#include <vector>

#include "url_shortener/analytics/aggregate_query.hpp"
#include "url_shortener/analytics/aggregate_stats.hpp"
#include "url_shortener/analytics/click_event.hpp"
#include "url_shortener/analytics/click_event_query.hpp"

namespace url_shortener::analytics
{

/**
 * @brief Storage port for click-event persistence and aggregate reads (see
 * `CLAUDE.md` "Storage: ports and adapters"). Implemented per backend
 * under `src/storage/` (in-memory, SQL); called by `AnalyticsWorker`
 * (writes), the analytics management-plane handlers (aggregate reads), and
 * `RetentionCleanupJob` (deletes). As with every port in this codebase,
 * expected failures are reported via the `bool` + `*error` out-param
 * convention, never by throwing.
 */
class IClickEventRepository
{
public:
    /**
     * @brief Virtual destructor; implementations may be destroyed through
     * a base `IClickEventRepository*`.
     */
    virtual ~IClickEventRepository() = default;

    /**
     * @brief Persists a batch of click events.
     * @param events Events to persist; may be empty.
     * @param[out] error If non-null and the call fails, receives a
     * backend-specific failure reason (observed implementations use
     * `AnalyticsErrorCode::repository`); left untouched on success.
     * @return `true` if the batch was accepted by the backend; `false` if
     * it failed. This interface does not itself guarantee all-or-nothing
     * atomicity across the batch or across backends - consult the
     * specific adapter (e.g. `src/storage/sql/`, `src/storage/memory/`)
     * for its actual partial-failure behavior.
     */
    virtual bool InsertBatch(const std::vector<ClickEvent>& events,
                             AnalyticsError* error = nullptr) = 0;

    /**
     * @brief Computes aggregate statistics for the events matching `query`.
     * @param query Filter/time-range parameters; should already be
     * validated via `AggregateQueryValidator::Validate` - behavior for an
     * unvalidated query (e.g. `from > to`) is backend-defined.
     * @param[out] stats Receives the computed `AggregateStats` on success;
     * left unmodified on failure. Must be non-null.
     * @param[out] error If non-null and the call fails, receives a
     * backend-specific failure reason; left untouched on success.
     * @return `true` if `stats` is non-null and the query executed
     * successfully; `false` otherwise (including a null `stats`).
     */
    virtual bool GetAggregateStats(const AggregateQuery& query,
                                   AggregateStats* stats,
                                   AnalyticsError* error = nullptr) = 0;

    /**
     * @brief Deletes events older than `cutoff`, for retention cleanup.
     * @param cutoff Deletes events whose `occurred_at` is strictly before
     * this timestamp (observed implementations use `occurred_at < cutoff`,
     * i.e. `cutoff` itself is retained).
     * @param[out] error If non-null and the call fails, receives a
     * backend-specific failure reason; left untouched on success.
     * @return `true` if the delete completed (including deleting zero
     * rows); `false` if it failed. This interface does not report how
     * many rows were deleted - see `RetentionCleanupResult::deleted_count`
     * for the current caller-side caveat about count tracking.
     */
    virtual bool DeleteOlderThan(Timestamp cutoff,
                                 AnalyticsError* error = nullptr) = 0;

    /**
     * @brief Reads a bounded, ordered page of individual click events
     * matching `query`, for the management-plane browse surface. This is a
     * read off the redirect hot path and must never be called from it.
     *
     * Non-pure by design: the default body reports "unsupported" so that
     * test fakes and backends that have not implemented listing (e.g. the
     * SQL adapter before subtask 04) keep compiling and fail cleanly rather
     * than returning misleading data. Implementations that support listing
     * must clamp `query.limit` to `kMaxClickEventPageSize`, reject a
     * `query.text` longer than `kMaxClickEventTextFilterLength`, apply the
     * filters (slug, text, status, half-open `[from, to)` range), order by
     * `query.sort_by`/`query.descending` with `event_id` ascending as the
     * final tie-break, and set `page->total` to the match count before
     * `offset`/`limit`.
     *
     * @param query Filter, ordering, and paging parameters. `query.from >
     * query.to` (both set) is an invalid range, and a `query.text` longer than
     * `kMaxClickEventTextFilterLength` is invalid.
     * @param[out] page Receives the window and total on success; must be
     * non-null. Left unmodified on failure.
     * @param[out] error If non-null and the call fails, receives the failure
     * reason; left untouched on success. The default body writes
     * `AnalyticsErrorCode::repository` ("not supported"); supporting
     * implementations write `AnalyticsErrorCode::validation` when `page` is
     * null, the range is inverted, or `query.text` exceeds
     * `kMaxClickEventTextFilterLength`, and `AnalyticsErrorCode::repository` on
     * a backend failure.
     * @return `true` if the query executed and `page` was populated; `false`
     * when this repository does not support listing (default body), when
     * `page` is null, when `query.from > query.to`, when `query.text` exceeds
     * `kMaxClickEventTextFilterLength`, or on a backend failure.
     * @note No raw client identifier is ever returned - a listed `ClickEvent`
     * exposes only the HMAC `client_id_hash` (C6).
     */
    virtual bool ListEvents(const ClickEventQuery& query,
                            ClickEventPage* page,
                            AnalyticsError* error = nullptr)
    {
        (void)query;
        (void)page;
        if (error != nullptr) {
            error->code = AnalyticsErrorCode::repository;
            error->message = "ListEvents is not supported by this repository";
        }
        return false;
    }
};

}  // namespace url_shortener::analytics
