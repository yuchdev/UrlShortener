#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <thread>

#include <url_shortener/tui/async_caller.hpp>

namespace url_shortener::tui
{

/**
 * @file poll_ticker.hpp
 * @brief The off-FTXUI timer behind the Jobs screen's auto-refresh (D8,
 * subtask 06.0/16): a single background thread that posts a UI-thread tick once
 * per interval through an @ref IUiPoster (FTXUI has no timer).
 *
 * This header belongs to `url_shortener_tui_model` (ADR 0003 rule 6): it holds
 * no FTXUI and no colour. The ticker thread never calls @ref ICommandClient and
 * never touches the screen; it only hands a caller-supplied callback to
 * @ref IUiPoster::post, which runs it on the UI thread (ADR 0003 rule 3). The
 * screen's tick handler is then the only place that submits commands (through
 * an
 * @ref AsyncCaller), so the "all calls go off the UI thread via AsyncCaller,
 * the ticker only posts" boundary is structural.
 */

/**
 * @brief A cancellable, interval-driven source of UI-thread ticks.
 *
 * A started ticker runs one background thread that waits for the current
 * interval (or an earlier stop) and, each time the interval elapses, posts the
 * callback through the injected @ref IUiPoster so it runs on the UI thread. The
 * callback is never invoked on the ticker's own thread, so a handler may freely
 * touch UI state.
 *
 * Lifetime / teardown (threat model T-120): @ref stop and the destructor set a
 * stop flag, wake the thread and join it, so no tick is posted after the ticker
 * is destroyed and no handler runs against a torn-down owner. The injected
 * @ref IUiPoster must outlive the ticker; a screen owns its ticker as its
 * last-declared member so it is destroyed (and its thread joined) before the
 * caller/poster it borrows (subtask 16). @ref stop is idempotent and @ref start
 * is a no-op while already running.
 *
 * Thread-safety: @ref set_interval, @ref start and @ref stop are called from
 * the UI thread; they synchronize with the ticker thread through an internal
 * mutex and condition variable. The object is not copyable or movable.
 */
class PollTicker
{
public:
    /**
     * @brief Builds a stopped ticker bound to a poster and a tick callback.
     *
     * No thread is started until @ref start is called.
     *
     * @param poster The UI-thread poster each tick is delivered through; a
     * reference captured for the ticker's lifetime, so it must outlive this
     * object (see the class contract). Not owned.
     * @param on_tick The callback posted once per interval; copied and run on
     * the UI thread (never on the ticker thread). May be empty, in which case
     * each tick posts a harmless no-op.
     */
    PollTicker(IUiPoster& poster, std::function<void()> on_tick);

    /**
     * @brief Stops and joins the ticker thread; never throws.
     */
    ~PollTicker();

    PollTicker(const PollTicker&) = delete;
    PollTicker& operator=(const PollTicker&) = delete;
    PollTicker(PollTicker&&) = delete;
    PollTicker& operator=(PollTicker&&) = delete;

    /**
     * @brief Sets the wait between ticks, taking effect on the next wait.
     *
     * Safe to call while the ticker is running or stopped; a running thread
     * waiting on the old interval is woken so it re-waits for the new one. A
     * non-positive interval is clamped to a 1 ms minimum so the thread can
     * never busy-spin.
     *
     * @param interval The new inter-tick delay.
     */
    void set_interval(std::chrono::milliseconds interval);

    /**
     * @brief Starts the ticker thread if it is not already running.
     *
     * The first tick is posted after one interval has elapsed. Calling @ref
     * start while already running is a no-op (no second thread is created).
     */
    void start();

    /**
     * @brief Signals the ticker thread to stop and joins it.
     *
     * After @ref stop returns, no further ticks are posted. Idempotent: calling
     * it when already stopped does nothing and returns promptly.
     */
    void stop();

    /**
     * @brief Whether the ticker thread is currently running.
     *
     * @return `true` between a @ref start and the matching @ref stop /
     * destruction; `false` otherwise. Diagnostic/testing accessor.
     */
    bool running() const;

private:
    /**
     * @brief The ticker thread body: wait one interval, post a tick, repeat
     * until stopped.
     */
    void run();

    ///< UI-thread poster each tick is delivered through; not owned.
    IUiPoster& m_poster_;

    ///< Callback posted once per interval; run on the UI thread.
    std::function<void()> m_on_tick_;

    ///< Guards @ref m_interval_, @ref m_stop_ and @ref m_running_.
    mutable std::mutex m_mutex_;

    ///< Wakes the ticker thread on a stop request or an interval change.
    std::condition_variable m_cv_;

    ///< The background thread; joined by @ref stop and the destructor.
    std::thread m_thread_;

    ///< Current inter-tick wait; at least 1 ms.
    std::chrono::milliseconds m_interval_ {std::chrono::milliseconds(1000)};

    ///< `true` once a stop has been requested; the thread exits its loop.
    bool m_stop_ = false;

    ///< `true` while @ref m_thread_ is joinable and running its loop.
    bool m_running_ = false;
};

}  // namespace url_shortener::tui
