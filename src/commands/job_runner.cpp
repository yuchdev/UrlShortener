#include <chrono>
#include <exception>
#include <stdexcept>
#include <utility>

#include <url_shortener/commands/error_codes.hpp>
#include <url_shortener/commands/job_runner.hpp>
#include <url_shortener/core/clock.hpp>
#include <url_shortener/observability/logger_factory.hpp>

// ---------------------------------------------------------------------------
// Lock order (threat model T-89): there are exactly two locks in play -- the
// runner's own `m_mutex` (guarding m_queue, m_flags and m_stopping) and the
// JobStore's internal mutex. The single documented order is **m_mutex first,
// then the store mutex, never the reverse**: submit, cancel, cancel_all and
// the wait_* predicates call into the store while holding m_mutex; a worker
// calls the store *without* m_mutex (mark_running/finish/update_progress) and
// takes m_mutex only afterwards to erase a flag. A job body and the logger are
// never invoked while m_mutex is held. m_idle_cv is always signalled while
// holding m_mutex (never while the store mutex is held): publishing the store
// change behind the same lock the waiters hold while checking their predicate
// closes the lost-wakeup window between a waiter's predicate check and its
// block (threat model T-89).
// ---------------------------------------------------------------------------

namespace url_shortener::commands
{

namespace
{

/**
 * @brief Longest wait a @ref JobRunner blocking call will honour before it
 * treats the request as effectively unbounded.
 *
 * Caps the caller's timeout so that `steady_clock::now() + timeout` cannot
 * overflow the `time_point` (signed-overflow UB for a huge timeout such as
 * `std::chrono::milliseconds::max()`, threat model T-106). 24h is far beyond
 * any legitimate drain/wait, and a capped wait still returns the instant its
 * predicate holds, so the cap never changes an observable result.
 */
constexpr std::chrono::milliseconds k_max_wait_timeout = std::chrono::hours(24);

/**
 * @brief Computes a saturating wait deadline from @p timeout.
 *
 * @param timeout Caller-supplied maximum wait; clamped to
 * @ref k_max_wait_timeout before being added to the current time so the
 * addition cannot overflow `std::chrono::steady_clock::time_point` (UB). A
 * non-positive @p timeout yields a deadline at or before now, i.e. a single
 * predicate poll with no blocking.
 * @return `steady_clock::now()` plus the clamped timeout.
 */
std::chrono::steady_clock::time_point compute_deadline(
    std::chrono::milliseconds timeout)
{
    const std::chrono::milliseconds capped =
        timeout > k_max_wait_timeout ? k_max_wait_timeout : timeout;
    return std::chrono::steady_clock::now() + capped;
}

}  // namespace

// ---------------------------------------------------------------------------
// JobContext
// ---------------------------------------------------------------------------

JobContext::JobContext(JobStore& store,
                       std::string id,
                       std::shared_ptr<std::atomic<bool>> cancel)
    : m_store(store)
    , m_id(std::move(id))
    , m_cancel(std::move(cancel))
{
}

bool JobContext::cancel_requested() const noexcept
{
    return m_cancel->load(std::memory_order_relaxed);
}

void JobContext::set_total(std::uint64_t total)
{
    m_total = total;
    // Best-effort: the store rejects the update if the job is no longer
    // running, which is fine -- progress never fails the body.
    m_store.update_progress(m_id, JobProgress {m_done, m_total});
}

void JobContext::advance(std::uint64_t n)
{
    m_done += n;
    m_store.update_progress(m_id, JobProgress {m_done, m_total});
}

// ---------------------------------------------------------------------------
// JobRunner
// ---------------------------------------------------------------------------

JobRunnerOptions JobRunner::validate_options(JobRunnerOptions options)
{
    if (options.worker_count == 0) {
        throw std::invalid_argument("JobRunner: worker_count must be >= 1");
    }
    if (options.queue_capacity == 0) {
        throw std::invalid_argument("JobRunner: queue_capacity must be >= 1");
    }
    if (options.max_retained_jobs == 0) {
        throw std::invalid_argument(
            "JobRunner: max_retained_jobs must be >= 1");
    }
    return options;
}

JobRunner::JobRunner(const IClock& clock, JobRunnerOptions options)
    : m_options(validate_options(options))
    , m_logger(observability::LoggerFactory::Component("job_runner"))
    , m_store(clock, m_options.max_retained_jobs)
{
    m_workers.reserve(m_options.worker_count);
    m_worker_ids.reserve(m_options.worker_count);
    for (std::size_t i = 0; i < m_options.worker_count; ++i) {
        m_workers.emplace_back([this] { worker_loop(); });
        // Record the worker's id while it is live and before any join, so
        // shutdown() can detect a self-join (a body calling shutdown runs on a
        // worker thread). The vector is immutable after construction, so
        // is_worker_thread() reads it without a lock (threat model T-89).
        m_worker_ids.push_back(m_workers.back().get_id());
    }
}

bool JobRunner::is_worker_thread() const noexcept
{
    const std::thread::id self = std::this_thread::get_id();
    for (const std::thread::id& id : m_worker_ids) {
        if (id == self) {
            return true;
        }
    }
    return false;
}

JobRunner::~JobRunner()
{
    shutdown();
}

JobSubmitResult JobRunner::submit(std::string command,
                                  std::string args_digest,
                                  JobBody body)
{
    std::lock_guard<std::mutex> lock(m_mutex);

    // A full queue (or a runner that is stopping) is rejected with busy and
    // creates no record: nothing accepted work is ever silently dropped
    // (threat model T-86, ADR 0002 section 3).
    if (m_stopping || m_queue.size() >= m_options.queue_capacity) {
        return JobSubmitResult {
            std::nullopt,
            make_error(CommandErrorCode::busy, "job queue is full")};
    }

    // m_mutex is held while calling into the store: the one documented lock
    // order is m_mutex before the store mutex, never the reverse (T-89).
    JobRecord record =
        m_store.create(std::move(command), std::move(args_digest));
    auto cancel = std::make_shared<std::atomic<bool>>(false);
    m_flags.emplace(record.id, cancel);
    m_queue.push_back(
        Pending {record.id, record.command, std::move(body), cancel});
    m_cv.notify_one();

    return JobSubmitResult {JobInfo {record.id, JobState::queued}, {}};
}

std::optional<JobRecord> JobRunner::get(const std::string& id) const
{
    return m_store.get(id);
}

JobPage JobRunner::list(std::optional<JobState> filter,
                        std::size_t offset,
                        std::size_t limit) const
{
    return m_store.list(filter, offset, limit);
}

JobSummaryPage JobRunner::list_summaries(std::optional<JobState> filter,
                                         std::size_t offset,
                                         std::size_t limit) const
{
    // Thin forward: no runner lock is taken here, so the store's own (innermost)
    // mutex is the only lock involved and the m_mutex -> store order (T-89) is
    // trivially preserved. list_summaries copies metadata only, never the up-to-
    // ~1 MiB result_json, under that store mutex (threat model T-109).
    return m_store.list_summaries(filter, offset, limit);
}

std::optional<JobRecord> JobRunner::cancel(const std::string& id)
{
    {
        std::lock_guard<std::mutex> lock(m_mutex);

        // Read the current state under m_mutex -> store order (T-89).
        std::optional<JobRecord> record = m_store.get(id);
        if (!record) {
            return std::nullopt;  // unknown id
        }
        if (is_terminal(record->state)) {
            return record;  // terminal: no-op, return unchanged
        }

        // Queued and still in the queue: cancel it without ever running it.
        for (auto it = m_queue.begin(); it != m_queue.end(); ++it) {
            if (it->id == id) {
                m_queue.erase(it);
                m_flags.erase(id);
                m_store.finish(
                    id,
                    JobState::cancelled,
                    std::nullopt,
                    JobFailure {std::string(error_info_for(
                                                CommandErrorCode::job_cancelled)
                                                .name),
                                "job cancelled"});
                std::optional<JobRecord> cancelled = m_store.get(id);
                // A queued-job cancel can drop active_count to zero; wake any
                // wait_idle/wait_for_terminal waiter (spec step 1).
                m_idle_cv.notify_all();
                return cancelled;
            }
        }

        // Not in the queue: it is running (or has just been popped and is about
        // to run). Flip the cooperative flag; the body stops at its next
        // checkpoint. The state stays running until the body returns.
        auto flag_it = m_flags.find(id);
        if (flag_it != m_flags.end()) {
            flag_it->second->store(true, std::memory_order_relaxed);
        }
    }

    return m_store.get(id);
}

void JobRunner::worker_loop()
{
    for (;;) {
        Pending job;
        {
            std::unique_lock<std::mutex> lock(m_mutex);
            m_cv.wait(lock, [this] { return m_stopping || !m_queue.empty(); });

            // Drain remaining queued work even while stopping; only exit once
            // the queue is empty (subtask 03 refines the drain policy).
            if (m_queue.empty()) {
                return;
            }

            job = std::move(m_queue.front());
            m_queue.pop_front();
        }

        // The body and the logger run with the lock released (spec step 4).
        run_job(std::move(job));
    }
}

void JobRunner::run_job(Pending job)
{
    m_store.mark_running(job.id);

    JobState final_state = JobState::failed;
    std::optional<std::string> result_json;
    std::optional<JobFailure> failure;

    try {
        JobContext context(m_store, job.id, job.cancel);
        JobOutcome outcome = job.body(context);

        if (outcome.cancelled) {
            final_state = JobState::cancelled;
            result_json = std::move(outcome.result_json);
            failure = JobFailure {
                std::string(
                    error_info_for(CommandErrorCode::job_cancelled).name),
                "job cancelled"};
        }
        else if (outcome.ok) {
            final_state = JobState::succeeded;
            result_json = std::move(outcome.result_json);
        }
        else {
            final_state = JobState::failed;
            failure = JobFailure {
                std::string(error_info_for(outcome.failure.code).name),
                outcome.failure.message};
        }
    } catch (const std::exception& ex) {
        // The exception text may contain paths/args, so it goes to the log
        // only; the stored failure carries fixed, value-free text (contract
        // C6).
        final_state = JobState::failed;
        failure = JobFailure {
            std::string(error_info_for(CommandErrorCode::internal).name),
            "job failed"};
        m_logger.error(
            "job body threw",
            {{"id", job.id}, {"command", job.command}, {"detail", ex.what()}});
    } catch (...) {
        final_state = JobState::failed;
        failure = JobFailure {
            std::string(error_info_for(CommandErrorCode::internal).name),
            "job failed"};
        m_logger.error("job body threw",
                       {{"id", job.id}, {"command", job.command}});
    }

    m_store.finish(
        job.id, final_state, std::move(result_json), std::move(failure));

    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_flags.erase(job.id);

        // The job is now terminal, so active_count may have reached zero; wake
        // any wait_idle/wait_for_terminal waiter. Signalled while holding
        // m_mutex (never with the store mutex held) so a waiter that has just
        // evaluated its predicate cannot block after this notify and miss it:
        // the m_store.finish() above is published behind the same lock the
        // waiters hold while re-checking (lost-wakeup fix, T-89).
        m_idle_cv.notify_all();
    }

    // One line per finished job; id/command/state only, never argument values
    // (contract C6, threat model T-108).
    m_logger.info("job_finished",
                  {{"id", job.id},
                   {"command", job.command},
                   {"state", std::string(to_string(final_state))}});
}

std::optional<JobRecord> JobRunner::wait_for_terminal(
    const std::string& id, std::chrono::milliseconds timeout) const
{
    std::unique_lock<std::mutex> lock(m_mutex);

    // Read under m_mutex -> store order (T-89). Unknown id never becomes known.
    std::optional<JobRecord> record = m_store.get(id);
    if (!record) {
        return std::nullopt;
    }

    // Saturating add: a huge timeout (e.g. milliseconds::max()) would overflow
    // steady_clock::time_point otherwise (UB, threat model T-106).
    const auto deadline = compute_deadline(timeout);
    m_idle_cv.wait_until(lock,
                         deadline,
                         [this, &id, &record]
                         {
                             // Re-read the snapshot on every wake; stop once
                             // terminal (or evicted).
                             record = m_store.get(id);
                             return !record.has_value()
                                 || is_terminal(record->state);
                         });
    return record;
}

bool JobRunner::wait_idle(std::chrono::milliseconds timeout) const
{
    std::unique_lock<std::mutex> lock(m_mutex);
    // Saturating add: a huge timeout (e.g. milliseconds::max()) would overflow
    // steady_clock::time_point otherwise (UB, threat model T-106).
    const auto deadline = compute_deadline(timeout);
    // active_count() == 0 means no queued and no running job (T-88 drain gate).
    return m_idle_cv.wait_until(
        lock, deadline, [this] { return m_store.active_count() == 0; });
}

std::size_t JobRunner::cancel_all()
{
    std::size_t affected = 0;
    {
        std::lock_guard<std::mutex> lock(m_mutex);

        // Queued jobs: remove and finish cancelled without ever running the
        // body (T-88: the backlog is not run during stop). m_mutex -> store.
        while (!m_queue.empty()) {
            Pending job = std::move(m_queue.front());
            m_queue.pop_front();
            m_flags.erase(job.id);
            m_store.finish(
                job.id,
                JobState::cancelled,
                std::nullopt,
                JobFailure {
                    std::string(
                        error_info_for(CommandErrorCode::job_cancelled).name),
                    "job cancelled"});
            ++affected;
        }

        // Running (and just-popped) jobs: flip the cooperative flag so a
        // polling body stops at its next checkpoint (T-90). A body that never
        // polls runs to completion -- documented residual risk on shutdown().
        // A flag can briefly outlive its job's terminal transition, because a
        // worker calls finish() off-lock and erases the flag only afterwards;
        // skip any flag whose job is already terminal so the affected count
        // reflects genuinely-running jobs and a second call is a clean no-op.
        for (auto& entry : m_flags) {
            std::optional<JobRecord> record = m_store.get(entry.first);
            if (record && !is_terminal(record->state)) {
                entry.second->store(true, std::memory_order_relaxed);
                ++affected;
            }
        }

        // Queued cancels may have dropped active_count; wake any drain waiter.
        // Signalled while holding m_mutex (the store changes above are
        // published behind the same lock the waiters re-check under) so a
        // waiter cannot block just after this notify and miss it (lost-wakeup
        // fix, T-89).
        m_idle_cv.notify_all();
    }

    return affected;
}

void JobRunner::shutdown()
{
    // A body may call shutdown(); it then runs on a worker thread and must
    // never join itself (a thread joining itself is UB / throws
    // resource_deadlock_would_occur). Captured before taking any lock from the
    // immutable worker-id list (threat model T-89, T-106).
    const bool from_worker = is_worker_thread();

    bool do_stop = false;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (!m_stopping) {
            // First caller to observe m_stopping as false owns the one-time
            // stop signal; every later or concurrent call skips it. Idempotent
            // and concurrency-safe (T-89).
            m_stopping = true;
            do_stop = true;
        }
    }

    if (do_stop) {
        // Cancel queued work (never run it) and flag running cooperative bodies
        // so a long body is told to stop rather than blocking the join
        // unboundedly (T-88). A non-cooperative body still blocks the join
        // until it returns -- C++ cannot forcibly stop a thread -- the
        // documented limitation.
        cancel_all();

        // Wake every worker; a drained worker observes m_stopping and exits.
        m_cv.notify_all();

        // Value-free line only (contract C6): no argument values on exit paths.
        m_logger.info("job_runner_shutdown");
    }

    if (from_worker) {
        // Stop + cancel is done; skip the join. The calling worker cannot join
        // itself, and joining the other workers here would race the eventual
        // owner-side join. The join is completed by the destructor (or any
        // other non-worker caller), which blocks on m_join_mutex below until
        // every worker -- including this one, once its body returns -- has
        // exited (T-89, T-106).
        return;
    }

    // Join under m_join_mutex so concurrent non-worker callers serialize: the
    // first joins every worker, the rest find them non-joinable and skip, so no
    // worker is ever joined twice and every caller returns only after the join
    // has completed (T-89).
    std::lock_guard<std::mutex> join_lock(m_join_mutex);
    for (std::thread& worker : m_workers) {
        if (worker.joinable()) {
            worker.join();
        }
    }
}

}  // namespace url_shortener::commands
