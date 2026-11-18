#define BOOST_TEST_MODULE TuiAsyncCallerTest

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <future>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <unordered_set>
#include <utility>
#include <vector>

#include <boost/test/unit_test.hpp>

#include <url_shortener/commands/envelope.hpp>
#include <url_shortener/commands/error_codes.hpp>
#include <url_shortener/tui/async_caller.hpp>
#include <url_shortener/tui/command_client.hpp>

#include "inline_poster.hpp"

using url_shortener::commands::CommandError;
using url_shortener::commands::CommandErrorCode;
using url_shortener::commands::CommandRequest;
using url_shortener::commands::CommandResponse;
using url_shortener::commands::make_error;
using url_shortener::commands::make_failure;
using url_shortener::commands::make_success;
using url_shortener::tui::AsyncCaller;
using url_shortener::tui::CallToken;
using url_shortener::tui::CommandInfo;
using url_shortener::tui::ICommandClient;
using url_shortener::tui::RequestIdGenerator;
using url_shortener::tui::SessionInfo;
using url_shortener::tui::testing::InlinePoster;

namespace
{

// Builds a minimal valid request carrying the given command and id.
CommandRequest make_request(std::string command, std::string id)
{
    CommandRequest request;
    request.command = std::move(command);
    request.id = std::move(id);
    return request;
}

// A client whose `call` blocks until the test releases its command name (or
// releases everything). Lets a test hold a worker mid-call, fill the queue, or
// force a specific completion order - deterministically, with no sleeps.
class GatedClient final : public ICommandClient
{
public:
    CommandResponse call(const CommandRequest& request) override
    {
        {
            std::lock_guard<std::mutex> lock(m_mutex_);
            ++m_started_;
        }
        m_started_cv_.notify_all();
        {
            std::unique_lock<std::mutex> lock(m_mutex_);
            m_release_cv_.wait(
                lock,
                [this, &request]
                {
                    return m_release_all_
                        || m_released_.count(request.command) > 0;
                });
        }
        return make_success(request.id, "{}");
    }

    std::vector<CommandInfo> commands() const override { return {}; }

    SessionInfo session() const override { return {}; }

    // Blocks until at least @p n calls have entered `call`.
    void wait_started(std::size_t n)
    {
        std::unique_lock<std::mutex> lock(m_mutex_);
        m_started_cv_.wait(lock, [this, n] { return m_started_ >= n; });
    }

    // Unblocks every call whose command equals @p command.
    void release(std::string command)
    {
        {
            std::lock_guard<std::mutex> lock(m_mutex_);
            m_released_.insert(std::move(command));
        }
        m_release_cv_.notify_all();
    }

    // Unblocks every current and future call.
    void release_all()
    {
        {
            std::lock_guard<std::mutex> lock(m_mutex_);
            m_release_all_ = true;
        }
        m_release_cv_.notify_all();
    }

private:
    mutable std::mutex m_mutex_;
    std::condition_variable m_started_cv_;
    std::condition_variable m_release_cv_;
    std::size_t m_started_ = 0;
    std::set<std::string> m_released_;
    bool m_release_all_ = false;
};

// A client that records the thread its single `call` ran on, returns success.
class RecordingClient final : public ICommandClient
{
public:
    CommandResponse call(const CommandRequest& request) override
    {
        {
            std::lock_guard<std::mutex> lock(m_mutex_);
            m_call_thread_ = std::this_thread::get_id();
        }
        return make_success(request.id, "{}");
    }

    std::vector<CommandInfo> commands() const override { return {}; }

    SessionInfo session() const override { return {}; }

    std::thread::id call_thread() const
    {
        std::lock_guard<std::mutex> lock(m_mutex_);
        return m_call_thread_;
    }

private:
    mutable std::mutex m_mutex_;
    std::thread::id m_call_thread_;
};

// A client that always throws, to exercise the internal-error envelope path.
class ThrowingClient final : public ICommandClient
{
public:
    CommandResponse call(const CommandRequest&) override
    {
        throw std::runtime_error("boom");
    }

    std::vector<CommandInfo> commands() const override { return {}; }

    SessionInfo session() const override { return {}; }
};

// A trivial client that echoes success immediately; used for throughput.
class EchoClient final : public ICommandClient
{
public:
    CommandResponse call(const CommandRequest& request) override
    {
        return make_success(request.id, "{}");
    }

    std::vector<CommandInfo> commands() const override { return {}; }

    SessionInfo session() const override { return {}; }
};

}  // namespace

// ---------------------------------------------------------------------------
// A handler never runs on a worker thread and never before run_pending(): it
// runs on the test (UI) thread only when the poster is drained.
// On failure first check: AsyncCaller ran the handler inline instead of via
// IUiPoster::post, or InlinePoster ran a task eagerly.
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(handler_runs_on_ui_thread_only_after_run_pending)
{
    const std::thread::id test_thread = std::this_thread::get_id();
    RecordingClient client;
    InlinePoster poster;
    AsyncCaller caller(client, poster, 1);

    std::atomic<bool> handler_ran {false};
    std::thread::id handler_thread {};
    caller.submit(make_request("echo.run", "id-1"),
                  [&](const CommandResponse&)
                  {
                      handler_thread = std::this_thread::get_id();
                      handler_ran.store(true);
                  });

    // The worker has posted its completion, but nothing drains it yet.
    poster.wait_for_posted(1);
    BOOST_TEST(!handler_ran.load());

    const std::size_t ran = poster.run_pending();
    BOOST_TEST(ran == 1u);
    BOOST_TEST(handler_ran.load());
    BOOST_TEST((handler_thread == test_thread));
    BOOST_TEST((handler_thread != client.call_thread()));
    BOOST_TEST((client.call_thread() != test_thread));
    BOOST_TEST(caller.in_flight() == 0u);
}

// ---------------------------------------------------------------------------
// Two calls completing out of order each deliver their own response to their
// own token.
// On failure first check: a response was routed to the wrong handler/token.
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(responses_delivered_per_token_out_of_order)
{
    GatedClient client;
    InlinePoster poster;
    AsyncCaller caller(client, poster, 2);

    std::vector<std::pair<CallToken, std::string>> delivered;
    const CallToken token_a =
        caller.submit(make_request("cmd.a", "a"),
                      [&](const CommandResponse& resp)
                      { delivered.emplace_back(0, resp.id); });
    const CallToken token_b =
        caller.submit(make_request("cmd.b", "b"),
                      [&](const CommandResponse& resp)
                      { delivered.emplace_back(0, resp.id); });

    // Both workers are blocked inside call(); complete "b" first, then "a".
    client.wait_started(2);
    client.release("cmd.b");
    poster.wait_for_posted(1);
    poster.run_pending();
    client.release("cmd.a");
    poster.wait_for_posted(2);
    poster.run_pending();

    BOOST_TEST(delivered.size() == 2u);
    BOOST_TEST(delivered[0].second == "b");
    BOOST_TEST(delivered[1].second == "a");
    BOOST_TEST(token_a != token_b);
    BOOST_TEST(caller.in_flight() == 0u);
}

// ---------------------------------------------------------------------------
// cancel() suppresses the handler though the call still runs to completion.
// On failure first check: deliver() did not consult the cancel set, or cancel
// was applied after delivery.
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(cancel_suppresses_handler)
{
    GatedClient client;
    InlinePoster poster;
    AsyncCaller caller(client, poster, 1);

    std::atomic<bool> handler_ran {false};
    const CallToken token =
        caller.submit(make_request("cmd.x", "x"),
                      [&](const CommandResponse&) { handler_ran.store(true); });

    client.wait_started(1);
    caller.cancel(token);
    client.release("cmd.x");
    poster.wait_for_posted(1);
    poster.run_pending();

    BOOST_TEST(!handler_ran.load());
    BOOST_TEST(caller.in_flight() == 0u);
}

// ---------------------------------------------------------------------------
// T-120: destroying the caller with a completion queued but undrained neither
// runs the handler nor hangs; the guarded task is a no-op after teardown.
// On failure first check: the m_alive guard, join-on-destroy, or shared_ptr
// capture of the flag.
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(destructor_with_in_flight_call_does_not_run_or_hang)
{
    GatedClient client;
    InlinePoster poster;
    std::atomic<bool> handler_ran {false};

    auto caller = std::make_unique<AsyncCaller>(client, poster, 1);
    caller->submit(make_request("cmd.slow", "s"),
                   [&](const CommandResponse&) { handler_ran.store(true); });

    client.wait_started(1);
    client.release("cmd.slow");
    // The worker has finished the call and posted its completion; it sits in
    // the poster, undrained.
    poster.wait_for_posted(1);

    // Destroy within a time budget (success criterion 4: within 2 s).
    auto done = std::async(std::launch::async, [&] { caller.reset(); });
    const bool finished =
        done.wait_for(std::chrono::seconds(2)) == std::future_status::ready;
    BOOST_TEST(finished);
    done.get();

    // Draining after destruction runs the guarded task, which is a no-op.
    poster.run_pending();
    BOOST_TEST(!handler_ran.load());
}

// ---------------------------------------------------------------------------
// submit() beyond the queue capacity answers immediately with a busy envelope,
// without blocking the caller.
// On failure first check: the capacity branch in submit(), or the busy error
// code.
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(submit_over_capacity_yields_busy_envelope)
{
    GatedClient client;
    InlinePoster poster;
    AsyncCaller caller(client, poster, 1);

    // Hold the single worker inside call() so the queue cannot drain.
    caller.submit(make_request("hold", "hold"), [](const CommandResponse&) {});
    client.wait_started(1);

    // Fill the queue to capacity; all of these are accepted.
    for (std::size_t i = 0; i < AsyncCaller::k_queue_capacity; ++i)
    {
        caller.submit(make_request("queued", "q"),
                      [](const CommandResponse&) {});
    }

    // The next submit overflows and is answered with a busy envelope.
    std::atomic<bool> got_response {false};
    CommandErrorCode code = CommandErrorCode::none;
    bool ok = true;
    caller.submit(make_request("over", "over"),
                  [&](const CommandResponse& resp)
                  {
                      ok = resp.ok;
                      code = resp.error.code;
                      got_response.store(true);
                  });

    poster.wait_for_posted(1);
    poster.run_pending();
    BOOST_TEST(got_response.load());
    BOOST_TEST(!ok);
    BOOST_TEST((code == CommandErrorCode::busy));

    client.release_all();
}

// ---------------------------------------------------------------------------
// An exception thrown by the client becomes a well-formed internal error
// envelope.
// On failure first check: the try/catch around client.call() in worker_loop().
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(client_exception_becomes_internal_envelope)
{
    ThrowingClient client;
    InlinePoster poster;
    AsyncCaller caller(client, poster, 1);

    std::atomic<bool> ok {true};
    CommandErrorCode code = CommandErrorCode::none;
    std::string echoed_id;
    caller.submit(make_request("cmd.throw", "t-1"),
                  [&](const CommandResponse& resp)
                  {
                      ok = resp.ok;
                      code = resp.error.code;
                      echoed_id = resp.id;
                  });

    poster.wait_for_posted(1);
    poster.run_pending();
    BOOST_TEST(!ok.load());
    BOOST_TEST((code == CommandErrorCode::internal));
    BOOST_TEST(echoed_id == "t-1");
    BOOST_TEST(caller.in_flight() == 0u);
}

// ---------------------------------------------------------------------------
// submit() never blocks even when every worker is stuck and the queue is full.
// On failure first check: submit() waiting on queue space instead of posting a
// busy envelope.
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(submit_never_blocks_the_caller)
{
    GatedClient client;
    InlinePoster poster;
    AsyncCaller caller(client, poster, 2);

    const auto start = std::chrono::steady_clock::now();
    for (int i = 0; i < 1000; ++i)
    {
        caller.submit(make_request("blocked", "b"),
                      [](const CommandResponse&) {});
    }
    const auto elapsed = std::chrono::steady_clock::now() - start;

    // Generous bound: a blocking submit against a never-released client would
    // deadlock, not merely slow down.
    BOOST_TEST(
        (elapsed < std::chrono::seconds(5)));

    client.release_all();
}

// ---------------------------------------------------------------------------
// A stress run of 1,000 submits all deliver and in_flight returns to 0.
// On failure first check: the in-flight counter increment/decrement balance.
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(stress_many_submits_all_delivered)
{
    EchoClient client;
    InlinePoster poster;
    AsyncCaller caller(client, poster, 4);

    std::atomic<int> delivered {0};
    constexpr int k_count = 1000;
    for (int i = 0; i < k_count; ++i)
    {
        caller.submit(make_request("echo.run", std::to_string(i)),
                      [&](const CommandResponse&)
                      { delivered.fetch_add(1); });
    }

    poster.wait_for_posted(static_cast<std::size_t>(k_count));
    while (caller.in_flight() > 0)
    {
        poster.run_pending();
    }
    poster.run_pending();

    BOOST_TEST(delivered.load() == k_count);
    BOOST_TEST(caller.in_flight() == 0u);
}

// ---------------------------------------------------------------------------
// RequestIdGenerator yields unique ids across threads, each within 64 bytes.
// On failure first check: the atomic counter in RequestIdGenerator::next().
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(request_id_generator_unique_and_bounded)
{
    RequestIdGenerator generator;
    constexpr int k_threads = 4;
    constexpr int k_per_thread = 1000;

    std::vector<std::vector<std::string>> per_thread(k_threads);
    std::vector<std::thread> threads;
    for (int t = 0; t < k_threads; ++t)
    {
        threads.emplace_back(
            [&, t]
            {
                per_thread[t].reserve(k_per_thread);
                for (int i = 0; i < k_per_thread; ++i)
                {
                    per_thread[t].push_back(generator.next());
                }
            });
    }
    for (std::thread& thread : threads)
    {
        thread.join();
    }

    std::unordered_set<std::string> all;
    for (const std::vector<std::string>& ids : per_thread)
    {
        for (const std::string& id : ids)
        {
            BOOST_TEST(id.size() <= 64u);
            BOOST_TEST(id.compare(0, 4, "tui-") == 0);
            all.insert(id);
        }
    }
    BOOST_TEST(all.size()
               == static_cast<std::size_t>(k_threads * k_per_thread));
}
