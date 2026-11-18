#pragma once

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_set>
#include <vector>

#include <url_shortener/commands/envelope.hpp>
#include <url_shortener/tui/command_client.hpp>

namespace url_shortener::tui
{

/**
 * @brief Delivers a completed-work callback onto the UI thread.
 *
 * The one abstraction that lets @ref AsyncCaller stay FTXUI-free (ADR 0003
 * rule 6, contract C8): the caller runs commands on background workers and
 * hands each result back through @ref post, which an implementation must run on
 * the single UI thread. The production implementation is `FtxuiPoster`
 * (`ftxui_poster.hpp`, the UI library) binding to
 * `ftxui::ScreenInteractive::Post`; tests use `InlinePoster`, which records the
 * tasks and runs them on demand.
 *
 * Thread-safety: @ref post must be callable concurrently from any worker
 * thread; an implementation is responsible for marshalling the task onto the UI
 * thread safely. The task itself always runs on the UI thread.
 */
class IUiPoster
{
public:
    /**
     * @brief Virtual destructor; posters are held through this base.
     */
    virtual ~IUiPoster() = default;

    /**
     * @brief Schedules @p task to run on the UI thread.
     *
     * Thread-safe: may be called from any worker thread. The task is not run
     * synchronously; it runs later on the UI thread (for FTXUI, after the next
     * event is pumped). An implementation must also request a repaint so the
     * screen reflects whatever the task changed.
     *
     * @param task The work to run on the UI thread; moved into the poster. Must
     * not be run by @ref post on the calling (worker) thread.
     */
    virtual void post(std::function<void()> task) = 0;
};

/**
 * @brief Opaque handle identifying one in-flight @ref AsyncCaller::submit.
 *
 * Returned by @ref AsyncCaller::submit and accepted by
 * @ref AsyncCaller::cancel. Values are unique and strictly increasing for the
 * life of a single @ref AsyncCaller; `0` is never issued, so callers may use it
 * as a "no token" sentinel.
 */
using CallToken = std::uint64_t;

/**
 * @brief Callback a screen supplies to receive a command's response.
 *
 * Always invoked on the UI thread (through @ref IUiPoster::post), exactly once
 * per accepted @ref AsyncCaller::submit unless the token was cancelled or the
 * owning @ref AsyncCaller was destroyed first, in which case it never runs. The
 * handler may freely touch UI components/state. It must not capture a raw
 * pointer to a screen that can be destroyed while a call is in flight; capture
 * a `std::weak_ptr` or a token instead (ADR 0003 rule 3, threat-model T-120).
 */
using ResponseHandler = std::function<void(const commands::CommandResponse&)>;

/**
 * @brief Runs every @ref ICommandClient::call off the UI thread and delivers
 * the response back on it (ADR 0003 rule 3, threat-model T-120).
 *
 * A screen calls @ref submit with a request and a handler; the request runs on
 * one of a fixed pool of worker threads, and the handler is posted back to the
 * UI thread through the injected @ref IUiPoster. @ref submit never blocks the
 * caller: a full queue is answered immediately with a synthesized `busy`
 * error envelope rather than blocking or silently dropping.
 *
 * Threading contract:
 * - @ref submit, @ref cancel, @ref in_flight and @ref busy may be called from
 *   the UI thread (the usual case) or any other thread; they are internally
 *   synchronized.
 * - `ICommandClient::call` runs on a worker thread, so the injected client must
 *   be safe to call concurrently from up to `workers` threads.
 *   `InProcessClient` is, because `CommandRegistry::execute` is const and
 *   re-entrant; if a future client is not, construct the caller with
 *   `workers = 1`.
 * - Every @ref ResponseHandler runs on the UI thread (the thread that drains
 *   the poster), never on a worker thread.
 *
 * Lifetime / teardown (T-120): the destructor marks the caller dead, stops and
 * joins all workers, and drops any still-queued requests. A completion that was
 * already posted to the UI thread but not yet drained becomes a no-op, so a
 * handler never runs against a destroyed screen and the destructor neither runs
 * a pending handler nor hangs. This guarantee relies on the poster being
 * drained on the same thread that destroys the caller (the FTXUI UI thread);
 * the injected @ref ICommandClient and @ref IUiPoster must outlive the caller.
 */
class AsyncCaller
{
public:
    /**
     * @brief Starts the worker pool bound to a client and a UI poster.
     *
     * @param client The command transport each worker calls; must be safe to
     * call concurrently from `workers` threads (see the class contract) and
     * must outlive this object. Not owned.
     * @param poster Delivers completions onto the UI thread; must outlive this
     * object and be drained on the thread that later destroys this object. Not
     * owned.
     * @param workers Number of worker threads to start; clamped to at least 1.
     * Defaults to 2. Use 1 for a client that is not safe for concurrent calls.
     */
    AsyncCaller(ICommandClient& client,
                IUiPoster& poster,
                std::size_t workers = 2);

    /**
     * @brief Stops and joins all workers; pending handlers never run after this
     * returns.
     *
     * Marks the caller dead so any completion already posted but not yet
     * drained is a no-op, signals the workers to stop, drops still-queued
     * requests, and joins. A worker already inside `ICommandClient::call`
     * finishes that one call (it cannot be interrupted) and then exits, so
     * destruction completes promptly. Never throws.
     */
    ~AsyncCaller();

    AsyncCaller(const AsyncCaller&) = delete;
    AsyncCaller& operator=(const AsyncCaller&) = delete;
    AsyncCaller(AsyncCaller&&) = delete;
    AsyncCaller& operator=(AsyncCaller&&) = delete;

    /**
     * @brief Queues @p request to run on a worker and @p on_done to receive its
     * response on the UI thread.
     *
     * Non-blocking: it returns as soon as the request is queued (or, when the
     * queue is already at capacity, after posting a synthesized `busy` error
     * envelope back through the poster). The handler runs on the UI thread,
     * once, unless the returned token is cancelled or this object is destroyed
     * before the completion is drained.
     *
     * @param request The command to run; moved into the queue. Its `id` is
     * echoed by the response the handler receives.
     * @param on_done Handler invoked on the UI thread with the response; moved
     * in. May be empty, in which case the response is simply discarded.
     * @return A non-zero @ref CallToken for this submission, usable with
     * @ref cancel. A fresh token is returned even for the `busy` overflow case.
     */
    CallToken submit(commands::CommandRequest request, ResponseHandler on_done);

    /**
     * @brief Suppresses the handler for an outstanding submission.
     *
     * The running command is not interrupted (true cancellation is a job-level
     * concern, out of scope here); the call still runs to completion on its
     * worker, but its result is discarded instead of being handed to the
     * handler. Cancelling an unknown, already-delivered or already-cancelled
     * token is a harmless no-op.
     *
     * @param token The token returned by a prior @ref submit.
     */
    void cancel(CallToken token);

    /**
     * @brief Returns how many submissions have not yet been delivered.
     *
     * A submission counts as in flight from @ref submit until its completion
     * task runs on the UI thread (whether the handler fired or was suppressed
     * by @ref cancel). Screens use this to skip redundant work while a call is
     * outstanding (e.g. the Jobs poll in subtask 16).
     *
     * @return The number of outstanding submissions; `0` when idle.
     */
    std::size_t in_flight() const;

    /**
     * @brief Convenience predicate over @ref in_flight.
     *
     * @return `true` when at least one submission is outstanding.
     */
    bool busy() const { return in_flight() > 0; }

    /**
     * @brief The injected UI-thread poster this caller delivers completions
     * through.
     *
     * Exposed so a screen that needs an off-FTXUI timer (the Jobs poll of
     * subtask 16) can post a UI-thread tick through the same poster without the
     * screen holding a second reference, keeping @ref IUiPoster out of
     * @ref ScreenContext. The returned reference is the one injected at
     * construction; it is owned by @ref App (it outlives this caller, which is
     * destroyed before it) and must only be used to @ref IUiPoster::post work
     * onto the UI thread.
     *
     * @return The poster reference; valid for this caller's whole lifetime.
     */
    IUiPoster& poster() const noexcept { return m_poster_; }

    /**
     * @brief Diagnostic/testing accessor for the recorded-cancellation count.
     *
     * Exposes the size of the internal cancel set so a test can prove it does
     * not leak: cancelling an unknown or already-delivered token records
     * nothing, and a delivered or cancelled token is erased (T-121). Not part
     * of the screen-facing contract.
     *
     * @return The number of tokens currently flagged for suppression.
     */
    std::size_t tracked_cancellations() const;

    /**
     * @brief Maximum number of requests that may be queued before @ref submit
     * answers immediately with a `busy` envelope.
     */
    static constexpr std::size_t k_queue_capacity = 64;

private:
    /**
     * @brief One queued unit of work: the token, the request, and its handler.
     */
    struct work_item
    {
        ///< Token identifying this submission; echoed to @ref cancel.
        CallToken token = 0;

        ///< The command to run on a worker thread.
        commands::CommandRequest request;

        ///< Handler to run on the UI thread with the response; may be empty.
        ResponseHandler on_done;
    };

    /**
     * @brief Worker-thread body: pops work, calls the client, posts the result.
     *
     * Loops until stopped, turning any exception from `ICommandClient::call`
     * into an `internal` error envelope so a handler always sees a well-formed
     * response.
     */
    void worker_loop();

    /**
     * @brief Posts a guarded completion task that delivers @p resp on the UI
     * thread.
     *
     * The task captures the shared @ref m_alive flag and checks it first, so it
     * is a no-op if this object was destroyed before the task was drained
     * (T-120).
     *
     * @param token The submission's token, checked against cancellations.
     * @param resp The response to deliver; moved into the task.
     * @param on_done The handler run when not cancelled; moved into the task.
     */
    void post_completion(CallToken token,
                         commands::CommandResponse resp,
                         ResponseHandler on_done);

    /**
     * @brief Runs on the UI thread: decrements the in-flight count and invokes
     * the handler unless the token was cancelled.
     *
     * @param token The submission's token; erased from the cancel set if
     * present.
     * @param resp The response to pass to the handler.
     * @param on_done The handler; not called when the token was cancelled or
     * the handler is empty.
     */
    void deliver(CallToken token,
                 const commands::CommandResponse& resp,
                 const ResponseHandler& on_done);

    ///< The command transport; not owned, must outlive this object.
    ICommandClient& m_client_;

    ///< The UI-thread poster; not owned, must outlive this object.
    IUiPoster& m_poster_;

    ///< Guards @ref m_queue_, @ref m_live_, @ref m_cancelled_,
    ///< @ref m_next_token_ and @ref m_stopping_.
    mutable std::mutex m_mutex_;

    ///< Signals workers that work was queued or that a stop was requested.
    std::condition_variable m_cv_;

    ///< Bounded FIFO of pending work (cap @ref k_queue_capacity).
    std::deque<work_item> m_queue_;

    ///< Tokens that have been submitted but not yet delivered. A token must be
    ///< live for @ref cancel to record it, and delivery erases it, so the
    ///< cancel set cannot leak entries for unknown or stale tokens (T-121).
    std::unordered_set<CallToken> m_live_;

    ///< Tokens whose handlers must be suppressed; a completion erases its own.
    ///< Only ever holds a subset of @ref m_live_.
    std::unordered_set<CallToken> m_cancelled_;

    ///< The worker threads; joined in the destructor.
    std::vector<std::thread> m_threads_;

    ///< Source of @ref CallToken values; pre-incremented so the first is 1.
    CallToken m_next_token_ = 0;

    ///< `true` once the destructor has asked the workers to stop.
    bool m_stopping_ = false;

    ///< Count of submissions not yet delivered; atomic so @ref in_flight is
    ///< lock-free.
    std::atomic<std::size_t> m_in_flight_ {0};

    ///< `true` while this object is alive. Shared with every posted completion
    ///< task; the destructor clears it so a not-yet-drained task is a no-op
    ///< (T-120). A `shared_ptr` so a task can hold the flag past destruction.
    std::shared_ptr<std::atomic<bool>> m_alive_;
};

/**
 * @brief Generates short, unique correlation ids for TUI-originated requests.
 *
 * Produces `"tui-1"`, `"tui-2"`, ... so each request the TUI issues carries a
 * distinct `id` the response echoes back. Thread-safe: @ref next may be called
 * concurrently from any thread. The returned id stays well within the 64-byte
 * request-id limit (`"tui-"` plus a decimal counter).
 */
class RequestIdGenerator
{
public:
    /**
     * @brief Returns the next unique request id.
     *
     * @return A string of the form `"tui-<n>"` with a strictly increasing
     * `<n>` starting at 1; always at most 24 bytes, so never over the 64-byte
     * id limit.
     */
    std::string next();

private:
    ///< Monotonic counter; pre-incremented so the first id is `"tui-1"`.
    std::atomic<std::uint64_t> m_counter_ {0};
};

}  // namespace url_shortener::tui
