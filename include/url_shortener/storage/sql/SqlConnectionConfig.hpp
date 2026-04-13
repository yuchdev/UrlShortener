#pragma once

#include <chrono>
#include <string>

#include "url_shortener/storage/sql/SqlBackendKind.hpp"

/**
 * @brief Backend-neutral SQL connection settings for metadata repositories.
 *
 * Only settings that are currently honored by the available SQL storage
 * implementation are exposed here to avoid configuration drift.
 */
struct SqlConnectionConfig {
    SqlBackendKind backend = SqlBackendKind::sqlite;
    std::string dsn;
    std::chrono::milliseconds busy_timeout{2500};
};
