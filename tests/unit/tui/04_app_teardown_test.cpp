#define BOOST_TEST_MODULE TuiAppTeardownTest

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>
#include <utility>
#include <vector>

#include <boost/test/unit_test.hpp>

#include <url_shortener/commands/envelope.hpp>
#include <url_shortener/tui/async_caller.hpp>
#include <url_shortener/tui/command_client.hpp>

using url_shortener::commands::CommandRequest;
using url_shortener::commands::CommandResponse;
using url_shortener::commands::make_success;
using url_shortener::tui::AsyncCaller;
using url_shortener::tui::CommandInfo;
using url_shortener::tui::ICommandClient;
using url_shortener::tui::IUiPoster;
using url_shortener::tui::SessionInfo;

namespace
{

// A poster that stands in for FtxuiPoster + the ScreenInteractive it
// dereferences: once "dead" (screen destroyed), any further post() is a
// use-after-free, which this spy records instead.
class LifecyclePoster final : public IUiPoster
{
public:
    void post(std::function<void()> task) override
    {
        if (m_dead_.load(std::memory_order_acquire))
        {
            m_posted_after_death_.store(true, std::memory_order_release);
        }
        {
            std::lock_guard<std::mutex> lock(m_mutex_);
            ++m_post_count_;
        }
        // Drop the task: the simulated screen never drains it. This mirrors a
        // quit where completions arrive after the loop stopped.
        (void)task;
    }

    void mark_dead() { m_dead_.store(true, std::memory_order_release); }

    bool posted_after_death() const
    {
        return m_posted_after_death_.load(std::memory_order_acquire);
    }

    std::size_t post_count() const
    {
        std::lock_guard<std::mutex> lock(m_mutex_);
        return m_post_count_;
    }

private:
    std::atomic<bool> m_dead_ {false};
    std::atomic<bool> m_posted_after_death_ {false};
    mutable std::mutex m_mutex_;
    std::size_t m_post_count_ = 0;
};

// A client whose call() blocks inside the worker until the test releases it, so
// a call can be held genuinely in flight across the caller's destruction.
class BlockingClient final : public ICommandClient
{
public:
    CommandResponse call(const CommandRequest& request) override
    {
        m_entered_.store(true, std::memory_order_release);
        std::unique_lock<std::mutex> lock(m_mutex_);
        m_cv_.wait(lock, [this] { return m_released_; });
        return make_success(request.id, "{}");
    }

    std::vector<CommandInfo> commands() const override { return {}; }
    SessionInfo session() const override { return {}; }

    bool entered() const { return m_entered_.load(std::memory_order_acquire); }

    void release()
    {
        {
            std::lock_guard<std::mutex> lock(m_mutex_);
            m_released_ = true;
        }
        m_cv_.notify_all();
    }

private:
    std::atomic<bool> m_entered_ {false};
    std::mutex m_mutex_;
    std::condition_variable m_cv_;
    bool m_released_ = false;
};

}  // namespace

// ---------------------------------------------------------------------------
// App teardown ordering (03 security review, T-120): a call in flight when the
// app shuts down must neither hang the destructor nor post to a destroyed
// screen. The AsyncCaller (declared after the poster in App::run) is destroyed
// first and joins its workers, so the poster is only ever used while alive.
// On failure first check: App::run reordering the poster/caller locals, or
// AsyncCaller not joining workers in its destructor.
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(in_flight_call_at_shutdown_neither_hangs_nor_posts_to_dead)
{
    LifecyclePoster poster;
    BlockingClient client;

    auto caller = std::make_unique<AsyncCaller>(client, poster, 1);

    CommandRequest request;
    request.command = "system.info";
    request.id = "tui-1";
    caller->submit(request, [](const CommandResponse&) {});

    // Wait until the worker is genuinely inside call() (the call is in flight).
    while (!client.entered())
    {
        std::this_thread::yield();
    }

    // Release the blocked call shortly after we start tearing the caller down,
    // so the join has a real in-flight call to wait on.
    std::thread releaser([&] {
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        client.release();
    });

    // Destroying the caller joins the worker; it must return (no hang).
    caller.reset();
    releaser.join();

    // The caller is gone and its workers joined; only now is the screen torn
    // down. Any post from here on would be a use-after-free.
    poster.mark_dead();

    BOOST_TEST(!poster.posted_after_death());
    BOOST_TEST(poster.post_count() >= 1u);
}
