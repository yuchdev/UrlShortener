#include "url_shortener/analytics/BoundedClickEventQueue.hpp"

#include <stdexcept>

namespace url_shortener::analytics {

BoundedClickEventQueue::BoundedClickEventQueue(const std::size_t capacity) : capacity_(capacity) {
    if (capacity_ == 0) throw std::invalid_argument("capacity must be > 0");
}
EnqueueResult BoundedClickEventQueue::TryEnqueue(ClickEvent event) noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    if (queue_.size() >= capacity_) { ++dropped_total_; return EnqueueResult::DroppedFull; }
    queue_.push_back(std::move(event)); ++enqueued_total_; cv_.notify_one(); return EnqueueResult::Enqueued;
}
bool BoundedClickEventQueue::TryDequeue(ClickEvent& out) noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    if (queue_.empty()) return false;
    out = std::move(queue_.front()); queue_.pop_front(); return true;
}
bool BoundedClickEventQueue::WaitDequeue(ClickEvent& out, std::chrono::milliseconds timeout) noexcept {
    std::unique_lock<std::mutex> lock(mutex_);
    if (!cv_.wait_for(lock, timeout, [this]{ return !queue_.empty(); })) return false;
    out = std::move(queue_.front()); queue_.pop_front(); return true;
}
QueueStats BoundedClickEventQueue::GetStats() const noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    return QueueStats{queue_.size(), enqueued_total_, dropped_total_};
}

}
