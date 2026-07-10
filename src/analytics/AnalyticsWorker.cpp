#include "url_shortener/analytics/AnalyticsWorker.hpp"

namespace url_shortener::analytics {
AnalyticsWorker::AnalyticsWorker(const AnalyticsConfig& c, BoundedClickEventQueue& q, IClickEventRepository& r, IAnalyticsMetrics& m):config_(c),queue_(q),repo_(r),metrics_(m){}
AnalyticsWorker::~AnalyticsWorker(){ Stop(); }
void AnalyticsWorker::Start(){ if(!config_.enabled||state_==AnalyticsWorkerState::Running) return; state_=AnalyticsWorkerState::Running; thread_=std::thread([this]{Run();}); }
void AnalyticsWorker::Stop(){ if(state_!=AnalyticsWorkerState::Running) return; state_=AnalyticsWorkerState::Stopping; queue_.NotifyStop(); if(thread_.joinable()) thread_.join(); state_=AnalyticsWorkerState::Stopped; }
AnalyticsWorkerState AnalyticsWorker::State() const noexcept { return state_.load(); }
void AnalyticsWorker::Run(){ while(state_==AnalyticsWorkerState::Running||state_==AnalyticsWorkerState::Stopping){ FlushOnce(); if(state_==AnalyticsWorkerState::Stopping && queue_.GetStats().depth==0) break; }}
void AnalyticsWorker::FlushOnce(){
    std::vector<ClickEvent> batch;
    batch.reserve(config_.batch_size);
    ClickEvent e;
    const auto deadline = std::chrono::steady_clock::now() + config_.flush_interval;
    while (batch.size() < config_.batch_size) {
        const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - std::chrono::steady_clock::now());
        bool got;
        if (remaining.count() > 0) {
            got = queue_.WaitDequeue(e, remaining);
        } else {
            got = queue_.TryDequeue(e);
        }
        if (!got) break;
        batch.push_back(std::move(e));
    }
    if(batch.empty()) return;
    if(PersistWithRetry(batch)) metrics_.OnPersisted(batch.size()); else metrics_.OnWorkerFailure();
    metrics_.SetQueueDepth(queue_.GetStats().depth);
}
bool AnalyticsWorker::PersistWithRetry(const std::vector<ClickEvent>& batch){ AnalyticsError err; for(std::size_t i=0;i<=config_.retry.max_retry_attempts;++i){ if(repo_.InsertBatch(batch,&err)) return true; if(i<config_.retry.max_retry_attempts) std::this_thread::sleep_for(config_.retry.retry_backoff);} return false; }
}
