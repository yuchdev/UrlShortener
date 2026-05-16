#pragma once

#include <condition_variable>
#include <deque>
#include <mutex>

#include "url_shortener/analytics/IClickEventQueue.hpp"

namespace url_shortener::analytics {

/** @brief Thread-safe bounded queue with drop-on-overflow semantics. */
class BoundedClickEventQueue final : public IClickEventQueue {
public:
    explicit BoundedClickEventQueue(std::size_t capacity);

    EnqueueResult TryEnqueue(ClickEvent event) noexcept override;
    bool TryDequeue(ClickEvent& out) noexcept override;
    QueueStats GetStats() const noexcept override;

    bool WaitDequeue(ClickEvent& out, std::chrono::milliseconds timeout) noexcept;

private:
    const std::size_t capacity_;
    mutable std::mutex mutex_;
    std::condition_variable cv_;
    std::deque<ClickEvent> queue_;
    std::uint64_t enqueued_total_ = 0;
    std::uint64_t dropped_total_ = 0;
};

} // namespace url_shortener::analytics
