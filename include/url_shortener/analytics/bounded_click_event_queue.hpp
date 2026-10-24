#pragma once

#include <condition_variable>
#include <deque>
#include <mutex>

#include "url_shortener/analytics/i_click_event_queue.hpp"

namespace url_shortener::analytics
{

/**
 * @brief `IClickEventQueue` implementation backed by a mutex-protected
 * `std::deque`, with drop-on-overflow semantics: once at capacity,
 * `TryEnqueue` drops the new event rather than blocking the producer or
 * evicting an existing one.
 *
 * Thread-safety: every public method is safe to call concurrently from any
 * thread (the redirect hot path calls `TryEnqueue`; `AnalyticsWorker` calls
 * `TryDequeue`/`WaitDequeue`/`GetStats`/`NotifyStop` from its own thread).
 * All access to the underlying deque and counters is guarded by `mutex_`.
 */
class BoundedClickEventQueue final : public IClickEventQueue
{
public:
    /**
     * @brief Constructs an empty queue with a fixed capacity.
     * @param capacity Maximum number of buffered events; capacity is not
     * adjustable after construction.
     * @throws std::invalid_argument if `capacity == 0`.
     */
    explicit BoundedClickEventQueue(std::size_t capacity);

    /**
     * @brief Attempts to enqueue `event` without blocking.
     * @param event Event to move into the queue; left in a moved-from
     * state on success. On drop, `event` is simply discarded.
     * @return `EnqueueResult::Enqueued` if there was room and the event
     * was buffered; `EnqueueResult::DroppedFull` if the queue was already
     * at capacity, or if moving `event` into the underlying deque threw
     * (e.g. `std::bad_alloc`) - the exception is swallowed, not
     * propagated, to honor `noexcept`.
     */
    EnqueueResult TryEnqueue(ClickEvent event) noexcept override;

    /**
     * @brief Attempts to dequeue one event without blocking.
     * @param out Receives the dequeued event on success; left unmodified
     * on failure.
     * @return `true` if an event was dequeued; `false` if the queue was
     * empty.
     */
    bool TryDequeue(ClickEvent& out) noexcept override;

    /**
     * @brief Returns a point-in-time snapshot of queue depth and lifetime
     * enqueue/drop counters.
     * @return Current `QueueStats`; `depth` reflects the size at the
     * moment of the call and may be stale immediately after it returns.
     */
    QueueStats GetStats() const noexcept override;

    /**
     * @brief Dequeues one event, blocking the calling thread until one is
     * available, `timeout` elapses, or `NotifyStop()` is called.
     * @param out Receives the dequeued event on success; left unmodified
     * on failure.
     * @param timeout Maximum time to wait for an event to become
     * available.
     * @return `true` if an event was dequeued before the timeout; `false`
     * if the wait timed out, or if `NotifyStop()` woke this call while the
     * queue was still empty.
     */
    bool WaitDequeue(ClickEvent& out,
                     std::chrono::milliseconds timeout) noexcept;

    /**
     * @brief Wakes every thread currently blocked in `WaitDequeue` (even
     * with an empty queue) so that a stopping worker exits promptly
     * instead of waiting out its full timeout.
     */
    void NotifyStop() noexcept;

private:
    const std::size_t capacity_;

    mutable std::mutex mutex_;
    
    std::condition_variable cv_;
    
    std::deque<ClickEvent> queue_;
    
    std::uint64_t enqueued_total_ = 0;
    
    std::uint64_t dropped_total_ = 0;
    
    bool stop_ = false;
};

}  // namespace url_shortener::analytics
