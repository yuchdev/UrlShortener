#pragma once

#include <mutex>
#include <vector>

#include "url_shortener/analytics/i_click_event_repository.hpp"

namespace url_shortener::storage::memory
{

/**
 * @brief Thread-safe in-memory `IClickEventRepository` for tests and
 * local development. Stores all events in a single vector; aggregate
 * queries are computed by scanning the full event set on each call.
 *
 * All three methods always succeed (they always return `true` and
 * ignore the `error` out-param). `GetAggregateStats` returns `false`
 * only when `stats` is null (a programming error).
 *
 * Thread-safety: all public methods acquire `mu_` exclusively on
 * every call.
 *
 * @see analytics::IClickEventRepository
 */
class InMemoryClickEventRepository final
    : public analytics::IClickEventRepository
{
public:
    /**
     * @brief Appends all events in `events` to the internal store.
     * Always succeeds; `error` is not written.
     * @param events Events to append; may be empty.
     * @param[out] error Unused; not written.
     * @return Always `true`.
     * @see analytics::IClickEventRepository::InsertBatch
     */
    bool InsertBatch(const std::vector<analytics::ClickEvent>& events,
                     analytics::AnalyticsError* error = nullptr) override;

    /**
     * @brief Scans all stored events and computes aggregate counts
     * matching `query`. Filters by `query.slug`, `query.link_id`,
     * and the half-open time range `[query.from, query.to)`. Counts
     * status codes in the 3xx range as `successful_redirects`. Time
     * buckets are sorted ascending by `bucket_start`.
     * @param query Aggregate query parameters.
     * @param[out] stats Receives the computed statistics on success;
     * must be non-null (`false` is returned immediately if null).
     * @param[out] error Unused; not written.
     * @return `true` on success; `false` if `stats` is null.
     * @see analytics::IClickEventRepository::GetAggregateStats
     */
    bool GetAggregateStats(const analytics::AggregateQuery& query,
                           analytics::AggregateStats* stats,
                           analytics::AnalyticsError* error = nullptr) override;

    /**
     * @brief Removes all events whose `occurred_at` is strictly
     * before `cutoff` (i.e. `occurred_at < cutoff`). Always
     * succeeds; `error` is not written.
     * @param cutoff Deletion boundary; events at exactly `cutoff`
     * are retained.
     * @param[out] error Unused; not written.
     * @return Always `true`.
     * @see analytics::IClickEventRepository::DeleteOlderThan
     */
    bool DeleteOlderThan(analytics::Timestamp cutoff,
                         analytics::AnalyticsError* error = nullptr) override;

private:
    ///< All stored click events, in insertion order. Guarded by
    ///< `mu_`.
    std::vector<analytics::ClickEvent> events_;

    ///< Guards `events_` on all public method calls.
    std::mutex mu_;
};

}  // namespace url_shortener::storage::memory
