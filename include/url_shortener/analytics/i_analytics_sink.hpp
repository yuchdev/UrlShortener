#pragma once

#include "url_shortener/analytics/analytics_error.hpp"
#include "url_shortener/analytics/click_event.hpp"

namespace url_shortener::analytics
{

/**
 * @brief Optional forwarding sink for click events, in addition to the
 * primary bounded-queue pipeline (see `IClickEventQueue`).
 *
 * Implementations must be safe to call from whatever thread the redirect
 * hot path or the analytics worker runs on; this interface makes no
 * blocking guarantee either way, so an implementation that blocks must say
 * so in its own class comment.
 */
class IAnalyticsSink
{
public:
    /**
     * @brief Virtual destructor; implementations may be destroyed through
     * a base `IAnalyticsSink*`.
     */
    virtual ~IAnalyticsSink() = default;

    /**
     * @brief Forwards one normalized click event to the sink.
     * @param event The event to forward; the sink does not take ownership
     * and must not retain a reference beyond the call.
     * @param[out] error If non-null and the call fails, receives the
     * failure reason; left untouched on success.
     * @return `true` if the event was accepted by the sink; `false` if
     * the sink rejected or failed to forward it. A `false` return must
     * never throw and must never affect the caller's redirect response.
     */
    virtual bool Emit(const ClickEvent& event,
                      AnalyticsError* error = nullptr) = 0;

    /**
     * @brief Requests the sink flush any buffered events immediately.
     * @param[out] error If non-null and the flush fails, receives the
     * failure reason; left untouched on success.
     * @return `true` if the flush completed (or there was nothing to
     * flush); `false` if the flush failed. Best-effort: a `false` return
     * must never be treated as data loss by the caller without separately
     * checking sink-specific state.
     */
    virtual bool Flush(AnalyticsError* error = nullptr) = 0;
};

}  // namespace url_shortener::analytics
