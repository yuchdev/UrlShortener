#pragma once

#include <chrono>
#include <cstddef>
#include <thread>

#include <boost/test/unit_test.hpp>
#include <url_shortener/tui/async_caller.hpp>

#include "inline_poster.hpp"

namespace url_shortener::tui::testing
{

/**
 * @brief Drains a real @ref AsyncCaller to true quiescence instead of pumping
 * the poster a fixed number of times.
 *
 * A test that drives a screen over a real @ref AsyncCaller (with a live worker
 * thread) cannot know up front how many completions the worker will post: a
 * screen may fan out several reads or chain a refresh, and under CPU load the
 * worker may not have called `ICommandClient::call` yet when the test looks.
 * A fixed `for (...) poster.run_pending();` loop therefore races the worker -
 * it can assert before an in-flight request has been recorded, which both
 * flakes and *weakens* a read-only safety proof (a stray write could slip past
 * an `assert_only_reads` that ran too early).
 *
 * This helper instead loops until the caller is genuinely idle: it runs a
 * poster pass, then checks @ref AsyncCaller::in_flight. A submission is counted
 * in flight from @ref AsyncCaller::submit until its completion task actually
 * runs on the UI thread (inside `run_pending`), where the in-flight count is
 * decremented. Because the worker records its request *before* it posts the
 * completion, and the completion is only drained here, `in_flight() == 0` after
 * a drain pass means every request has been delivered to the client and every
 * completion handler has run - including any re-entrant submit a handler made,
 * which bumps the count back above zero and keeps the loop going. A pending
 * (posted but undrained) completion always keeps the count above zero, so the
 * extra @ref InlinePoster::pending check is belt-and-braces, never the sole
 * guard.
 *
 * The deadline is a hang guard, not a performance assertion: if the caller is
 * still busy after @p deadline the test fails with a clear message rather than
 * hanging a CI worker forever. The default is deliberately generous (10 s) so a
 * heavily loaded machine never trips it spuriously.
 *
 * @param caller The live caller to drain; its worker(s) and injected client
 * must be the ones @p poster delivers for. Not owned.
 * @param poster The same @ref InlinePoster injected into @p caller, drained on
 * the calling (test/UI) thread. Not owned.
 * @param deadline Maximum wall-clock time to wait for quiescence before failing
 * the test. Defaults to 10 seconds.
 */
inline void settle_until_idle(
    AsyncCaller& caller,
    InlinePoster& poster,
    std::chrono::steady_clock::duration deadline = std::chrono::seconds(10))
{
    const std::chrono::steady_clock::time_point limit =
        std::chrono::steady_clock::now() + deadline;
    for (;;) {
        poster.run_pending();
        if (caller.in_flight() == 0 && poster.pending() == 0) {
            return;
        }
        if (std::chrono::steady_clock::now() >= limit) {
            BOOST_FAIL(
                "settle_until_idle timed out waiting for the AsyncCaller to go "
                "idle: in_flight="
                << caller.in_flight() << " pending=" << poster.pending());
        }
        std::this_thread::yield();
    }
}

}  // namespace url_shortener::tui::testing
