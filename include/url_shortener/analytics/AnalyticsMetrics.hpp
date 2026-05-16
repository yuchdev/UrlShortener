#pragma once

#include <cstdint>

namespace url_shortener::analytics {

struct AnalyticsMetricNames {
    static constexpr const char* events_enqueued_total = "analytics_events_enqueued_total";
    static constexpr const char* events_dropped_total = "analytics_events_dropped_total";
    static constexpr const char* events_persisted_total = "analytics_events_persisted_total";
    static constexpr const char* worker_failures_total = "analytics_worker_failures_total";
    static constexpr const char* queue_depth = "analytics_queue_depth";
    static constexpr const char* enqueue_latency_us = "analytics_enqueue_latency_us";
};

class IAnalyticsMetrics { public: virtual ~IAnalyticsMetrics() = default; virtual void OnEnqueued() noexcept = 0; virtual void OnDropped() noexcept = 0; virtual void OnPersisted(std::uint64_t count) noexcept = 0; virtual void OnWorkerFailure() noexcept = 0; virtual void SetQueueDepth(std::uint64_t depth) noexcept = 0; virtual void ObserveEnqueueLatencyUs(std::uint64_t) noexcept = 0; };
class NullAnalyticsMetrics final : public IAnalyticsMetrics { public: void OnEnqueued() noexcept override {} void OnDropped() noexcept override {} void OnPersisted(std::uint64_t) noexcept override {} void OnWorkerFailure() noexcept override {} void SetQueueDepth(std::uint64_t) noexcept override {} void ObserveEnqueueLatencyUs(std::uint64_t) noexcept override {} };

} // namespace
