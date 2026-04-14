#pragma once

#include <chrono>
#include <optional>
#include <string>

#include "url_shortener/storage/sql/SqlBackendKind.hpp"

/**
 * @brief Backend-neutral SQL connection settings for metadata repositories.
 */
struct SqlConnectionConfig {
    SqlBackendKind backend = SqlBackendKind::sqlite;
    std::string dsn;

    // SQLite tuning.
    std::chrono::milliseconds busy_timeout{2500};

    // PostgreSQL tuning.
    std::chrono::milliseconds connect_timeout{3000};
    std::chrono::milliseconds statement_timeout{5000};
    std::optional<std::string> application_name;

    // Retry budget for transient, idempotent operations.
    std::size_t max_retries = 0;
};
