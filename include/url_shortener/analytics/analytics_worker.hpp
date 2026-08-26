#pragma once

#include <atomic>
#include <thread>

#include "url_shortener/analytics/analytics_config.hpp"
#include "url_shortener/analytics/analytics_metrics.hpp"
#include "url_shortener/analytics/bounded_click_event_queue.hpp"
#include "url_shortener/analytics/i_click_event_repository.hpp"

namespace url_shortener::analytics {

enum class AnalyticsWorkerState { Stopped, Running, Stopping };

class AnalyticsWorker {
public:
    AnalyticsWorker(const AnalyticsConfig& config, BoundedClickEventQueue& queue, IClickEventRepository& repo, IAnalyticsMetrics& metrics);
    ~AnalyticsWorker();
    void Start();
    void Stop();
    AnalyticsWorkerState State() const noexcept;
    void FlushOnce();
private:
    void Run();
    bool PersistWithRetry(const std::vector<ClickEvent>& batch);
    const AnalyticsConfig& config_; BoundedClickEventQueue& queue_; IClickEventRepository& repo_; IAnalyticsMetrics& metrics_;
    std::atomic<AnalyticsWorkerState> state_{AnalyticsWorkerState::Stopped}; std::thread thread_;
};

}
