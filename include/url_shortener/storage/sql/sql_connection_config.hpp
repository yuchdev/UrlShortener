#pragma once

#include <chrono>
#include <optional>
#include <string>

#include "url_shortener/storage/sql/sql_backend_kind.hpp"

/**
 * @brief Backend-neutral SQL connection settings consumed by
 * `SqliteSessionFactory` and `PostgresSessionFactory`. Fields that are only
 * meaningful for one backend are ignored by the other; see the per-field
 * comments below.
 */
struct SqlConnectionConfig
{
    ///< Which SOCI backend to open; must match the factory type being
    ///< constructed (e.g. `SqlBackendKind::sqlite` for
    ///< `SqliteSessionFactory`).
    SqlBackendKind backend = SqlBackendKind::sqlite;

    ///< Backend connection string. For SQLite: a file path or `":memory:"`.
    ///< For PostgreSQL: a libpq keyword/value connection string. Never
    ///< embed credentials here in source or comments.
    std::string dsn;

    // SQLite tuning.

    ///< SQLite only. Passed to `PRAGMA busy_timeout`; controls how long
    ///< SQLite waits before returning `SQLITE_BUSY` on a locked database.
    std::chrono::milliseconds busy_timeout {2500};

    // PostgreSQL tuning.

    ///< PostgreSQL only. Appended as `connect_timeout=<seconds>` to the
    ///< DSN; minimum effective value is 1 second (milliseconds are truncated
    ///< to whole seconds).
    std::chrono::milliseconds connect_timeout {3000};

    ///< PostgreSQL only. Applied via `SET statement_timeout = <ms>` after
    ///< the session opens; causes long-running statements to be cancelled by
    ///< the server.
    std::chrono::milliseconds statement_timeout {5000};

    ///< PostgreSQL only. If set, appended as `application_name=<value>` to
    ///< the connection string so the server can identify this client.
    std::optional<std::string> application_name;

    // Retry budget for transient, idempotent operations.

    ///< PostgreSQL only. Number of additional attempts after the first on a
    ///< `RepoError::transient_failure`; 0 means no retries. Only operations
    ///< flagged as retryable by `PostgresSession::RunWithRetry` are retried
    ///< (reads and bootstrap DDL); writes are not retried to avoid duplicate
    ///< inserts.
    std::size_t max_retries = 0;
};
