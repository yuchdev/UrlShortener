#define BOOST_TEST_MODULE TuiPollTickerTest

#include <atomic>
#include <chrono>
#include <thread>

#include <boost/test/unit_test.hpp>
#include <url_shortener/tui/poll_ticker.hpp>

#include "inline_poster.hpp"

/**
 * @file 16_poll_ticker_test.cpp
 * @brief PollTicker tests (D8 auto-refresh, 06.0/16): interval ticking, stop,
 *        destruction-while-running, double start, and UI-thread delivery.
 *
 * Type: unit (links url_shortener_tui_model; no FTXUI). Uses InlinePoster so a
 *        tick is only executed when the test thread drains it.
 * Scenario: a 20 ms ticker posts >= 3 ticks in ~100 ms; stop() halts further
 *        posts and returns promptly; destroying a running ticker posts nothing
 *        after teardown (T-120); a second start() creates no second thread; the
 *        callback runs on the draining thread, never the ticker thread.
 * On failure first check: PollTicker::run()/stop() in poll_ticker.cpp and that
 *        the thread posts outside its lock.
 */

namespace tui = url_shortener::tui;
using tui::testing::InlinePoster;

// ---------------------------------------------------------------------------
// A 20 ms ticker posts at least three ticks within ~100 ms.
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(ticks_on_interval)
{
    InlinePoster poster;
    std::atomic<int> posted {0};
    tui::PollTicker ticker(poster, [&posted] { ++posted; });
    ticker.set_interval(std::chrono::milliseconds(20));
    ticker.start();

    // Race-free: wait until the poster has recorded at least three posts.
    poster.wait_for_posted(3);
    ticker.stop();

    // The ticks were only posted, never executed (InlinePoster is lazy).
    BOOST_TEST(posted.load() == 0);
    const std::size_t ran = poster.run_pending();
    BOOST_TEST(ran >= 3u);
    BOOST_TEST(posted.load() >= 3);
}

// ---------------------------------------------------------------------------
// stop() prevents further posts and returns promptly.
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(stop_halts_posting)
{
    InlinePoster poster;
    tui::PollTicker ticker(poster, [] {});
    ticker.set_interval(std::chrono::milliseconds(10));
    ticker.start();
    poster.wait_for_posted(1);

    ticker.stop();
    BOOST_TEST(!ticker.running());
    poster.run_pending();  // drain whatever was posted before stop
    const std::size_t after_stop = poster.pending();

    // No new ticks are posted after stop, even after a generous wait.
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    BOOST_TEST(poster.pending() == after_stop);

    ticker.stop();  // idempotent
}

// ---------------------------------------------------------------------------
// Destroying a running ticker posts nothing more (T-120: no UAF after
// teardown).
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(destruction_while_running_is_safe)
{
    InlinePoster poster;
    std::atomic<int> posted {0};
    {
        tui::PollTicker ticker(poster, [&posted] { ++posted; });
        ticker.set_interval(std::chrono::milliseconds(10));
        ticker.start();
        poster.wait_for_posted(1);
    }  // ticker destroyed: thread joined before this returns

    // Any posts captured only a counter that still outlives the ticker; running
    // them is safe and no further posts arrive after destruction.
    poster.run_pending();
    const int seen = posted.load();
    std::this_thread::sleep_for(std::chrono::milliseconds(30));
    BOOST_TEST(poster.run_pending() == 0u);
    BOOST_TEST(posted.load() == seen);
}

// ---------------------------------------------------------------------------
// A second start() while running is a no-op (no second thread).
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(double_start_is_noop)
{
    InlinePoster poster;
    tui::PollTicker ticker(poster, [] {});
    ticker.set_interval(std::chrono::milliseconds(15));
    ticker.start();
    ticker.start();  // must not spawn a second thread
    BOOST_TEST(ticker.running());
    poster.wait_for_posted(1);
    ticker.stop();
    BOOST_TEST(!ticker.running());
}

// ---------------------------------------------------------------------------
// The callback runs only on the draining (UI) thread, never the ticker thread.
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(callback_runs_on_ui_thread_only)
{
    InlinePoster poster;
    const std::thread::id test_thread = std::this_thread::get_id();
    std::atomic<bool> wrong_thread {false};
    std::atomic<int> ran {0};
    tui::PollTicker ticker(poster,
                           [&]
                           {
                               if (std::this_thread::get_id() != test_thread) {
                                   wrong_thread = true;
                               }
                               ++ran;
                           });
    ticker.set_interval(std::chrono::milliseconds(10));
    ticker.start();
    poster.wait_for_posted(2);
    ticker.stop();

    poster.run_pending();  // executes the ticks on THIS (test/UI) thread
    BOOST_TEST(ran.load() >= 2);
    BOOST_TEST(!wrong_thread.load());
}
