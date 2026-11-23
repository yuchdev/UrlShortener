#pragma once

#include <exception>

#include "url_shortener/storage/errors/repo_error.hpp"

/**
 * @brief Maps SQLite/SOCI exception messages to `RepoError` codes for use
 * by `SqliteSessionFactory` and the internal `SqliteSession` implementation.
 * The mapping is text-based (scanning `ex.what()`) because SOCI's SQLite
 * backend does not expose typed error codes directly.
 */
class SqliteErrorMapper
{
public:
    /**
     * @brief Classifies a SOCI or SQLite exception as a `RepoError` code.
     *
     * Mapping rules (applied in order):
     * - Message contains `"UNIQUE"` or `"unique"` →
     *   `RepoError::already_exists`.
     * - Message contains `"database is locked"` or `"busy"` →
     *   `RepoError::transient_failure`.
     * - Message contains `"unable to open"` or `"no such"` →
     *   `RepoError::permanent_failure`.
     * - Any other message → `RepoError::permanent_failure`.
     *
     * @param ex Exception to classify; `ex.what()` is inspected.
     * @return The closest matching `RepoError` code.
     */
    RepoError MapException(const std::exception& ex) const;
};
