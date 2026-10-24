#pragma once

#include <cstdint>

namespace url_shortener::analytics
{

/**
 * @brief Canonical metric name strings published by `IAnalyticsMetrics`
 * implementations, so metrics-backend adapters (e.g. Prometheus) and their
 * tests agree on exact names without duplicating string literals.
 */
struct AnalyticsMetricNames
{
    ///< Counter name for `IAnalyticsMetrics::OnEnqueued`.
    static constexpr const char* events_enqueued_total = "analytics_events_enqueued_total";  
    
    ///< Counter name for `IAnalyticsMetrics::OnDropped`.
    static constexpr const char* events_dropped_total = "analytics_events_dropped_total";
    
    ///< Counter name for `IAnalyticsMetrics::OnPersisted`.
    static constexpr const char* events_persisted_total = "analytics_events_persisted_total";  

    ///< Counter name for `IAnalyticsMetrics::OnWorkerFailure`.
    static constexpr const char* worker_failures_total = "analytics_worker_failures_total";  

    ///< Gauge name for
    ///< `IAnalyticsMetrics::SetQueueDepth`.
    static constexpr const char* queue_depth = "analytics_queue_depth";  

    ///< Histogram/summary name for `IAnalyticsMetrics::ObserveEnqueueLatencyUs`.
    static constexpr const char* enqueue_latency_us = "analytics_enqueue_latency_us";

    ///< Counter name reserved for retention-deletion counts.
    ///< Note: no code in this module currently reports a value under this name -
    ///< `RetentionCleanupJob` does not call a dedicated "deleted" metric method.
    static constexpr const char* retention_deleted_total = "analytics_retention_deleted_total";  

    ///< Counter name reserved for retention failures.
    ///< Note: `RetentionCleanupJob::Run` reports a failed cleanup pass via
    ///< `IAnalyticsMetrics::OnWorkerFailure` (the generic worker-failure counter),
    ///< not a dedicatedretention-failure method under this name.
    static constexpr const char* retention_failures_total = "analytics_retention_failures_total";  
};

/**
 * @brief Metrics sink interface for the analytics pipeline: enqueue/drop/
 * persist counters, a queue-depth gauge, and an enqueue-latency observation.
 *
 * Thread-safety: every method is `noexcept` and must be safe to call
 * concurrently from multiple threads without external synchronization -
 * `AnalyticsService::RecordRedirectAttempt` (redirect hot path) calls
 * `OnEnqueued`/`OnDropped`/`SetQueueDepth`/`ObserveEnqueueLatencyUs`, while
 * the background `AnalyticsWorker` calls `OnPersisted`/`OnWorkerFailure`/
 * `SetQueueDepth` from its own thread; implementations are responsible for
 * their own internal synchronization (e.g. atomics).
 *
 * Method contracts:
 * - `OnEnqueued()`: a click event was accepted into the bounded queue.
 * - `OnDropped()`: a click event was rejected because the queue was full (`EnqueueResult::DroppedFull`).
 * - `OnPersisted(count)`: `count` events from one batch were successfully written to the repository.
 * - `OnWorkerFailure()`: a batch persist attempt exhausted its retry budget without succeeding, or a `RetentionCleanupJob::Run` delete pass failed.
 * - `SetQueueDepth(depth)`: reports the current number of buffered, not-yet-persisted events; a point-in-time gauge, not cumulative.
 * - `ObserveEnqueueLatencyUs(microseconds)`: records how long one `AnalyticsService::RecordRedirectAttempt` call spent building and enqueueing a single event.
 */
class IAnalyticsMetrics
{
public:
    virtual ~IAnalyticsMetrics() = default;
    virtual void OnEnqueued() noexcept = 0;
    virtual void OnDropped() noexcept = 0;
    virtual void OnPersisted(std::uint64_t count) noexcept = 0;
    virtual void OnWorkerFailure() noexcept = 0;
    virtual void SetQueueDepth(std::uint64_t depth) noexcept = 0;
    virtual void ObserveEnqueueLatencyUs(std::uint64_t) noexcept = 0;
};

/**
 * @brief No-op `IAnalyticsMetrics` implementation; every method is a
 * trivial override that discards its input. Lets a caller satisfy
 * `AnalyticsService`/`AnalyticsWorker`/`RetentionCleanupJob`'s
 * `IAnalyticsMetrics&` dependency without a real metrics backend wired up
 * (e.g. in tests), without null-checking the dependency on every call.
 */
class NullAnalyticsMetrics final : public IAnalyticsMetrics
{
public:
    void OnEnqueued() noexcept override {}
    void OnDropped() noexcept override {}
    void OnPersisted(std::uint64_t) noexcept override {}
    void OnWorkerFailure() noexcept override {}
    void SetQueueDepth(std::uint64_t) noexcept override {}
    void ObserveEnqueueLatencyUs(std::uint64_t) noexcept override {}
};

}  // namespace url_shortener::analytics
