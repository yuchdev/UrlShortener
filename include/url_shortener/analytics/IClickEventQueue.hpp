#pragma once

#include <cstddef>
#include <cstdint>
#include <ostream>

#include "url_shortener/analytics/ClickEvent.hpp"

namespace url_shortener::analytics {

enum class EnqueueResult { Enqueued, DroppedFull };

inline std::ostream& operator<<(std::ostream& os, const EnqueueResult value)
{
    switch (value) {
    case EnqueueResult::Enqueued:
        return os << "EnqueueResult::Enqueued";
    case EnqueueResult::DroppedFull:
        return os << "EnqueueResult::DroppedFull";
    }
    return os << "EnqueueResult(" << static_cast<int>(value) << ")";
}

struct QueueStats {
    std::size_t depth = 0;
    std::uint64_t enqueued_total = 0;
    std::uint64_t dropped_total = 0;
};

/** @brief Non-blocking queue interface for click events. */
class IClickEventQueue {
public:
    virtual ~IClickEventQueue() = default;
    virtual EnqueueResult TryEnqueue(ClickEvent event) noexcept = 0;
    virtual bool TryDequeue(ClickEvent& out) noexcept = 0;
    virtual QueueStats GetStats() const noexcept = 0;
};

} // namespace url_shortener::analytics
