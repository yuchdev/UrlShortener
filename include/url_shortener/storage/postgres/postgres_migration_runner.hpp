#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include "url_shortener/storage/errors/repo_error.hpp"

namespace soci
{
class session;
}

/**
 * @brief Applies deterministic, versioned PostgreSQL schema migrations from
 * a directory of numbered SQL files. Files named `*.up.sql` are applied in
 * ascending lexicographic order (e.g. `001_create_links.up.sql` before
 * `002_add_index.up.sql`); files named `*.down.sql` are applied in
 * descending order (most-recent first) for teardown. Each file is executed
 * as one SOCI statement with no wrapping transaction — if a mid-sequence
 * migration fails, earlier migrations have already been committed. The
 * directory is scanned lazily on each call; there is no internal state
 * beyond `migrations_dir_`.
 */
class PostgresMigrationRunner
{
public:
    /**
     * @brief Stores the directory path; does not scan or open any files.
     * @param migrations_dir Path to the directory containing `*.up.sql`
     * and `*.down.sql` migration files.
     */
    explicit PostgresMigrationRunner(std::filesystem::path migrations_dir);

    /**
     * @brief Applies all `*.up.sql` migration files in ascending
     * lexicographic order against the given session.
     * @param session Open SOCI session for the target PostgreSQL database;
     * the caller owns the session's lifetime.
     * @param[out] error If non-null and the call fails, receives
     * `RepoError::permanent_failure` (any SOCI exception during file
     * read or execution); set to `RepoError::none` on success.
     * @return `true` if all files executed without throwing; `false` if any
     * file read or SQL execution raised an exception. Migrations applied
     * before the failure are not rolled back.
     */
    bool ApplyUp(soci::session& session, RepoError* error) const;

    /**
     * @brief Applies all `*.down.sql` migration files in descending
     * lexicographic order (reverse of `ApplyUp` order) against the given
     * session. Intended for schema teardown in tests.
     * @param session Open SOCI session for the target PostgreSQL database.
     * @param[out] error If non-null and the call fails, receives
     * `RepoError::permanent_failure`; set to `RepoError::none` on success.
     * @return `true` if all files executed without throwing; `false`
     * otherwise. Migrations applied before the failure are not rolled back.
     */
    bool ApplyDownToBase(soci::session& session, RepoError* error) const;

    /**
     * @brief Returns paths to all `*.up.sql` files in `migrations_dir_`,
     * sorted in ascending lexicographic order (the apply order).
     * @return Sorted list of paths; empty if `migrations_dir_` does not
     * exist or contains no matching files.
     */
    std::vector<std::filesystem::path> OrderedUpMigrations() const;

    /**
     * @brief Returns paths to all `*.down.sql` files in `migrations_dir_`,
     * sorted in descending lexicographic order (the rollback order).
     * @return Sorted list of paths; empty if `migrations_dir_` does not
     * exist or contains no matching files.
     */
    std::vector<std::filesystem::path> OrderedDownMigrations() const;

private:
    ///< Directory scanned for `*.up.sql` and `*.down.sql` files on each
    ///< call to `ApplyUp`, `ApplyDownToBase`, `OrderedUpMigrations`, or
    ///< `OrderedDownMigrations`.
    std::filesystem::path migrations_dir_;
};
