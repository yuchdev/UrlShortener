/**
 * @file jobs_table_source.cpp
 * @brief The `jobs` logical `db.*` table over the process-wide @ref
 * url_shortener::commands::JobRunner (subtask 05.0/21).
 *
 * The column list is a literal constant here (contract C6 / threat model T-94):
 * only `id, command, state, done, total, error_code, created_at, finished_at`
 * are ever emitted. A reviewer can see from this file alone that no job
 * argument, `args_digest`, `result_json` or failure *message* can be returned --
 * those @ref JobRecord fields are never read. Every cell value is
 * `jsonString`-escaped and no Beast, SOCI or driver header appears in the
 * matching public header (`core/utils.hpp`, which pulls in Beast, is included
 * only from this `.cpp`).
 */
#include <url_shortener/commands/jobs_table_source.hpp>

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <limits>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <url_shortener/app/table_source.hpp>
#include <url_shortener/commands/job_runner.hpp>
#include <url_shortener/commands/job_types.hpp>
#include <url_shortener/core/utils.hpp>

namespace url_shortener
{
namespace commands
{

namespace
{

using app::Result;
using app::TableColumn;
using app::TableQuery;
using app::TableRowsPage;

/**
 * @brief Builds one @ref app::TableColumn.
 * @param name Column name (also the row-object key).
 * @param type Catalog type token (`"string"`, `"int"`, `"timestamp"`).
 * @param sortable Whether `db.rows` may order by this column.
 * @param searchable Whether this column participates in the `q` filter.
 * @return The populated column.
 */
TableColumn make_column(std::string name,
                        std::string type,
                        bool sortable,
                        bool searchable)
{
    TableColumn column;
    column.name = std::move(name);
    column.type = std::move(type);
    column.sortable = sortable;
    column.searchable = searchable;
    return column;
}

/**
 * @brief Lower-cases an ASCII string for the case-insensitive `q` match.
 * @param value Input bytes; non-ASCII bytes pass through unchanged.
 * @return @p value with every ASCII upper-case letter folded to lower case.
 */
std::string to_lower_ascii(std::string value)
{
    for (char& ch : value) {
        ch = static_cast<char>(
            std::tolower(static_cast<unsigned char>(ch)));
    }
    return value;
}

/**
 * @brief Tests whether a record matches a case-insensitive free-text filter on
 * its `id` or `command`.
 *
 * Mirrors the two searchable columns (`id`, `command`); no other field is ever
 * consulted, so a `q` can never probe `args`, `args_digest`, `result_json` or a
 * failure message (threat model T-94).
 *
 * @param record The job record to test.
 * @param needle_lower The already lower-cased search term.
 * @return `true` when @p needle_lower is a substring of either the lower-cased
 * `id` or the lower-cased `command`.
 */
bool matches_query(const JobRecord& record, const std::string& needle_lower)
{
    if (to_lower_ascii(record.id).find(needle_lower) != std::string::npos) {
        return true;
    }
    return to_lower_ascii(record.command).find(needle_lower)
        != std::string::npos;
}

/**
 * @brief Finish-time sort key: an absent `finished_at` sorts before any real
 * instant.
 * @param record The job record whose finish time is the key.
 * @return `record.finished_at` when present, otherwise
 * `system_clock::time_point::min()` (a non-terminal job orders first ascending).
 */
std::chrono::system_clock::time_point finished_key(const JobRecord& record)
{
    if (record.finished_at.has_value()) {
        return *record.finished_at;
    }
    return std::chrono::system_clock::time_point::min();
}

/**
 * @brief Ascending `<` comparison of two records by a sortable column name.
 *
 * Only the sortable columns reach here (`id`, `command`, `state`, `created_at`,
 * `finished_at`); the handler has already rejected any other `sort` token, and
 * the mapping falls back to `created_at` so an unexpected value can never order
 * by an unintended field (threat model T-51).
 *
 * @param column The admissible sort column name.
 * @param a Left record.
 * @param b Right record.
 * @return `true` when @p a orders strictly before @p b ascending by @p column.
 */
bool less_by_column(const std::string& column,
                    const JobRecord& a,
                    const JobRecord& b)
{
    if (column == "id") {
        return a.id < b.id;
    }
    if (column == "command") {
        return a.command < b.command;
    }
    if (column == "state") {
        return job_state_name(a.state) < job_state_name(b.state);
    }
    if (column == "finished_at") {
        return finished_key(a) < finished_key(b);
    }
    return a.created_at < b.created_at;
}

/**
 * @brief Serializes one job record as the flat `jobs` row object.
 *
 * Keys equal the fixed column names and every string cell is `jsonString`-
 * escaped. The only failure detail emitted is the error *code*; the failure
 * *message*, `args`, `args_digest` and `result_json` are never read (threat
 * model T-94, contract C6). An absent `error_code`/`finished_at` is emitted as
 * an empty string.
 *
 * @param record The job record to serialize.
 * @return The single-line JSON object for this row.
 */
std::string serialize_job_row(const JobRecord& record)
{
    std::ostringstream os;
    os << '{';
    os << "\"id\":" << jsonString(record.id);
    os << ",\"command\":" << jsonString(record.command);
    os << ",\"state\":"
       << jsonString(std::string(job_state_name(record.state)));
    os << ",\"done\":" << record.progress.done;
    os << ",\"total\":" << record.progress.total;
    os << ",\"error_code\":";
    if (record.failure.has_value()) {
        os << jsonString(record.failure->code);  // code only, never message.
    }
    else {
        os << jsonString(std::string {});
    }
    os << ",\"created_at\":" << jsonString(formatTimestamp(record.created_at));
    os << ",\"finished_at\":";
    if (record.finished_at.has_value()) {
        os << jsonString(formatTimestamp(*record.finished_at));
    }
    else {
        os << jsonString(std::string {});
    }
    os << '}';
    return os.str();
}

/**
 * @brief The `jobs` logical `db.*` table: job *metadata only* over a borrowed
 * process-wide @ref JobRunner (milestone C6 / decision Q7, threat model T-94).
 *
 * The column list is a literal constant built once in the constructor, never
 * derived from a record, so the columns that exist are exactly the columns
 * `db.rows` may expose. Rows are flat scalar objects whose keys equal the column
 * names; `args`, `args_digest`, `result_json` and any failure *message* are
 * never projected. `writable()` is `false` (there is no typed write command for
 * jobs and no write path through `db.*`).
 *
 * Thread-safety follows the backing @ref JobRunner, whose `list` takes a
 * snapshot under its own lock; the source holds no mutable state after
 * construction.
 */
class JobsTableSource final : public app::ITableSource
{
public:
    /**
     * @brief Builds the source over a runner.
     *
     * Ownership: @p runner is borrowed, not owned. The reference is captured for
     * this source's whole lifetime, so @p runner must outlive this object (the
     * process-wide runner wired in at composition time, subtask 20, does).
     *
     * @param runner The job runner every count and page is read from.
     */
    explicit JobsTableSource(const JobRunner& runner) : m_runner_(runner)
    {
        // Literal, fixed column list (contract C6 / T-94): no args, args_digest,
        // result or failure message column can ever exist here.
        m_columns_ = {
            make_column("id", "string", true, true),
            make_column("command", "string", true, true),
            make_column("state", "string", true, false),
            make_column("done", "int", false, false),
            make_column("total", "int", false, false),
            make_column("error_code", "string", false, false),
            make_column("created_at", "timestamp", true, false),
            make_column("finished_at", "timestamp", true, false),
        };
    }

    /**
     * @brief The logical table name.
     * @return The constant `"jobs"`.
     */
    const std::string& name() const override
    {
        static const std::string k_name = "jobs";
        return k_name;
    }

    /**
     * @brief Whether a typed write command exists for this table.
     * @return Always `false`; jobs are created by submitting commands, not
     * through a `job.*` mutation, and there is no write path through `db.*`.
     */
    bool writable() const override { return false; }

    /**
     * @brief The fixed `jobs` column list, in display order.
     * @return `id, command, state, done, total, error_code, created_at,
     * finished_at`; `id`/`command`/`state`/`created_at`/`finished_at` are
     * sortable, `id`/`command` are searchable.
     */
    const std::vector<TableColumn>& columns() const override
    {
        return m_columns_;
    }

    /**
     * @brief The total number of retained job records.
     * @return `Result<std::size_t>` holding `runner.list(nullopt, 0, 1).total`
     * (all retained records, including active jobs); always `ok` -- the runner's
     * in-memory list never fails.
     */
    Result<std::size_t> row_count() const override
    {
        Result<std::size_t> result;
        result.value = m_runner_.list(std::nullopt, 0, 1).total;
        return result;
    }

    /**
     * @brief Returns a filtered, counted page of job rows.
     *
     * Snapshots every retained record (including active jobs, so the snapshot
     * may exceed `max_retained_jobs`), filters by the case-insensitive `q` over
     * `id`/`command`, sorts by the requested sortable column (default
     * `created_at` descending), then applies the `offset`/`limit` window.
     *
     * @param query A bounded, pre-validated browse query; its `sort` (when set)
     * is guaranteed to name one of this source's sortable columns.
     * @return `Result<TableRowsPage>` whose `rows_json` holds one flat,
     * `jsonString`-escaped metadata object per job (never args, args_digest,
     * result or failure message) and whose `total` is the filtered count before
     * the window; always `ok`.
     */
    Result<TableRowsPage> rows(const TableQuery& query) const override
    {
        // Snapshot ALL retained records in one locked JobRunner::list call.
        // SIZE_MAX as the limit returns every record (newest first); active
        // jobs are preserved, so this count can exceed max_retained_jobs.
        JobPage snapshot = m_runner_.list(
            std::nullopt, 0, std::numeric_limits<std::size_t>::max());
        std::vector<JobRecord> records = std::move(snapshot.items);

        // Filter by the free-text q over id/command (case-insensitive).
        if (query.q.has_value() && !query.q->empty()) {
            const std::string needle = to_lower_ascii(*query.q);
            std::vector<JobRecord> filtered;
            filtered.reserve(records.size());
            for (JobRecord& record : records) {
                if (matches_query(record, needle)) {
                    filtered.push_back(std::move(record));
                }
            }
            records = std::move(filtered);
        }

        // Sort. The snapshot is already created_at-descending (newest first),
        // which is the default order, so only an explicit sort re-orders.
        if (query.sort.has_value()) {
            const std::string& column = *query.sort;
            const bool descending = query.descending;
            std::stable_sort(
                records.begin(), records.end(),
                [&column, descending](const JobRecord& a, const JobRecord& b)
                {
                    return descending ? less_by_column(column, b, a)
                                      : less_by_column(column, a, b);
                });
        }

        TableRowsPage page;
        page.total = records.size();

        // Apply the offset/limit window over the filtered, sorted records.
        const std::size_t begin = std::min(query.offset, records.size());
        std::size_t end = begin;
        if (query.limit <= records.size() - begin) {
            end = begin + query.limit;
        }
        else {
            end = records.size();
        }
        page.rows_json.reserve(end - begin);
        for (std::size_t i = begin; i < end; ++i) {
            page.rows_json.push_back(serialize_job_row(records[i]));
        }

        Result<TableRowsPage> result;
        result.value = std::move(page);
        return result;
    }

private:
    ///< Borrowed process-wide job runner; not owned, must outlive this source
    ///< (see the constructor's ownership note).
    const JobRunner& m_runner_;

    ///< The fixed `jobs` column list, built once in the constructor and returned
    ///< by @ref columns; never derived from a record (contract C6 / T-94).
    std::vector<TableColumn> m_columns_;
};

}  // namespace

std::shared_ptr<app::ITableSource> make_jobs_table_source(
    const JobRunner& runner)
{
    return std::make_shared<JobsTableSource>(runner);
}

}  // namespace commands
}  // namespace url_shortener
