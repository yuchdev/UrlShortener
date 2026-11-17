#include <url_shortener/commands/job_store.hpp>

#include <cassert>
#include <stdexcept>
#include <utility>

#include <url_shortener/core/clock.hpp>

namespace url_shortener::commands
{

std::string_view to_string(JobState state) noexcept
{
    // Single source of truth: delegate to the envelope's name table (01.0) so
    // the store and the wire never disagree on a state name.
    return job_state_name(state);
}

std::optional<JobState> parse_job_state(std::string_view text) noexcept
{
    return job_state_from_name(text);
}

bool is_terminal(JobState state) noexcept
{
    switch (state)
    {
        case JobState::queued:
        case JobState::running:
            return false;
        case JobState::succeeded:
        case JobState::failed:
        case JobState::cancelled:
            return true;
    }
    return false;
}

namespace
{

// Returns a pointer to the record with the given id, or nullptr if none.
// Caller must hold the store mutex. The table is small (bounded by
// max_retained plus active jobs), so a linear scan is adequate (spec note:
// switch to an index map only if the default retention grows).
JobRecord* find_locked(std::deque<JobRecord>& records, const std::string& id)
{
    for (auto& record : records)
    {
        if (record.id == id)
        {
            return &record;
        }
    }
    return nullptr;
}

// Copies only the eight metadata fields of @p record into a @ref JobSummary.
// Deliberately never touches `result_json` (up to ~1 MiB) or `args_digest`
// (threat model T-109): the whole point of list_summaries is that this copy
// stays bounded to small scalar/short-string fields under the store mutex.
JobSummary summarize(const JobRecord& record)
{
    JobSummary summary;
    summary.id = record.id;
    summary.command = record.command;
    summary.state = record.state;
    summary.progress = record.progress;
    summary.failure = record.failure;
    summary.created_at = record.created_at;
    summary.started_at = record.started_at;
    summary.finished_at = record.finished_at;
    return summary;
}

}  // namespace

JobStore::JobStore(const IClock& clock, std::size_t max_retained)
    : m_clock(clock), m_max_retained(max_retained)
{
    if (max_retained == 0)
    {
        throw std::invalid_argument("JobStore: max_retained must be >= 1");
    }
}

JobRecord JobStore::create(std::string command, std::string args_digest)
{
    std::lock_guard<std::mutex> lock(m_mutex);

    JobRecord record;
    record.id = "j-" + std::to_string(m_next_id++);
    record.command = std::move(command);
    record.args_digest = std::move(args_digest);
    record.state = JobState::queued;
    record.created_at = m_clock.now();

    m_records.push_back(record);
    return record;
}

bool JobStore::mark_running(const std::string& id)
{
    std::lock_guard<std::mutex> lock(m_mutex);

    JobRecord* record = find_locked(m_records, id);
    if (record == nullptr || record->state != JobState::queued)
    {
        return false;
    }

    record->state = JobState::running;
    record->started_at = m_clock.now();
    return true;
}

bool JobStore::update_progress(const std::string& id, JobProgress progress)
{
    std::lock_guard<std::mutex> lock(m_mutex);

    JobRecord* record = find_locked(m_records, id);
    if (record == nullptr || record->state != JobState::running)
    {
        return false;
    }
    if (progress.done < record->progress.done)
    {
        return false;
    }

    record->progress = progress;
    return true;
}

bool JobStore::finish(const std::string& id, JobState terminal,
                      std::optional<std::string> result_json,
                      std::optional<JobFailure> failure)
{
    // A non-terminal `terminal` argument is a programming error: assert in
    // debug and fail closed in release rather than corrupt the lifecycle.
    assert(is_terminal(terminal) && "finish: terminal state required");
    if (!is_terminal(terminal))
    {
        return false;
    }

    std::lock_guard<std::mutex> lock(m_mutex);

    JobRecord* record = find_locked(m_records, id);
    if (record == nullptr || is_terminal(record->state))
    {
        return false;
    }

    record->state = terminal;
    record->finished_at = m_clock.now();
    record->result_json = std::move(result_json);
    record->failure = std::move(failure);

    evict_locked();
    return true;
}

std::optional<JobRecord> JobStore::get(const std::string& id) const
{
    std::lock_guard<std::mutex> lock(m_mutex);

    for (const auto& record : m_records)
    {
        if (record.id == id)
        {
            return record;
        }
    }
    return std::nullopt;
}

JobPage JobStore::list(std::optional<JobState> filter, std::size_t offset,
                       std::size_t limit) const
{
    std::lock_guard<std::mutex> lock(m_mutex);

    JobPage page;

    // Newest first: iterate the creation-ordered deque from the back. Count the
    // full match set for `total`, then emit only the [offset, offset+limit)
    // window; both clamps fall out naturally (offset beyond size -> empty,
    // limit 0 -> empty) with `total` still correct.
    for (auto it = m_records.rbegin(); it != m_records.rend(); ++it)
    {
        if (filter.has_value() && it->state != *filter)
        {
            continue;
        }

        const std::size_t index = page.total;
        ++page.total;
        if (index < offset)
        {
            continue;
        }
        if (page.items.size() < limit)
        {
            page.items.push_back(*it);
        }
    }

    return page;
}

JobSummaryPage JobStore::list_summaries(std::optional<JobState> filter,
                                        std::size_t offset,
                                        std::size_t limit) const
{
    std::lock_guard<std::mutex> lock(m_mutex);

    JobSummaryPage page;

    // Identical window/filter/order logic to list() (newest first, total counts
    // the full match set, both clamps fall out naturally), but each emitted item
    // is a metadata-only JobSummary: summarize() copies only the eight small
    // fields under the lock and never the up-to-~1 MiB result_json (threat model
    // T-109 / CWE-400). Keeping the two loops in lock-step is covered by the
    // behavioural-equality tests in 50_job_store.cpp.
    for (auto it = m_records.rbegin(); it != m_records.rend(); ++it)
    {
        if (filter.has_value() && it->state != *filter)
        {
            continue;
        }

        const std::size_t index = page.total;
        ++page.total;
        if (index < offset)
        {
            continue;
        }
        if (page.items.size() < limit)
        {
            page.items.push_back(summarize(*it));
        }
    }

    return page;
}

std::size_t JobStore::active_count() const
{
    std::lock_guard<std::mutex> lock(m_mutex);

    std::size_t count = 0;
    for (const auto& record : m_records)
    {
        if (!is_terminal(record.state))
        {
            ++count;
        }
    }
    return count;
}

void JobStore::evict_locked()
{
    // Drop oldest-first, but only terminal records: active (queued/running)
    // records are never evicted, so the table may stay above m_max_retained
    // while enough jobs are still active (threat model T-86).
    while (m_records.size() > m_max_retained)
    {
        bool erased = false;
        for (auto it = m_records.begin(); it != m_records.end(); ++it)
        {
            if (is_terminal(it->state))
            {
                m_records.erase(it);
                erased = true;
                break;
            }
        }
        if (!erased)
        {
            break;
        }
    }
}

}  // namespace url_shortener::commands
