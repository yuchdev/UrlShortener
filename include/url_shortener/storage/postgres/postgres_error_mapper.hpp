#pragma once

#include <exception>

#include "url_shortener/storage/errors/repo_error.hpp"

/**
 * @brief Maps PostgreSQL/SOCI exception messages to `RepoError` codes for
 * use by `PostgresSessionFactory` and the internal `PostgresSession`
 * implementation. The mapping inspects `ex.what()` for PostgreSQL SQLSTATE
 * codes and known diagnostic substrings.
 */
class PostgresErrorMapper
{
public:
    /**
     * @brief Classifies a SOCI or PostgreSQL exception as a `RepoError`
     * code.
     *
     * Mapping rules (applied in order):
     * - SQLSTATE `23505` or message contains `"unique"` →
     *   `RepoError::already_exists`.
     * - SQLSTATE `42P01` or `"undefined table"` →
     *   `RepoError::permanent_failure`.
     * - SQLSTATE `40001`, `40P01` (serialization / deadlock), or
     *   `"connection"` / `"could not connect"` →
     *   `RepoError::transient_failure`.
     * - SQLSTATE `57014`, `"statement timeout"`, `"timeout"`, or
     *   `"canceling statement"` → `RepoError::transient_failure`.
     * - SQLSTATE `28P01` or `"password authentication failed"` →
     *   `RepoError::permanent_failure`.
     * - Any other message → `RepoError::permanent_failure`.
     *
     * @param ex Exception to classify; `ex.what()` is inspected.
     * @return The closest matching `RepoError` code.
     */
    RepoError MapException(const std::exception& ex) const;
};
