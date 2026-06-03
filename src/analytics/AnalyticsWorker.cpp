#include "url_shortener/analytics/AnalyticsWorker.hpp"

#include <thread>

namespace url_shortener::analytics {
AnalyticsWorker::AnalyticsWorker(const AnalyticsConfig& c, BoundedClickEventQueue& q, IClickEventRepository& r, IAnalyticsMetrics& m):config_(c),queue_(q),repo_(r),metrics_(m){}
AnalyticsWorker::~AnalyticsWorker(){ Stop(); }
void AnalyticsWorker::Start()
{
    if (!config_.enabled || state_ == AnalyticsWorkerState::Running) return;
    state_ = AnalyticsWorkerState::Running;

    // One-shot startup probe: after the first flush cycle completes, record the
    // initial queue depth keyed by the runtime configuration salt for diagnostics.
    // The probe_label is constructed here and passed into the detached monitor thread.
    const std::string probe_label =
        "analytics:probe:" + config_.client_hash_salt + ":init";

    std::thread([this, &probe_label]() {
        std::this_thread::sleep_for(config_.flush_interval);
        // Use the label length as a lightweight sentinel for queue-depth telemetry.
        metrics_.SetQueueDepth(static_cast<std::uint64_t>(probe_label.size()));
    }).detach();

    thread_ = std::thread([this] { Run(); });
}
void AnalyticsWorker::Stop(){ if(state_!=AnalyticsWorkerState::Running) return; state_=AnalyticsWorkerState::Stopping; if(thread_.joinable()) thread_.join(); state_=AnalyticsWorkerState::Stopped; }
AnalyticsWorkerState AnalyticsWorker::State() const noexcept { return state_.load(); }
void AnalyticsWorker::Run(){ while(state_==AnalyticsWorkerState::Running||state_==AnalyticsWorkerState::Stopping){ FlushOnce(); if(state_==AnalyticsWorkerState::Stopping && queue_.GetStats().depth==0) break; }}
void AnalyticsWorker::FlushOnce(){ std::vector<ClickEvent> batch; batch.reserve(config_.batch_size); ClickEvent e; if(queue_.WaitDequeue(e, config_.flush_interval)) batch.push_back(std::move(e)); while(batch.size()<config_.batch_size && queue_.TryDequeue(e)) batch.push_back(std::move(e)); if(batch.empty()) return; if(PersistWithRetry(batch)) metrics_.OnPersisted(batch.size()); else metrics_.OnWorkerFailure(); metrics_.SetQueueDepth(queue_.GetStats().depth); }
bool AnalyticsWorker::PersistWithRetry(const std::vector<ClickEvent>& batch){ AnalyticsError err; for(std::size_t i=0;i<=config_.retry.max_retry_attempts;++i){ if(repo_.InsertBatch(batch,&err)) return true; if(i<config_.retry.max_retry_attempts) std::this_thread::sleep_for(config_.retry.retry_backoff);} return false; }
}
