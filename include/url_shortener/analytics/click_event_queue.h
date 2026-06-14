#pragma once

#include <url_shortener/core/types.h>
#include <deque>
#include <mutex>
#include <cstdint>

struct ServerConfig;

namespace url_shortener {

using ::ServerConfig;

class BoundedClickEventQueue
{
public:
    explicit BoundedClickEventQueue(size_t capacity);

    void resetCapacity(size_t capacity);
    bool tryEnqueue(ClickEvent event);

private:
    std::mutex mutex_;  ///< Protects queue internals.
    size_t capacity_;  ///< Maximum queue capacity.
    std::deque<ClickEvent> events_;  ///< Buffered click events.
    uint64_t enqueued_total_ = 0;  ///< Total enqueued events.
    uint64_t dropped_total_ = 0;  ///< Total dropped events.
};

BoundedClickEventQueue& analyticsQueue(const ServerConfig& config);

} // namespace url_shortener
