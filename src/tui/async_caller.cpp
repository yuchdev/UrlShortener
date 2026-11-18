#include "url_shortener/tui/async_caller.hpp"

#include <exception>
#include <string>
#include <utility>

#include <url_shortener/commands/error_codes.hpp>

namespace url_shortener::tui
{

AsyncCaller::AsyncCaller(ICommandClient& client,
                         IUiPoster& poster,
                         std::size_t workers)
    : m_client_(client)
    , m_poster_(poster)
    , m_alive_(std::make_shared<std::atomic<bool>>(true))
{
    const std::size_t count = workers == 0 ? 1 : workers;
    m_threads_.reserve(count);
    for (std::size_t i = 0; i < count; ++i)
    {
        m_threads_.emplace_back([this] { worker_loop(); });
    }
}

AsyncCaller::~AsyncCaller()
{
    // Mark dead first so any completion already posted to the UI thread but not
    // yet drained becomes a no-op (T-120). The poster is drained on the same
    // thread that runs this destructor, so no completion can observe a
    // torn-down object mid-flight.
    m_alive_->store(false);
    {
        std::lock_guard<std::mutex> lock(m_mutex_);
        m_stopping_ = true;
    }
    m_cv_.notify_all();
    for (std::thread& worker : m_threads_)
    {
        if (worker.joinable())
        {
            worker.join();
        }
    }
}

CallToken AsyncCaller::submit(commands::CommandRequest request,
                              ResponseHandler on_done)
{
    CallToken token = 0;
    bool overflow = false;
    {
        std::lock_guard<std::mutex> lock(m_mutex_);
        token = ++m_next_token_;
        // Register the token as live and count it as in flight before the work
        // can become visible to a worker, so a fast completion can never
        // fetch_sub before this fetch_add and underflow the counter. Both the
        // accepted and the overflow path post exactly one completion, so both
        // are counted identically and in_flight() still returns to 0.
        m_live_.insert(token);
        m_in_flight_.fetch_add(1, std::memory_order_acq_rel);
        if (m_stopping_ || m_queue_.size() >= k_queue_capacity)
        {
            overflow = true;
        }
        else
        {
            m_queue_.push_back(
                work_item {token, std::move(request), std::move(on_done)});
        }
    }

    if (overflow)
    {
        commands::CommandResponse busy = commands::make_failure(
            request.id,
            commands::make_error(commands::CommandErrorCode::busy,
                                 "the command queue is full"));
        post_completion(token, std::move(busy), std::move(on_done));
        return token;
    }

    m_cv_.notify_one();
    return token;
}

void AsyncCaller::cancel(CallToken token)
{
    std::lock_guard<std::mutex> lock(m_mutex_);
    // Only record a cancellation for a token that is still outstanding;
    // cancelling an unknown or already-delivered token is a true no-op and
    // never leaks an entry into the cancel set (T-121).
    if (m_live_.count(token) > 0)
    {
        m_cancelled_.insert(token);
    }
}

std::size_t AsyncCaller::in_flight() const
{
    return m_in_flight_.load(std::memory_order_acquire);
}

std::size_t AsyncCaller::tracked_cancellations() const
{
    std::lock_guard<std::mutex> lock(m_mutex_);
    return m_cancelled_.size();
}

void AsyncCaller::worker_loop()
{
    for (;;)
    {
        work_item item;
        {
            std::unique_lock<std::mutex> lock(m_mutex_);
            m_cv_.wait(lock,
                       [this] { return m_stopping_ || !m_queue_.empty(); });
            // On stop, drop any still-queued requests and exit promptly; their
            // handlers never run (teardown contract).
            if (m_stopping_)
            {
                return;
            }
            item = std::move(m_queue_.front());
            m_queue_.pop_front();
        }

        commands::CommandResponse response;
        try
        {
            response = m_client_.call(item.request);
        }
        catch (...)
        {
            // A throwing client becomes a well-formed internal error so the
            // handler always sees a valid envelope (implementation step 2).
            response = commands::make_failure(
                item.request.id,
                commands::make_error(commands::CommandErrorCode::internal,
                                     "the command transport threw"));
        }

        post_completion(
            item.token, std::move(response), std::move(item.on_done));
    }
}

void AsyncCaller::post_completion(CallToken token,
                                  commands::CommandResponse resp,
                                  ResponseHandler on_done)
{
    std::shared_ptr<std::atomic<bool>> alive = m_alive_;
    m_poster_.post(
        [this, alive, token, resp = std::move(resp),
         on_done = std::move(on_done)]() mutable
        {
            // Guard first: if the caller was destroyed before this task was
            // drained, touching any member would be a use-after-free (T-120).
            if (!alive->load())
            {
                return;
            }
            deliver(token, resp, on_done);
        });
}

void AsyncCaller::deliver(CallToken token,
                          const commands::CommandResponse& resp,
                          const ResponseHandler& on_done)
{
    bool cancelled = false;
    {
        std::lock_guard<std::mutex> lock(m_mutex_);
        // The token is being delivered now, so it is no longer live; drop it
        // from both sets so neither can leak (T-121).
        m_live_.erase(token);
        auto it = m_cancelled_.find(token);
        if (it != m_cancelled_.end())
        {
            m_cancelled_.erase(it);
            cancelled = true;
        }
    }

    m_in_flight_.fetch_sub(1, std::memory_order_acq_rel);

    if (!cancelled && on_done)
    {
        on_done(resp);
    }
}

std::string RequestIdGenerator::next()
{
    const std::uint64_t n =
        m_counter_.fetch_add(1, std::memory_order_relaxed) + 1;
    return "tui-" + std::to_string(n);
}

}  // namespace url_shortener::tui
