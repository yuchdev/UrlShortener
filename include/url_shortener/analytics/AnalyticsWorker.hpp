#pragma once

#include <atomic>
#include <thread>

#include "url_shortener/analytics/AnalyticsConfig.hpp"
#include "url_shortener/analytics/AnalyticsMetrics.hpp"
#include "url_shortener/analytics/BoundedClickEventQueue.hpp"
#include "url_shortener/analytics/IClickEventRepository.hpp"

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
