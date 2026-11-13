#pragma once

#include <mutex>
#include <vector>

#include "url_shortener/storage/i_analytics_sink.hpp"

/**
 * @brief In-memory `IAnalyticsSink` for deterministic tests; appends
 * every emitted event to an internal vector. `Emit` and `Flush`
 * always succeed. The accumulated events are readable via `Snapshot`.
 *
 * Thread-safety: `Emit`, `Flush`, and `Snapshot` are safe to call
 * concurrently; all accesses to `events_` are guarded by `mutex_`.
 *
 * @see IAnalyticsSink
 */
class InMemoryAnalyticsSink final : public IAnalyticsSink
{
public:
    /**
     * @brief Appends `event` to the internal store. Always succeeds.
     * @param event Event to store; a copy is taken.
     * @param[out] error If non-null, set to `AnalyticsError::none`.
     * @return Always `true`.
     * @see IAnalyticsSink::Emit
     */
    bool Emit(const LinkAccessEvent& event,
              AnalyticsError* error = nullptr) override;

    /**
     * @brief No-op flush; always succeeds.
     * @param[out] error If non-null, set to `AnalyticsError::none`.
     * @return Always `true`.
     * @see IAnalyticsSink::Flush
     */
    bool Flush(AnalyticsError* error = nullptr) override;

    /**
     * @brief Returns a snapshot copy of all events emitted so far.
     * @return A copy of the internal event vector, ordered by
     * insertion (i.e. `Emit` call order).
     */
    std::vector<LinkAccessEvent> Snapshot() const;

private:
    ///< Guards `events_`; `mutable` so that `Snapshot` (const) can
    ///< lock it.
    mutable std::mutex mutex_;

    ///< Accumulates events in `Emit` call order. Guarded by
    ///< `mutex_`.
    std::vector<LinkAccessEvent> events_;
};
