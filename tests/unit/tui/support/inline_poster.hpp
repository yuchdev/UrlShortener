#pragma once

#include <condition_variable>
#include <cstddef>
#include <functional>
#include <mutex>
#include <utility>
#include <vector>

#include <url_shortener/tui/async_caller.hpp>

namespace url_shortener::tui::testing
{

/**
 * @brief An @ref IUiPoster that queues tasks for the test thread to run,
 * standing in for the FTXUI UI thread in headless async tests (subtask 03).
 *
 * `AsyncCaller` workers call @ref post from background threads; the test thread
 * drains them deterministically with @ref run_pending, so a completion handler
 * never runs until the test asks for it. This is how subtask 03 proves
 * "no handler executes before `run_pending()`".
 *
 * Thread-safety: @ref post is safe to call concurrently from any worker thread;
 * @ref run_pending, @ref pending and @ref wait_for_posted are called from the
 * single test thread (the simulated UI thread).
 */
class InlinePoster final : public IUiPoster
{
public:
    /**
     * @brief Records @p task to be run later by @ref run_pending.
     *
     * Never runs the task inline, so a worker thread can never execute a
     * handler. Wakes any @ref wait_for_posted waiter.
     *
     * @param task The task to queue; moved in.
     */
    void post(std::function<void()> task) override
    {
        std::lock_guard<std::mutex> lock(m_mutex_);
        m_pending_.push_back(std::move(task));
        ++m_posted_total_;
        m_cv_.notify_all();
    }

    /**
     * @brief Runs every currently-queued task on the calling (test) thread.
     *
     * Tasks queued by a handler while it runs (re-entrant @ref post) are not
     * run in this pass; they wait for the next @ref run_pending, keeping each
     * drain deterministic.
     *
     * @return The number of tasks run in this pass.
     */
    std::size_t run_pending()
    {
        std::vector<std::function<void()>> batch;
        {
            std::lock_guard<std::mutex> lock(m_mutex_);
            batch.swap(m_pending_);
        }
        for (std::function<void()>& task : batch)
        {
            task();
        }
        return batch.size();
    }

    /**
     * @brief Returns how many tasks are currently queued and undrained.
     *
     * @return The size of the pending queue.
     */
    std::size_t pending() const
    {
        std::lock_guard<std::mutex> lock(m_mutex_);
        return m_pending_.size();
    }

    /**
     * @brief Blocks until at least @p count tasks have ever been posted.
     *
     * A race-free latch replacing sleeps: a test submits N calls then
     * `wait_for_posted(N)` to know every worker has posted its completion
     * before draining. Counts cumulative posts, so already-drained tasks still
     * count toward the total.
     *
     * @param count Cumulative number of posts to wait for.
     */
    void wait_for_posted(std::size_t count)
    {
        std::unique_lock<std::mutex> lock(m_mutex_);
        m_cv_.wait(lock, [this, count] { return m_posted_total_ >= count; });
    }

private:
    ///< Guards @ref m_pending_ and @ref m_posted_total_.
    mutable std::mutex m_mutex_;

    ///< Wakes @ref wait_for_posted when a task is posted.
    std::condition_variable m_cv_;

    ///< Tasks awaiting @ref run_pending.
    std::vector<std::function<void()>> m_pending_;

    ///< Cumulative count of tasks ever posted; never decremented.
    std::size_t m_posted_total_ = 0;
};

}  // namespace url_shortener::tui::testing
