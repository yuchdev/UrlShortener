/**
 * @file poll_ticker.cpp
 * @brief Implementation of @ref url_shortener::tui::PollTicker: one background
 * thread that posts a UI-thread tick once per interval (D8 auto-refresh,
 * subtask 06.0/16). The thread never calls the command client; it only posts
 * the callback through the injected @ref url_shortener::tui::IUiPoster.
 */
#include <utility>

#include "url_shortener/tui/poll_ticker.hpp"

namespace url_shortener::tui
{

namespace
{

/// Floor for the inter-tick wait, so a zero/negative interval can never make
/// the ticker thread busy-spin.
constexpr std::chrono::milliseconds k_min_interval {1};

}  // namespace

PollTicker::PollTicker(IUiPoster& poster, std::function<void()> on_tick)
    : m_poster_(poster)
    , m_on_tick_(std::move(on_tick))
{
}

PollTicker::~PollTicker()
{
    stop();
}

void PollTicker::set_interval(std::chrono::milliseconds interval)
{
    {
        std::lock_guard<std::mutex> lock(m_mutex_);
        m_interval_ = interval < k_min_interval ? k_min_interval : interval;
        m_interval_changed_ = true;
    }
    // Wake the thread so a wait on the previous interval re-arms immediately
    // (the changed flag is part of the wait predicate, so the notify is honored
    // rather than swallowed when the thread re-evaluates and keeps waiting).
    m_cv_.notify_all();
}

void PollTicker::start()
{
    {
        std::lock_guard<std::mutex> lock(m_mutex_);
        if (m_running_) {
            return;  // double start is a no-op
        }
        m_stop_ = false;
        m_running_ = true;
    }
    m_thread_ = std::thread([this] { run(); });
}

void PollTicker::stop()
{
    {
        std::lock_guard<std::mutex> lock(m_mutex_);
        if (!m_running_ && !m_thread_.joinable()) {
            return;  // already stopped (idempotent)
        }
        m_stop_ = true;
    }
    m_cv_.notify_all();
    if (m_thread_.joinable()) {
        m_thread_.join();
    }
    std::lock_guard<std::mutex> lock(m_mutex_);
    m_running_ = false;
}

bool PollTicker::running() const
{
    std::lock_guard<std::mutex> lock(m_mutex_);
    return m_running_;
}

void PollTicker::run()
{
    for (;;) {
        {
            std::unique_lock<std::mutex> lock(m_mutex_);
            m_interval_changed_ = false;
            const std::chrono::milliseconds interval = m_interval_;
            // Wait the current interval, waking early on stop or an interval
            // change. wait_for returns true when the predicate holds (a stop or
            // an interval change) and false on timeout (a due tick).
            if (m_cv_.wait_for(lock,
                               interval,
                               [this]
                               { return m_stop_ || m_interval_changed_; }))
            {
                if (m_stop_) {
                    return;  // stop requested; no tick is posted
                }
                // The interval changed: restart the wait with the new interval
                // so a shortened interval takes effect at once and a lengthened
                // one cannot fire a tick early.
                continue;
            }
        }
        // Post the tick OUTSIDE the lock so the UI thread's handler (which may
        // call back into set_interval/stop) never deadlocks against the ticker.
        m_poster_.post(m_on_tick_);
    }
}

}  // namespace url_shortener::tui
