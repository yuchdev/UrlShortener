#pragma once

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include <url_shortener/commands/command_descriptor.hpp>
#include <url_shortener/commands/job_store.hpp>
#include <url_shortener/commands/job_submitter.hpp>
#include <url_shortener/commands/job_types.hpp>
#include <url_shortener/observability/logger.hpp>

// Forward declaration: the runner holds the clock only by reference, forwarded
// straight to the @ref JobStore it owns; an incomplete type suffices here. The
// full definition lives in `core/clock.hpp`, included by `job_runner.cpp`.
// Keeping that include out of this header satisfies the commands/ include
// hygiene guard (01.0 subtask 13).
class IClock;

namespace url_shortener::commands
{

/**
 * @brief Construction-time bounds for a @ref JobRunner: how many workers, how
 * deep the queue, and how many finished records to retain.
 *
 * Every field bounds a resource a hostile or buggy caller could otherwise
 * exhaust (threat model T-86); all three are validated by the @ref JobRunner
 * constructor, which throws `std::invalid_argument` on a zero. The defaults are
 * the V1 tuning: two workers (the TUI import plus one export is the expected
 * peak), a sixteen-deep queue and 256 retained records.
 */
struct JobRunnerOptions
{
    ///< Number of always-on worker threads draining the queue; must be `>= 1`.
    std::size_t worker_count = 2;

    ///< Maximum number of *queued* (not yet running) jobs; a `submit` that
    ///< would exceed it returns `busy` and creates no record (threat model
    ///< T-86). Must be `>= 1`.
    std::size_t queue_capacity = 16;

    ///< Maximum number of terminal records retained, forwarded verbatim to the
    ///< owned @ref JobStore (which evicts oldest-first beyond it). Must be
    ///< `>= 1`.
    std::size_t max_retained_jobs = 256;
};

/**
 * @brief The @ref IJobContext a worker hands to a running @ref JobBody: reports
 * progress through the @ref JobStore and exposes the cooperative cancel flag.
 *
 * One instance is constructed on the worker thread for the duration of a single
 * body call and destroyed when the body returns; it is never shared across
 * threads. Cancellation is observed through a `std::shared_ptr` to an atomic
 * flag the runner also holds, so `job.cancel` can flip it from another thread
 * while the body polls it here (threat model T-90).
 *
 * Thread-safety: the owning worker is the only thread that calls
 * @ref set_total / @ref advance; @ref cancel_requested reads an atomic and is
 * safe to call concurrently with the runner setting the flag. The context takes
 * no runner lock, so a body may report progress without contending with
 * `submit`/`cancel`.
 */
class JobContext final : public IJobContext
{
public:
    /**
     * @brief Tests whether cancellation has been requested for this job.
     * @return `true` once @ref JobRunner::cancel flipped this job's flag; a
     * cooperative body should stop at its next checkpoint and report
     * `cancelled`. Reads the flag with relaxed ordering; `noexcept`.
     */
    bool cancel_requested() const noexcept override;

    /**
     * @brief Declares the total unit count, written through
     * @ref JobStore::update_progress.
     * @param total Expected number of units of work; recorded as the record's
     * `progress.total` alongside the current `done`. A failed store update
     * (e.g. the job is no longer `running`) is ignored: progress is
     * best-effort and never fails the body.
     */
    void set_total(std::uint64_t total) override;

    /**
     * @brief Advances the completed-unit counter, written through
     * @ref JobStore::update_progress.
     * @param n Number of units just completed; defaults to one. The running
     * total is monotonic; a failed store update is ignored (best-effort).
     */
    void advance(std::uint64_t n = 1) override;

private:
    friend class JobRunner;

    /**
     * @brief Constructs a context bound to one job record and its cancel flag.
     * @param store The record table to report progress to; held by reference
     * and must outlive this context (the owning @ref JobRunner guarantees it).
     * @param id The job id whose progress this context updates; copied in.
     * @param cancel Shared cancel flag, also held by the runner; never null.
     */
    JobContext(JobStore& store, std::string id,
               std::shared_ptr<std::atomic<bool>> cancel);

    ///< Record table that receives progress updates; not owned, outlives this.
    JobStore& m_store;

    ///< Id of the job this context reports for.
    std::string m_id;

    ///< Shared cooperative cancel flag; the runner flips it, the body polls it.
    std::shared_ptr<std::atomic<bool>> m_cancel;

    ///< Units completed so far, mirrored into the store on each @ref advance.
    std::uint64_t m_done = 0;

    ///< Last total declared via @ref set_total; re-sent on each @ref advance so
    ///< the record keeps a consistent `{done, total}` pair.
    std::uint64_t m_total = 0;
};

/**
 * @brief A small fixed worker pool fed by a bounded FIFO queue that runs
 * @ref JobBody units asynchronously; the one process-wide @ref IJobSubmitter
 * (decision log D13, injected as `std::shared_ptr`, Task 05.0 subtask 20).
 *
 * `submit` never blocks and never silently drops work: a full queue is rejected
 * with `busy` and creates no record (threat model T-86). Workers transition
 * each job `queued -> running -> {succeeded|failed|cancelled}` through the
 * owned
 * @ref JobStore and poll a per-job cooperative cancel flag through
 * @ref JobContext (threat model T-90). Bodies run on worker threads and must
 * therefore capture only `std::shared_ptr` copies and values, never references
 * to a per-request runtime or registry (threat model T-87).
 *
 * Thread-safety / lock order: the runner's own `m_mutex` guards the queue, the
 * cancel-flag map and the `m_stopping` flag; the @ref JobStore has its own
 * independent mutex. The single documented lock order is **`m_mutex` first,
 * then the store mutex, never the reverse** (threat model T-89): `submit` and
 * the queued-cancel path call into the store while holding `m_mutex`, while a
 * worker calls the store *without* `m_mutex` held and takes `m_mutex` only
 * afterwards to erase the flag. A body and the logger are never invoked while
 * `m_mutex` is held.
 *
 * Subtask 02 provides `submit`/`get`/`list`/`cancel` plus a minimal
 * `shutdown()` the destructor calls so no thread leaks; the public
 * `wait_for_terminal`/`wait_idle`/`cancel_all` drain surface and the full
 * `shutdown()` semantics are subtask 03 and slot onto the same members without
 * restructuring.
 */
class JobRunner final : public IJobSubmitter
{
public:
    /**
     * @brief Validates @p options, builds the owned @ref JobStore and starts
     * `options.worker_count` worker threads.
     *
     * @param clock Time source forwarded to the @ref JobStore for record
     * timestamps; held by reference, not owned, and must outlive this runner
     * (the composition root guarantees it).
     * @param options Pool/queue/retention bounds; see @ref JobRunnerOptions.
     * @throws std::invalid_argument if `worker_count`, `queue_capacity` or
     * `max_retained_jobs` is `0` (a programming error, not a runtime
     * condition). No thread is started and no store is built when it throws.
     */
    JobRunner(const IClock& clock, JobRunnerOptions options);

    /**
     * @brief Stops the pool and joins every worker so no thread outlives the
     * runner; delegates to the subtask-02 @ref shutdown.
     */
    ~JobRunner() override;

    JobRunner(const JobRunner&) = delete;
    JobRunner& operator=(const JobRunner&) = delete;
    JobRunner(JobRunner&&) = delete;
    JobRunner& operator=(JobRunner&&) = delete;

    /**
     * @brief Enqueues a prepared body, or rejects it with `busy` when the queue
     * is full (@ref IJobSubmitter).
     *
     * Takes `m_mutex`, and if the runner is stopping or the queue already holds
     * `queue_capacity` jobs returns `{nullopt, busy}` having created **no**
     * record (threat model T-86). Otherwise it creates a `queued` record in the
     * store, registers the job's cancel flag, pushes the body onto the queue
     * and
     * wakes one worker.
     *
     * @param command Validated dotted command name for the record; moved in.
     * @param args_digest First 16 hex of `SHA-256(args_json)`; moved in. Never
     * the raw arguments (contract C6, threat model T-13).
     * @param body Self-contained unit of work; must own every value it touches
     * (threat model T-87). Consumed on acceptance, left untouched on `busy`.
     * @return `{JobInfo{id, queued}, none}` on acceptance; `{nullopt, busy}`
     * when stopping or the queue is full.
     */
    JobSubmitResult submit(std::string command, std::string args_digest,
                           JobBody body) override;

    /**
     * @brief Returns a snapshot of one job record by id (delegates to the
     * store).
     * @param id Target job id.
     * @return A copy of the record, or `std::nullopt` if the id is unknown.
     */
    std::optional<JobRecord> get(const std::string& id) const;

    /**
     * @brief Returns a newest-first page of records, optionally filtered by
     * state (delegates to the store).
     * @param filter If set, only records in this state are returned;
     * `std::nullopt` lists all states.
     * @param offset Number of newest matching records to skip.
     * @param limit Maximum number of records to return; `0` yields an empty
     * page with the correct `total`.
     * @return A @ref JobPage with the clamped slice and the full match count.
     */
    JobPage list(std::optional<JobState> filter, std::size_t offset,
                 std::size_t limit) const;

    /**
     * @brief Requests cancellation of a job, with semantics by current state
     * (threat model T-90).
     *
     * - Unknown id: returns `std::nullopt`, nothing changes.
     * - Terminal job: a no-op; returns the record unchanged.
     * - Queued job: erased from the queue and finished `cancelled` without ever
     *   running its body; returns the now-`cancelled` record.
     * - Running job: sets the cooperative cancel flag and returns the current
     *   (still `running`) record; the body stops at its next checkpoint and
     *   reports `cancelled`, keeping any partial result. A body that never
     *   polls the flag runs to completion.
     *
     * @param id Target job id.
     * @return The affected record (post-transition for a queued job, current
     * state otherwise), or `std::nullopt` for an unknown id.
     */
    std::optional<JobRecord> cancel(const std::string& id);

    // wait_for_terminal / wait_idle / cancel_all and the full shutdown()
    // semantics are subtask 03; they extend the members below without
    // restructuring.

private:
    /**
     * @brief One queued unit of work: the id, its command name (for the finish
     * log line), the body and its shared cancel flag.
     */
    struct Pending
    {
        ///< Job id as allocated by @ref JobStore::create.
        std::string id;

        ///< Validated command name, carried so the worker can log
        ///< `job_finished` without a store lookup; no argument values (C6).
        std::string command;

        ///< The self-contained unit of work; owns every value it touches.
        JobBody body;

        ///< Shared cooperative cancel flag, also held in @ref m_flags.
        std::shared_ptr<std::atomic<bool>> cancel;
    };

    /**
     * @brief Validates @p options and returns them unchanged, for use in the
     * constructor's member-initializer list before any thread or store exists.
     * @param options Candidate bounds.
     * @return @p options verbatim when valid.
     * @throws std::invalid_argument if any of the three bounds is `0`.
     */
    static JobRunnerOptions validate_options(JobRunnerOptions options);

    /**
     * @brief Worker-thread main loop: wait for work or stop, pop one job under
     * the lock, then run it with the lock released.
     *
     * Returns (ending the thread) only when the runner is stopping and the
     * queue is drained, so a pending `shutdown()` drains outstanding queued
     * work
     * before the thread exits.
     */
    void worker_loop();

    /**
     * @brief Runs one popped job to completion off the lock: marks it running,
     * invokes the body in a `try/catch`, maps the outcome to a terminal state
     * and finishes the record, then erases the cancel flag and logs one
     * `job_finished` line.
     *
     * Exception mapping (threat model, contract C6): any exception escaping the
     * body becomes `failed` with a fixed `{internal, "job failed"}`; the
     * exception text is sent to the log only (it may contain paths) and never
     * to the stored failure message.
     *
     * @param job The popped unit of work; consumed.
     */
    void run_job(Pending job);

    /**
     * @brief Subtask-02 stop: sets `m_stopping`, wakes all workers and joins
     * them. Idempotent for the serialized (destructor) call path; subtask 03
     * replaces it with the full drain-and-shutdown semantics.
     */
    void shutdown();

    ///< Validated construction bounds; declared first so @ref m_store can read
    ///< `max_retained_jobs` from it in the initializer list.
    const JobRunnerOptions m_options;

    ///< Component logger (`"job_runner"`); used only off @ref m_mutex.
    observability::Logger m_logger;

    ///< The owned record table; has its own mutex and is the innermost lock.
    JobStore m_store;

    ///< Guards @ref m_queue, @ref m_flags and @ref m_stopping. Outermost
    ///< lock: acquired before the store mutex, never after (threat model T-89).
    mutable std::mutex m_mutex;

    ///< Signalled on every enqueue and on stop; workers wait on it.
    std::condition_variable m_cv;

    ///< FIFO of queued (not yet running) jobs; bounded by
    ///< `m_options.queue_capacity`.
    std::deque<Pending> m_queue;

    ///< Live cancel flags keyed by job id: present while a job is queued or
    ///< running, erased by the worker once the body returns (or on a queued
    ///< cancel). Lets @ref cancel reach a running body's flag.
    std::unordered_map<std::string, std::shared_ptr<std::atomic<bool>>>
        m_flags;

    ///< The worker threads; joined by @ref shutdown.
    std::vector<std::thread> m_workers;

    ///< `true` once @ref shutdown has begun; blocks new `submit`s and tells
    ///< drained workers to exit.
    bool m_stopping = false;
};

}  // namespace url_shortener::commands
