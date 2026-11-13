#pragma once

#include <mutex>
#include <string>

#include <soci/session.h>

#include "url_shortener/analytics/i_click_event_repository.hpp"

namespace url_shortener::storage::sql
{

/**
 * @brief `IClickEventRepository` implementation backed by SQLite or
 * PostgreSQL via SOCI. The backend is selected automatically from the DSN
 * prefix: URIs starting with `postgresql://` or `postgres://`, or strings
 * containing `host=` / `dbname=`, are opened as PostgreSQL; everything else
 * is treated as SQLite (a file path or `":memory:"`).
 *
 * The `click_events` schema (table + three composite indexes) is created
 * unconditionally in the constructor, not deferred to first use.
 *
 * Thread-safety: all public methods are serialized through `mu_`; it is safe
 * to call them concurrently from multiple threads.
 */
class SqlClickEventRepository final : public analytics::IClickEventRepository
{
public:
    /**
     * @brief Opens the SOCI session and bootstraps the `click_events`
     * schema.
     * @param dsn For SQLite: a filesystem path or `":memory:"`. For
     * PostgreSQL: a libpq keyword/value or URI connection string; never
     * embed credentials in comments or source.
     * @throws `soci::soci_error` (or derived) if the connection or DDL
     * fails.
     */
    explicit SqlClickEventRepository(const std::string& dsn = ":memory:");

    /**
     * @brief Persists a batch of click events. Duplicate `event_id` values
     * are silently ignored (`INSERT OR IGNORE` for SQLite,
     * `ON CONFLICT DO NOTHING` for PostgreSQL).
     * @param events Events to persist; may be empty (no-op).
     * @param[out] error If non-null and the call fails, receives
     * `AnalyticsErrorCode::repository` with the SOCI exception message;
     * left untouched on success.
     * @return `true` if all rows were processed without throwing; `false` if
     * any SOCI exception occurred.
     */
    bool InsertBatch(const std::vector<analytics::ClickEvent>& events,
                     analytics::AnalyticsError* error = nullptr) override;

    /**
     * @brief Computes aggregate statistics for events matching `query`.
     * @param query Time range, optional slug filter, and optional time
     * bucket. Time bucketing is only supported for the SQLite backend;
     * `query.bucket` is ignored when connected to PostgreSQL.
     * @param[out] stats Receives the aggregate result on success; must be
     * non-null. Set to a zero-initialized `AggregateStats` before computing
     * so callers receive partial results only on a thrown exception.
     * @param[out] error If non-null and the call fails, receives
     * `AnalyticsErrorCode::repository`; left untouched on success.
     * @return `true` if the query completed without throwing; `false` if
     * `stats` is null or any SOCI exception occurred.
     */
    bool GetAggregateStats(const analytics::AggregateQuery& query,
                           analytics::AggregateStats* stats,
                           analytics::AnalyticsError* error = nullptr) override;

    /**
     * @brief Deletes events whose `occurred_at` is strictly before
     * `cutoff`.
     * @param cutoff Deletion boundary; events with `occurred_at < cutoff`
     * are removed (the cutoff timestamp itself is retained).
     * @param[out] error If non-null and the call fails, receives
     * `AnalyticsErrorCode::repository`; left untouched on success.
     * @return `true` if the DELETE completed without throwing (including
     * deleting zero rows); `false` on any SOCI exception.
     */
    bool DeleteOlderThan(analytics::Timestamp cutoff,
                         analytics::AnalyticsError* error = nullptr) override;

private:
    /**
     * @brief Creates the `click_events` table and composite indexes.
     * Called unconditionally from the constructor. Postgres uses
     * `TIMESTAMPTZ` for `occurred_at`; SQLite uses `INTEGER` (epoch
     * seconds).
     * @param is_postgres `true` to emit PostgreSQL DDL, `false` for SQLite.
     */
    void Bootstrap(bool is_postgres);

    ///< Single SOCI session for this repository instance; all operations
    ///< are serialized through `mu_`.
    soci::session session_;

    ///< `true` when `session_` is connected to a PostgreSQL backend; `false`
    ///< for SQLite. Set in the constructor by `is_postgres_dsn()` heuristic.
    bool is_postgres_ = false;

    ///< Guards `session_` for all public methods so the repository is safe
    ///< to call concurrently.
    mutable std::mutex mu_;
};

}  // namespace url_shortener::storage::sql
