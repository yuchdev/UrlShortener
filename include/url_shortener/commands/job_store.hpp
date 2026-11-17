#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <mutex>
#include <optional>
#include <string>

#include <url_shortener/commands/job_types.hpp>

// Forward declaration: @ref JobStore holds the clock only by reference (as a
// parameter and a member), which needs an incomplete type here. The full
// definition lives in `core/clock.hpp`, included by `job_store.cpp`. Keeping
// that include out of this header satisfies the commands/ include-hygiene guard
// (01.0 subtask 13), which the textual scan enforces over every public header.
class IClock;

namespace url_shortener::commands
{

/**
 * @brief Thread-safe in-memory table of @ref JobRecord with id allocation,
 * legal state transitions and bounded retention of finished jobs.
 *
 * This is the pure data model mutated by `JobRunner` (subtask 02) and read by
 * the `job.get`/`job.list`/`job.cancel` commands (subtask 05); it owns no
 * threads, no queue and no command logic. It is the only owner of the record
 * table and the sole enforcer of the legal lifecycle
 * (`queued -> running -> {succeeded|failed}`, and `queued|running ->
 * cancelled`); every illegal transition is rejected without mutating the
 * record.
 *
 * Thread-safety: every public method is safe to call concurrently from any
 * thread. A single internal `std::mutex` serializes all access, so id
 * allocation (threat model T-86: ids are unique even under concurrent
 * `create`) and transitions are atomic with respect to each other. The mutex
 * is the innermost lock: no callback runs while it is held.
 *
 * Lifetime: does not own the injected @ref IClock; the caller (composition
 * root) must keep it alive for the lifetime of this store.
 *
 * Retention (threat model T-86/T-109): finished (terminal) records beyond
 * `max_retained` are evicted oldest-first after each @ref finish, bounding
 * retained memory (including inline `result_json`); `queued`/`running` records
 * are never evicted, so the table can only exceed `max_retained` by the number
 * of currently active jobs, which `JobRunner` separately bounds.
 */
class JobStore
{
public:
    /**
     * @brief Constructs an empty store that stamps timestamps from @p clock and
     * retains at most @p max_retained terminal records.
     *
     * @param clock Time source for `created_at`/`started_at`/`finished_at`;
     * read on every mutation, not copied, and must outlive this store (it is
     * held by reference, not owned).
     * @param max_retained Maximum number of terminal records kept before the
     * oldest are evicted; must be `>= 1`.
     * @throws std::invalid_argument if @p max_retained is `0` (a programming
     * error, not an expected runtime condition).
     */
    JobStore(const IClock& clock, std::size_t max_retained);

    /**
     * @brief Creates a new `queued` record with a freshly allocated id.
     *
     * Allocates the next id `"j-<n>"` (ids are correlation handles only, never
     * an authorization capability, threat model T-93), stamps `created_at` from
     * the injected clock, and appends the record (insertion order is creation
     * order). Does not evict: creation never produces a terminal record.
     *
     * @param command Dotted command name for the record (e.g. `"link.import"`);
     * moved in.
     * @param args_digest First 16 hex of `SHA-256(args_json)`; moved in. Never
     * the raw arguments (milestone contract C6, threat model T-86).
     * @return A copy of the newly created record, with `state == queued` and a
     * unique id.
     */
    JobRecord create(std::string command, std::string args_digest);

    /**
     * @brief Transitions a `queued` job to `running` and stamps `started_at`.
     *
     * @param id Target job id.
     * @return `true` if the job existed and was `queued` (now `running`);
     * `false` if the id is unknown or the job is not `queued` (already
     * `running` or terminal), in which case the record is left unchanged.
     */
    bool mark_running(const std::string& id);

    /**
     * @brief Replaces the progress counters of a `running` job.
     *
     * @param id Target job id.
     * @param progress New counters; `progress.done` must be `>=` the record's
     * current `progress.done` (progress is monotonic).
     * @return `true` if the job existed, was `running` and `progress.done` did
     * not decrease (counters updated); `false` if the id is unknown, the job is
     * not `running`, or `progress.done` would decrease, in which case the
     * record is left unchanged.
     */
    bool update_progress(const std::string& id, JobProgress progress);

    /**
     * @brief Transitions a non-terminal job to a terminal state, stamping
     * `finished_at`, then evicts over-retained terminal records.
     *
     * Sets `finished_at` from the clock and attaches @p result_json and
     * @p failure as given (nothing else is cleared). After the transition,
     * oldest-first eviction of terminal records beyond `max_retained` runs
     * (threat model T-86/T-109).
     *
     * @param id Target job id.
     * @param terminal The terminal state to move to; must satisfy
     * @ref is_terminal. A non-terminal value is a programming error (`assert`
     * in debug) and yields `false`.
     * @param result_json Optional opaque result text to retain on the record
     * (typically set for `succeeded`); moved in.
     * @param failure Optional failure detail to retain (typically set for
     * `failed`); moved in.
     * @return `true` if the job existed, was `queued` or `running`, and
     * @p terminal is terminal (now moved to @p terminal); `false` if the id is
     * unknown, the job is already terminal, or @p terminal is not terminal, in
     * which case the record is left unchanged.
     */
    bool finish(const std::string& id, JobState terminal,
                std::optional<std::string> result_json,
                std::optional<JobFailure> failure);

    /**
     * @brief Returns a snapshot copy of a single record by id.
     *
     * @param id Target job id.
     * @return A copy of the record, or `std::nullopt` if the id is unknown.
     */
    std::optional<JobRecord> get(const std::string& id) const;

    /**
     * @brief Returns a newest-first page of records, optionally filtered by
     * state.
     *
     * @param filter If set, only records in this state are considered;
     * `std::nullopt` lists all states.
     * @param offset Number of matching records to skip from the newest end; an
     * offset at or beyond the match count yields an empty `items` with the
     * correct `total`.
     * @param limit Maximum number of records to return; `0` yields an empty
     * `items` (with the correct `total`).
     * @return A @ref JobPage whose `items` is the clamped, newest-first slice
     * and whose `total` is the full count matching @p filter before slicing.
     */
    JobPage list(std::optional<JobState> filter, std::size_t offset,
                 std::size_t limit) const;

    /**
     * @brief Returns the number of active (`queued` + `running`) records.
     *
     * @return The count of non-terminal records currently in the table; used by
     * `JobRunner` drain logic (subtask 03) to tell when work is outstanding.
     */
    std::size_t active_count() const;

private:
    /**
     * @brief Evicts oldest-first terminal records until the table size is at
     * most `m_max_retained`.
     *
     * Must be called with @ref m_mutex held. Only terminal records are
     * eligible; `queued`/`running` records are skipped, so the table may remain
     * above `m_max_retained` when enough jobs are still active.
     */
    void evict_locked();

    ///< Time source for all record timestamps; not owned, must outlive this
    ///< store (see the constructor lifetime contract).
    const IClock& m_clock;

    ///< Maximum number of terminal records retained before eviction; `>= 1`,
    ///< validated by the constructor.
    const std::size_t m_max_retained;

    ///< Guards every access to @ref m_next_id and @ref m_records. `mutable` so
    ///< the `const` read methods (`get`, `list`, `active_count`) can lock it.
    mutable std::mutex m_mutex;

    ///< Next sequential id number to allocate; the `<n>` in `"j-<n>"`. Starts
    ///< at 1 and only increases.
    std::uint64_t m_next_id = 1;

    ///< The record table in creation order (front = oldest). A deque so
    ///< oldest-first eviction pops the front cheaply.
    std::deque<JobRecord> m_records;
};

}  // namespace url_shortener::commands
