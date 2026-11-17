#include <url_shortener/commands/job_runner.hpp>

#include <exception>
#include <stdexcept>
#include <utility>

#include <url_shortener/commands/error_codes.hpp>
#include <url_shortener/core/clock.hpp>
#include <url_shortener/observability/logger_factory.hpp>

namespace url_shortener::commands
{

// ---------------------------------------------------------------------------
// JobContext
// ---------------------------------------------------------------------------

JobContext::JobContext(JobStore& store, std::string id,
                       std::shared_ptr<std::atomic<bool>> cancel)
    : m_store(store), m_id(std::move(id)), m_cancel(std::move(cancel))
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
    m_store.update_progress(m_id, JobProgress{m_done, m_total});
}

void JobContext::advance(std::uint64_t n)
{
    m_done += n;
    m_store.update_progress(m_id, JobProgress{m_done, m_total});
}

// ---------------------------------------------------------------------------
// JobRunner
// ---------------------------------------------------------------------------

JobRunnerOptions JobRunner::validate_options(JobRunnerOptions options)
{
    if (options.worker_count == 0)
    {
        throw std::invalid_argument("JobRunner: worker_count must be >= 1");
    }
    if (options.queue_capacity == 0)
    {
        throw std::invalid_argument("JobRunner: queue_capacity must be >= 1");
    }
    if (options.max_retained_jobs == 0)
    {
        throw std::invalid_argument(
            "JobRunner: max_retained_jobs must be >= 1");
    }
    return options;
}

JobRunner::JobRunner(const IClock& clock, JobRunnerOptions options)
    : m_options(validate_options(options)),
      m_logger(observability::LoggerFactory::Component("job_runner")),
      m_store(clock, m_options.max_retained_jobs)
{
    m_workers.reserve(m_options.worker_count);
    for (std::size_t i = 0; i < m_options.worker_count; ++i)
    {
        m_workers.emplace_back([this] { worker_loop(); });
    }
}

JobRunner::~JobRunner()
{
    shutdown();
}

JobSubmitResult JobRunner::submit(std::string command, std::string args_digest,
                                  JobBody body)
{
    std::lock_guard<std::mutex> lock(m_mutex);

    // A full queue (or a runner that is stopping) is rejected with busy and
    // creates no record: nothing accepted work is ever silently dropped
    // (threat model T-86, ADR 0002 section 3).
    if (m_stopping || m_queue.size() >= m_options.queue_capacity)
    {
        return JobSubmitResult{
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
        Pending{record.id, record.command, std::move(body), cancel});
    m_cv.notify_one();

    return JobSubmitResult{JobInfo{record.id, JobState::queued}, {}};
}

std::optional<JobRecord> JobRunner::get(const std::string& id) const
{
    return m_store.get(id);
}

JobPage JobRunner::list(std::optional<JobState> filter, std::size_t offset,
                        std::size_t limit) const
{
    return m_store.list(filter, offset, limit);
}

std::optional<JobRecord> JobRunner::cancel(const std::string& id)
{
    {
        std::lock_guard<std::mutex> lock(m_mutex);

        // Read the current state under m_mutex -> store order (T-89).
        std::optional<JobRecord> record = m_store.get(id);
        if (!record)
        {
            return std::nullopt;  // unknown id
        }
        if (is_terminal(record->state))
        {
            return record;  // terminal: no-op, return unchanged
        }

        // Queued and still in the queue: cancel it without ever running it.
        for (auto it = m_queue.begin(); it != m_queue.end(); ++it)
        {
            if (it->id == id)
            {
                m_queue.erase(it);
                m_flags.erase(id);
                m_store.finish(
                    id, JobState::cancelled, std::nullopt,
                    JobFailure{std::string(error_info_for(
                                   CommandErrorCode::job_cancelled)
                                   .name),
                               "job cancelled"});
                return m_store.get(id);
            }
        }

        // Not in the queue: it is running (or has just been popped and is about
        // to run). Flip the cooperative flag; the body stops at its next
        // checkpoint. The state stays running until the body returns.
        auto flag_it = m_flags.find(id);
        if (flag_it != m_flags.end())
        {
            flag_it->second->store(true, std::memory_order_relaxed);
        }
    }

    return m_store.get(id);
}

void JobRunner::worker_loop()
{
    for (;;)
    {
        Pending job;
        {
            std::unique_lock<std::mutex> lock(m_mutex);
            m_cv.wait(lock,
                      [this] { return m_stopping || !m_queue.empty(); });

            // Drain remaining queued work even while stopping; only exit once
            // the queue is empty (subtask 03 refines the drain policy).
            if (m_queue.empty())
            {
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

    try
    {
        JobContext context(m_store, job.id, job.cancel);
        JobOutcome outcome = job.body(context);

        if (outcome.cancelled)
        {
            final_state = JobState::cancelled;
            result_json = std::move(outcome.result_json);
            failure = JobFailure{
                std::string(
                    error_info_for(CommandErrorCode::job_cancelled).name),
                "job cancelled"};
        }
        else if (outcome.ok)
        {
            final_state = JobState::succeeded;
            result_json = std::move(outcome.result_json);
        }
        else
        {
            final_state = JobState::failed;
            failure = JobFailure{
                std::string(error_info_for(outcome.failure.code).name),
                outcome.failure.message};
        }
    }
    catch (const std::exception& ex)
    {
        // The exception text may contain paths/args, so it goes to the log
        // only; the stored failure carries fixed, value-free text (contract
        // C6).
        final_state = JobState::failed;
        failure = JobFailure{
            std::string(error_info_for(CommandErrorCode::internal).name),
            "job failed"};
        m_logger.error("job body threw",
                       {{"id", job.id},
                        {"command", job.command},
                        {"detail", ex.what()}});
    }
    catch (...)
    {
        final_state = JobState::failed;
        failure = JobFailure{
            std::string(error_info_for(CommandErrorCode::internal).name),
            "job failed"};
        m_logger.error("job body threw",
                       {{"id", job.id}, {"command", job.command}});
    }

    m_store.finish(job.id, final_state, std::move(result_json),
                   std::move(failure));

    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_flags.erase(job.id);
    }

    // One line per finished job; id/command/state only, never argument values
    // (contract C6, threat model T-108).
    m_logger.info("job_finished",
                  {{"id", job.id},
                   {"command", job.command},
                   {"state", std::string(to_string(final_state))}});
}

void JobRunner::shutdown()
{
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_stopping)
        {
            // Idempotent for the serialized (destructor) call path; subtask 03
            // hardens this against concurrent shutdown.
            return;
        }
        m_stopping = true;
    }
    m_cv.notify_all();
    for (std::thread& worker : m_workers)
    {
        if (worker.joinable())
        {
            worker.join();
        }
    }
}

}  // namespace url_shortener::commands
