#pragma once

#include <cstddef>
#include <cstdint>
#include <ostream>

#include "url_shortener/analytics/click_event.hpp"

namespace url_shortener::analytics
{

/**
 * @brief Outcome of a non-blocking enqueue attempt on an `IClickEventQueue`.
 */
enum class EnqueueResult
{
    ///< The event was buffered and will be drained later.
    Enqueued,

    ///< The event was discarded: the queue was at capacity
    ///< (or, for `BoundedClickEventQueue`, the move into the
    ///< underlying storage threw). The caller is not notified
    ///< which; both cases are reported identically.
    DroppedFull
};

/**
 * @brief Stream formatter for enqueue results.
 * @param os Stream to write to.
 * @param value Value to format.
 * @return `os`, with the result's symbolic name (e.g.
 * `"EnqueueResult::Enqueued"`) written to it.
 */
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

/** @brief Point-in-time snapshot of an `IClickEventQueue`'s state. */
struct QueueStats
{
    ///< Number of events currently buffered, not yet
    ///< dequeued. Point-in-time; may be stale
    ///< immediately after the call that produced it.
    std::size_t depth = 0;

    ///< Lifetime count of events successfully enqueued since the queue
    ///< was constructed; monotonically increasing.
    std::uint64_t enqueued_total = 0;

    ///< Lifetime count of events dropped (`EnqueueResult::DroppedFull`)
    ///< since the queue was constructed; monotonically increasing.
    std::uint64_t dropped_total = 0;
};

/**
 * @brief Non-blocking queue interface for click events, decoupling the
 * redirect hot path (producer) from the background persistence worker
 * (consumer).
 *
 * Thread-safety: implementations must make every method safe to call
 * concurrently from any thread, since the hot path and the worker call
 * this interface from different threads with no external synchronization.
 */
class IClickEventQueue
{
public:
    /**
     * @brief Virtual destructor; implementations may be destroyed through
     * a base `IClickEventQueue*`.
     */
    virtual ~IClickEventQueue() = default;

    /**
     * @brief Attempts to enqueue `event` without blocking the caller.
     * @param event Event to move into the queue; left in a moved-from
     * state if the result is `EnqueueResult::Enqueued`.
     * @return `EnqueueResult::Enqueued` if the event was buffered;
     * `EnqueueResult::DroppedFull` if it was discarded (queue at capacity,
     * or an implementation-specific failure during enqueue).
     */
    virtual EnqueueResult TryEnqueue(ClickEvent event) noexcept = 0;

    /**
     * @brief Attempts to dequeue one event without blocking the caller.
     * @param out Receives the dequeued event on success; left unmodified
     * on failure.
     * @return `true` if an event was dequeued; `false` if the queue was
     * empty.
     */
    virtual bool TryDequeue(ClickEvent& out) noexcept = 0;

    /**
     * @brief Returns a snapshot of current depth and lifetime counters.
     * @return See `QueueStats`.
     */
    virtual QueueStats GetStats() const noexcept = 0;
};

}  // namespace url_shortener::analytics
