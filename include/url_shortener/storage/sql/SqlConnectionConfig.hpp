#pragma once

#include <chrono>
#include <string>

#include "url_shortener/storage/sql/SqlBackendKind.hpp"

/**
 * @brief Backend-neutral SQL connection settings for metadata repositories.
 */
struct SqlConnectionConfig {
    SqlBackendKind backend = SqlBackendKind::sqlite;
    std::string dsn;
    std::chrono::milliseconds connect_timeout{5000};
    std::chrono::milliseconds statement_timeout{5000};
    std::chrono::milliseconds busy_timeout{2500};
    bool auto_bootstrap = true;
    unsigned int retry_attempts = 0;
};
