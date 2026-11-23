#pragma once

#include "url_shortener/storage/postgres/postgres_error_mapper.hpp"
#include "url_shortener/storage/sql/i_sql_session_factory.hpp"
#include "url_shortener/storage/sql/sql_connection_config.hpp"

/**
 * @brief `ISqlSessionFactory` implementation that opens PostgreSQL sessions
 * via SOCI. Each `Create` call opens a new `soci::postgresql` connection
 * with `connect_timeout` appended to the DSN and `SET statement_timeout`
 * applied immediately after connect. Transient failures are retried up to
 * `config.max_retries` times for read and bootstrap operations (not for
 * writes, to avoid duplicate inserts). All SQLite-specific fields
 * (`busy_timeout`) in `SqlConnectionConfig` are ignored.
 */
class PostgresSessionFactory final : public ISqlSessionFactory
{
public:
    /**
     * @brief Stores the configuration; does not open a connection.
     * @param config Connection settings; `config.backend` must be
     * `SqlBackendKind::postgres` and `config.dsn` must be non-empty, or
     * `Create` will return `nullptr` with `RepoError::invalid_argument`.
     * Never store credentials in source or comments; pass the DSN via
     * environment variable or config file at runtime.
     */
    explicit PostgresSessionFactory(SqlConnectionConfig config);

    /**
     * @brief Opens a new PostgreSQL session and applies `statement_timeout`.
     * @param[out] error If non-null and the call fails, receives
     * `RepoError::invalid_argument` if `config_.backend` is not `postgres`
     * or `config_.dsn` is empty, or the `PostgresErrorMapper`-mapped SOCI
     * error on a connection failure; left untouched on success.
     * @return A new `ISqlSession` backed by a live PostgreSQL connection
     * with `statement_timeout` already set; `nullptr` on failure.
     */
    std::unique_ptr<ISqlSession> Create(RepoError* error) const override;

private:
    ///< Connection settings; `dsn` is a libpq keyword/value or URI string.
    ///< Copied by value.
    SqlConnectionConfig config_;

    ///< Maps SOCI/PostgreSQL exception messages to `RepoError` codes;
    ///< shared across all sessions created by this factory.
    PostgresErrorMapper error_mapper_;
};
