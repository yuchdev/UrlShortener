#pragma once

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <url_shortener/commands/envelope.hpp>

namespace url_shortener::commands
{

// @ref JobState (the job lifecycle enum: queued, running, succeeded, failed,
// cancelled) is reused verbatim from `commands/envelope.hpp` rather than
// redefined here. Task 01.0 already introduced it in this namespace for the
// response envelope's accepted-job shape, so a second definition would be an
// ODR collision. That envelope header transitively includes only
// `error_codes.hpp` and `json_scan.hpp` -- neither pulls in the HTTP library or
// the core string-utility header -- so including it keeps this file within the
// 01.0 "no HTTP headers in commands/" constraint. This file deliberately does
// NOT reuse the envelope's error type; it keeps its own value-only
// @ref JobFailure (see below).

/**
 * @brief Maps a @ref JobState to its stable lower-case catalog name.
 *
 * @param state Any @ref JobState enumerator.
 * @return A view into static storage valid for the program lifetime:
 * `"queued"`, `"running"`, `"succeeded"`, `"failed"` or `"cancelled"`. An
 * out-of-range value (only producible by casting) maps to `"queued"`. Mirrors
 * `commands::job_state_name` so there is one source of truth for the names.
 */
std::string_view to_string(JobState state) noexcept;

/**
 * @brief Parses a catalog state name back to its @ref JobState, for the
 * `job.list` state filter.
 *
 * @param text Candidate state name; compared exactly, case-sensitively, against
 * each enumerator's catalog name.
 * @return The matching @ref JobState, or `std::nullopt` when @p text matches no
 * state (including the empty string and any unknown token such as `"bogus"`).
 */
std::optional<JobState> parse_job_state(std::string_view text) noexcept;

/**
 * @brief Tests whether a state is terminal (a job in it never transitions
 * again).
 *
 * @param state Any @ref JobState enumerator.
 * @return `true` for `succeeded`, `failed` and `cancelled`; `false` for
 * `queued` and `running` (the two states counted by
 * @ref JobStore::active_count).
 */
bool is_terminal(JobState state) noexcept;

/**
 * @brief Monotonic progress counters reported by a running job body.
 *
 * Both counters are plain values with no unit beyond "items of work" (e.g. CSV
 * rows for `link.import`). A fresh record starts at `{0, 0}`;
 * @ref JobStore::update_progress enforces that `done` never decreases.
 */
struct JobProgress
{
    ///< Units of work completed so far; monotonically non-decreasing across
    ///< successive @ref JobStore::update_progress calls on the same job.
    std::uint64_t done = 0;

    ///< Total units of work expected, as known when progress was reported; may
    ///< be `0` when the total is not yet known.
    std::uint64_t total = 0;
};

/**
 * @brief Value-only failure detail stored on a job that ended in `failed`.
 *
 * Deliberately distinct from the envelope's `CommandError` so this header stays
 * independent of the envelope's error machinery: subtask 04/05 convert between
 * this and the wire error when a `job.get` response is built. Carries no
 * argument values (milestone contract C6); `message` is a human-readable
 * summary, never a raw caller argument.
 */
struct JobFailure
{
    ///< Catalog error-code string (e.g. `"invalid_field"`), as produced by the
    ///< command handler that failed the job.
    std::string code;

    ///< Human-readable failure summary; carries no raw argument values (C6).
    std::string message;
};

/**
 * @brief One in-memory job record: the full lifecycle state of a single
 * submitted job.
 *
 * Owned by @ref JobStore. The record stores an @ref args_digest (threat model
 * T-93 keeps ids as correlation-only) and never the raw arguments (milestone
 * contract C6, threat model T-86). `result_json` is opaque pre-serialized text
 * produced by the command handler, never re-parsed here (contract C9); threat
 * model T-109 bounds how many such records (and therefore how much inline
 * result text) are retained via @ref JobStore's eviction.
 */
struct JobRecord
{
    ///< Sequential correlation handle of the form `"j-<n>"`, assigned by
    ///< @ref JobStore::create. A handle for lookup only, never an authorization
    ///< capability (threat model T-93): visibility is decided by the caller's
    ///< role, not by knowledge of this id.
    std::string id;

    ///< Dotted command name that produced the job (e.g. `"link.import"`); a
    ///< validated name, never raw caller input.
    std::string command;

    ///< First 16 hex characters of `SHA-256(args_json)`; a correlation digest,
    ///< never the raw arguments (milestone contract C6, threat model T-86).
    std::string args_digest;

    ///< Current lifecycle state; `queued` on a freshly created record.
    JobState state = JobState::queued;

    ///< Latest progress counters reported by the body; `{0, 0}` until the body
    ///< first reports progress.
    JobProgress progress;

    ///< Opaque pre-serialized result object text (contract C9), present only on
    ///< a terminal record that produced one; `std::nullopt` otherwise. Retained
    ///< memory is bounded by @ref JobStore eviction (threat model T-109).
    std::optional<std::string> result_json;

    ///< Failure detail; present only when @ref state is `failed`,
    ///< `std::nullopt` otherwise.
    std::optional<JobFailure> failure;

    ///< Wall-clock instant the record was created, from the injected
    ///< @ref IClock; also the sort key for @ref JobStore::list (newest first)
    ///< and eviction order.
    std::chrono::system_clock::time_point created_at;

    ///< Wall-clock instant the job first entered `running`; `std::nullopt`
    ///< while still `queued` or if cancelled before it ran.
    std::optional<std::chrono::system_clock::time_point> started_at;

    ///< Wall-clock instant the job reached a terminal state; `std::nullopt`
    ///< while non-terminal.
    std::optional<std::chrono::system_clock::time_point> finished_at;
};

/**
 * @brief One page of job records returned by @ref JobStore::list.
 *
 * `items` is the clamped slice requested by `offset`/`limit`; `total` is the
 * full count matching the filter before the slice, so a caller can paginate
 * without re-listing.
 */
struct JobPage
{
    ///< The requested slice of matching records, newest first; empty when the
    ///< requested `limit` is `0` or `offset` is at/beyond @ref total.
    std::vector<JobRecord> items;

    ///< Total number of records matching the list filter before `offset`/
    ///< `limit` were applied.
    std::size_t total = 0;
};

/**
 * @brief The lightweight, metadata-only projection of a @ref JobRecord returned
 * by @ref JobStore::list_summaries for the list/browse surfaces.
 *
 * Carries exactly the eight metadata fields the paging callers read (`job.list`
 * items and the `jobs` `db.*` table) and deliberately **omits** the two heavy or
 * sensitive @ref JobRecord fields: the (up to ~1 MiB) opaque `result_json` and
 * the `args_digest`. This exists for resource safety (CWE-400 / CWE-770, threat
 * model T-109): a single list/browse request can touch every retained record
 * (up to the retention cap, 256 by default), so copying each record's
 * `result_json` under the store mutex could transiently copy hundreds of MiB and
 * stall workers on the lock. Copying only this struct keeps the locked copy
 * bounded to small scalar/short-string fields.
 *
 * The absence of `result_json` is a compile-time guarantee this projection can
 * never amplify a page into a multi-hundred-MiB response; `args_digest` is
 * dropped because no list/browse column exposes it (contract C6, threat model
 * T-93/T-94). `failure` is retained because `job.list`'s item shape emits a
 * short, value-free `error {code, message}` for a failed/cancelled job; the
 * `jobs` table reads only its `code`.
 */
struct JobSummary
{
    ///< Sequential correlation handle (`"j-<n>"`); copied verbatim from the
    ///< record. A lookup handle only, never an authorization capability (threat
    ///< model T-93).
    std::string id;

    ///< Dotted command name that produced the job (e.g. `"link.import"`); a
    ///< validated name, never raw caller input.
    std::string command;

    ///< Current lifecycle state; `queued` on a freshly created record.
    JobState state = JobState::queued;

    ///< Latest progress counters reported by the body; `{0, 0}` until the body
    ///< first reports progress.
    JobProgress progress;

    ///< Failure detail (short, value-free `{code, message}`); present only when
    ///< @ref state is `failed`, `std::nullopt` otherwise. Never carries an
    ///< argument value (contract C6). Deliberately *not* `result_json`, which is
    ///< never projected into a summary (threat model T-109).
    std::optional<JobFailure> failure;

    ///< Wall-clock instant the record was created; also the newest-first sort
    ///< key of the page.
    std::chrono::system_clock::time_point created_at;

    ///< Wall-clock instant the job first entered `running`; `std::nullopt`
    ///< while still `queued` or if cancelled before it ran.
    std::optional<std::chrono::system_clock::time_point> started_at;

    ///< Wall-clock instant the job reached a terminal state; `std::nullopt`
    ///< while non-terminal.
    std::optional<std::chrono::system_clock::time_point> finished_at;
};

/**
 * @brief One page of @ref JobSummary records returned by
 * @ref JobStore::list_summaries.
 *
 * The metadata-only counterpart of @ref JobPage: `items` is the clamped,
 * newest-first slice requested by `offset`/`limit` and `total` is the full count
 * matching the filter before the slice. Identical paging semantics to
 * @ref JobPage, so a caller can switch from `list` to `list_summaries` without
 * changing its paging logic, but each item omits `result_json`/`args_digest`
 * (threat model T-109).
 */
struct JobSummaryPage
{
    ///< The requested slice of matching summaries, newest first; empty when the
    ///< requested `limit` is `0` or `offset` is at/beyond @ref total.
    std::vector<JobSummary> items;

    ///< Total number of records matching the list filter before `offset`/
    ///< `limit` were applied.
    std::size_t total = 0;
};

}  // namespace url_shortener::commands
