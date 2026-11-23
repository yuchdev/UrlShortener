#pragma once

#include "url_shortener/storage/errors/analytics_error.hpp"
#include "url_shortener/storage/models/link_access_event.hpp"

/**
 * @brief Best-effort analytics sink for redirect-access events (see
 * `CLAUDE.md` "Storage: ports and adapters" and `LinkAccessEvent`).
 *
 * The sink is fail-open: a `false` return from either method must
 * never affect redirect response behavior or latency. Implementations
 * may buffer events internally and flush them asynchronously.
 *
 * Thread-safety: implementations must be safe to call concurrently
 * from any thread that handles redirect requests.
 */
class IAnalyticsSink
{
public:
    /**
     * @brief Virtual destructor; implementations may be destroyed
     * through a base `IAnalyticsSink*`.
     */
    virtual ~IAnalyticsSink() = default;

    /**
     * @brief Forwards one redirect-access event to the sink.
     * @param event Event to forward; the sink must not retain a
     * reference to it beyond this call.
     * @param[out] error If non-null and the call fails, receives the
     * failure category (`unavailable`, `queue_full`, or
     * `permanent_failure`); left untouched on success.
     * @return `true` if the event was accepted; `false` if the sink
     * was unavailable, the event queue was full, or a permanent
     * failure occurred. A `false` return must not affect the
     * caller's redirect response.
     */
    virtual bool Emit(const LinkAccessEvent& event,
                      AnalyticsError* error = nullptr) = 0;

    /**
     * @brief Requests the sink flush any buffered events immediately.
     * @param[out] error If non-null and the flush fails, receives the
     * failure category; left untouched on success (including when
     * there was nothing to flush).
     * @return `true` if all buffered events were flushed (or there
     * was nothing to flush); `false` if the flush failed.
     */
    virtual bool Flush(AnalyticsError* error = nullptr) = 0;
};
