#pragma once

#include "url_shortener/analytics/analytics_config.hpp"
#include "url_shortener/analytics/analytics_metrics.hpp"
#include "url_shortener/analytics/click_event_builder.hpp"
#include "url_shortener/analytics/i_click_event_queue.hpp"

namespace url_shortener::analytics
{

/**
 * @brief Entry point the redirect hot path calls to record one redirect
 * attempt as analytics. Builds a sanitized `ClickEvent` from the given
 * context and hands it to the bounded queue for the background
 * `AnalyticsWorker` to persist later.
 *
 * Lifetime: does not own `config`, `queue`, or `metrics`; the caller
 * (composition root) must keep all three alive for at least as long as
 * this object exists. Thread-safety: `RecordRedirectAttempt` is safe to
 * call concurrently from multiple redirect-handling threads - it only
 * reads `config_` and delegates to `queue_`/`metrics_`, which are
 * themselves required to be safe for concurrent use.
 */
class AnalyticsService
{
public:
    /**
     * @brief Stores the given references; does no validation and starts
     * no background work.
     * @param config Analytics subsystem configuration; read on every
     * `RecordRedirectAttempt` call, not copied, so later mutation of the
     * referenced object is observed. Must outlive this object.
     * @param queue Destination queue for normalized click events; must
     * outlive this object.
     * @param metrics Metrics sink updated on every call; must outlive this
     * object.
     */
    AnalyticsService(const AnalyticsConfig& config,
                     IClickEventQueue& queue,
                     IAnalyticsMetrics& metrics) noexcept
        : config_(config)
        , queue_(queue)
        , metrics_(metrics)
    {
    }

    /**
     * @brief Builds a sanitized `ClickEvent` from `context` and enqueues
     * it for later persistence; updates enqueue/drop and latency metrics.
     *
     * Never throws and never blocks on anything but the queue's internal
     * lock - safe to call from the redirect fast path. A no-op if
     * `config_.enabled` is `false`. If `ClickEventBuilder::Build` fails
     * (e.g. the event does not pass `ClickEvent::Validate`, or
     * `ClientIdHasher::Hash` fails), the event is silently dropped before
     * it reaches the queue and no metric is recorded for it - this is
     * intentional best-effort behavior; analytics failures must never
     * affect the caller's redirect response.
     * @param context Redirect metadata to normalize into a `ClickEvent`;
     * not retained past this call.
     */
    void RecordRedirectAttempt(const RedirectEventContext& context) noexcept;

private:
    ///< Analytics configuration, read on every `RecordRedirectAttempt`
    ///< call (not copied). Not owned; must outlive this object.
    const AnalyticsConfig& config_;

    ///< Destination for built click events. Not owned; must outlive this
    ///< object and be safe for concurrent `TryEnqueue` calls.
    IClickEventQueue& queue_;

    ///< Metrics sink for enqueue/drop counts and latency. Not owned; must
    ///< outlive this object and be safe for concurrent use.
    IAnalyticsMetrics& metrics_;
};

}  // namespace url_shortener::analytics
