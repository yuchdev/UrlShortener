#include <url_shortener/analytics/click_event_queue.h>
#include <url_shortener/url_shortener.h>

namespace url_shortener {

BoundedClickEventQueue::BoundedClickEventQueue(size_t capacity)
    : capacity_(capacity)
{
}

void BoundedClickEventQueue::resetCapacity(size_t capacity)
{
    std::unique_lock lock(mutex_);
    capacity_ = capacity;
    while (events_.size() > capacity_) {
        events_.pop_front();
        ++dropped_total_;
    }
}

bool BoundedClickEventQueue::tryEnqueue(ClickEvent event)
{
    std::unique_lock lock(mutex_);
    if (events_.size() >= capacity_) {
        ++dropped_total_;
        return false;
    }
    events_.push_back(std::move(event));
    ++enqueued_total_;
    return true;
}

BoundedClickEventQueue& analyticsQueue(const ServerConfig& config)
{
    static BoundedClickEventQueue queue(config.analytics_queue_capacity);
    static size_t last_capacity = config.analytics_queue_capacity;
    if (last_capacity != config.analytics_queue_capacity) {
        queue.resetCapacity(config.analytics_queue_capacity);
        last_capacity = config.analytics_queue_capacity;
    }
    return queue;
}

} // namespace url_shortener
