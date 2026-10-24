#pragma once

#include <cstdint>
#include <deque>
#include <mutex>

#include <url_shortener/core/types.hpp>

/// Forward declaration; full definition in `url_shortener/core/config.hpp`.
struct ServerConfig;

namespace url_shortener
{

/// Brings the global `::ServerConfig` into this namespace for use below.
using ::ServerConfig;

/**
 * @brief Legacy, global-namespace bounded click-event queue predating the
 * `url_shortener::analytics::BoundedClickEventQueue` /
 * `url_shortener::analytics::IClickEventQueue` pipeline. This is a
 * distinct type with a distinct (lowercase-method) API - do not confuse
 * the two. It is still wired into the redirect hot path directly (see
 * `analyticsQueue`'s caller in `src/http/handlers/redirect_handlers.cpp`)
 * alongside the newer pipeline.
 *
 * Thread-safety: `resetCapacity` and `tryEnqueue` lock `mutex_` and are
 * safe to call concurrently with each other from any thread. Unlike
 * `url_shortener::analytics::BoundedClickEventQueue`, this type has no
 * blocking dequeue and no consumer-side API at all in this header - events
 * are only ever enqueued and counted, never drained through this type.
 */
class BoundedClickEventQueue
{
public:
    /**
     * @brief Constructs an empty queue with the given capacity.
     * @param capacity Maximum number of buffered events. Unlike the
     * `url_shortener::analytics` queue, a `capacity` of `0` is accepted
     * without throwing - it simply means every subsequent `tryEnqueue`
     * call drops its event, since `events_.size() >= capacity_` is
     * immediately true.
     */
    explicit BoundedClickEventQueue(size_t capacity);

    /**
     * @brief Changes the maximum capacity, evicting the oldest buffered
     * events (incrementing the drop counter for each) if the queue is
     * currently larger than the new capacity.
     * @param capacity New maximum number of buffered events.
     */
    void resetCapacity(size_t capacity);

    /**
     * @brief Attempts to enqueue `event` without blocking.
     * @param event Event to move into the queue on success.
     * @return `true` if there was room and the event was buffered;
     * `false` if the queue was already at capacity (the event is
     * discarded and the drop counter incremented).
     */
    bool tryEnqueue(ClickEvent event);

private:
    std::mutex mutex_;  ///< Protects queue internals.
    size_t capacity_;  ///< Maximum queue capacity.
    std::deque<ClickEvent> events_;  ///< Buffered click events.
    uint64_t enqueued_total_ = 0;  ///< Total enqueued events.
    uint64_t dropped_total_ = 0;  ///< Total dropped events.
};

/**
 * @brief Returns the process-wide singleton legacy queue, sized from
 * `config.analytics_queue_capacity`, lazily constructing it on first call.
 *
 * On every call, if `config.analytics_queue_capacity` differs from the
 * capacity observed on the previous call, the singleton is resized via
 * `resetCapacity` (which may evict and drop buffered events if shrinking).
 * `config` itself is otherwise not retained - only its capacity value is
 * read.
 *
 * @note Thread-safety caveat: the one-time construction of the static
 * local queue is thread-safe, but the "did the capacity change since last
 * call" comparison and the subsequent `resetCapacity` call are not
 * synchronized with each other beyond what `resetCapacity` itself locks -
 * calling this concurrently from multiple threads while the configured
 * capacity is actively changing is a data race on the function-local
 * `last_capacity` variable. In practice this function is expected to be
 * called with a stable `ServerConfig` after startup/reload, not under
 * concurrent capacity changes.
 * @param config Source of the desired queue capacity.
 * @return Reference to the singleton queue; valid for the lifetime of the
 * process.
 */
BoundedClickEventQueue& analyticsQueue(const ServerConfig& config);

}  // namespace url_shortener
