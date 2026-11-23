#pragma once

#include <ostream>

/**
 * @brief Discriminator tag used by `SqlConnectionConfig` and the session
 * factories to select the correct SOCI backend at runtime.
 */
enum class SqlBackendKind
{
    ///< SQLite backend via `soci::sqlite3`; `dsn` is a file path or
    ///< `":memory:"`.
    sqlite,

    ///< PostgreSQL backend via `soci::postgresql`; `dsn` is a libpq
    ///< keyword/value connection string.
    postgres,
};

/**
 * @brief Stream formatter for `SqlBackendKind`.
 * @param os Stream to write to.
 * @param kind Value to format.
 * @return `os`, with `"sqlite"` or `"postgres"` written to it.
 */
inline std::ostream& operator<<(std::ostream& os, SqlBackendKind kind)
{
    switch (kind) {
        case SqlBackendKind::sqlite:
            return os << "sqlite";
        case SqlBackendKind::postgres:
            return os << "postgres";
        default:
            return os << "unknown";
    }
}
