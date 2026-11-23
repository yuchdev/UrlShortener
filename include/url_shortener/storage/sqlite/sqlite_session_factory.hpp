#pragma once

#include "url_shortener/storage/sql/i_sql_session_factory.hpp"
#include "url_shortener/storage/sql/sql_connection_config.hpp"
#include "url_shortener/storage/sqlite/sqlite_error_mapper.hpp"

/**
 * @brief `ISqlSessionFactory` implementation that opens SQLite sessions via
 * SOCI. Each `Create` call opens a new `soci::sqlite3` connection to the
 * path in `config.dsn` and applies `PRAGMA busy_timeout` from
 * `config.busy_timeout`. All fields of `SqlConnectionConfig` specific to
 * PostgreSQL (`connect_timeout`, `statement_timeout`, `application_name`,
 * `max_retries`) are ignored.
 */
class SqliteSessionFactory final : public ISqlSessionFactory
{
public:
    /**
     * @brief Stores the configuration; does not open a connection.
     * @param config Connection settings; `config.backend` must be
     * `SqlBackendKind::sqlite` and `config.dsn` must be non-empty, or
     * `Create` will return `nullptr` with `RepoError::invalid_argument`.
     */
    explicit SqliteSessionFactory(SqlConnectionConfig config);

    /**
     * @brief Opens a new SQLite session.
     * @param[out] error If non-null and the call fails, receives
     * `RepoError::invalid_argument` if `config_.backend` is not `sqlite`
     * or `config_.dsn` is empty, or the `SqliteErrorMapper`-mapped SOCI
     * error on a connection failure; left untouched on success.
     * @return A new `ISqlSession` backed by the SQLite database at
     * `config_.dsn`, with `PRAGMA busy_timeout` already applied; `nullptr`
     * on failure.
     */
    std::unique_ptr<ISqlSession> Create(RepoError* error) const override;

private:
    ///< Connection settings; `dsn` must point to a SQLite file or be
    ///< `":memory:"`. Not owned; copied by value.
    SqlConnectionConfig config_;

    ///< Maps SOCI exception messages to `RepoError` codes; shared across
    ///< all sessions created by this factory.
    SqliteErrorMapper error_mapper_;
};
