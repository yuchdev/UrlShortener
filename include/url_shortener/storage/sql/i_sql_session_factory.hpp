#pragma once

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "url_shortener/storage/errors/repo_error.hpp"

/**
 * @brief Backend-neutral row model passed between an `ISqlSession` and the
 * `SqlMetadataRepository` layer. All fields are optional to represent SQL
 * NULL columns and to allow partial results during mapping.
 *
 * Timestamps are Unix epoch seconds (UTC) packed into `long long`;
 * `SqlMetadataRowMapper` converts to and from
 * `std::chrono::system_clock::time_point`. `is_active` is `int` (0/1)
 * rather than `bool` because SOCI maps SQLite `INTEGER` and PostgreSQL
 * `BOOLEAN` to `int` in this layer.
 */
struct SqlRow
{
    ///< Stable link identifier; stored inside `attributes_json` for SQLite
    ///< and as a separate `VARCHAR` column for PostgreSQL.
    std::optional<std::string> id;

    ///< URL slug; primary key in the `links` table.
    std::optional<std::string> short_code;

    ///< Redirect destination URL.
    std::optional<std::string> target_url;

    ///< Link creation time, Unix epoch seconds (UTC).
    std::optional<long long> created_at;

    ///< Last modification time, Unix epoch seconds (UTC).
    std::optional<long long> updated_at;

    ///< Expiry time, Unix epoch seconds (UTC); `std::nullopt` means no
    ///< expiry.
    std::optional<long long> expires_at;

    ///< Liveness flag as `int` (0 = inactive, 1 = active) so it maps to
    ///< both SQLite `INTEGER` and PostgreSQL `BOOLEAN` without casting.
    std::optional<int> is_active;

    ///< Optional owner identifier; `std::nullopt` for un-owned links.
    std::optional<std::string> owner_id;

    ///< JSON blob encoding the link's stable `id` and `attributes` map (see
    ///< `SqlMetadataRowMapper`). `std::nullopt` when the backend stores `id`
    ///< as a separate column and no attributes are present.
    std::optional<std::string> attributes_json;
};

/**
 * @brief Backend-neutral SQL session for link metadata CRUD, used exclusively
 * by `SqlMetadataRepository`. Each instance wraps exactly one SOCI session
 * (or equivalent backend connection) and is not safe to call concurrently
 * from multiple threads. `SqlMetadataRepository` creates a new session per
 * operation via `ISqlSessionFactory::Create`, so concurrent repository
 * operations each own a distinct session.
 *
 * All methods accept the SQL string from the matching `SqlDialect` method;
 * implementations do not embed SQL text. Expected failures are reported via
 * the `bool` / `RepoError*` out-param convention; SOCI exceptions are caught
 * and mapped to `RepoError` by the backend-specific error mapper.
 */
class ISqlSession
{
public:
    /**
     * @brief Virtual destructor; implementations may be destroyed through a
     * base `ISqlSession*`.
     */
    virtual ~ISqlSession() = default;

    /**
     * @brief Executes the schema bootstrap DDL (CREATE TABLE IF NOT EXISTS).
     * @param sql DDL from `SqlDialect::BootstrapSchemaSql`; must be a
     * complete, self-contained SQL statement.
     * @param[out] error If non-null and the call fails, receives the mapped
     * error (typically `RepoError::permanent_failure`); set to
     * `RepoError::none` on success.
     * @return `true` if the DDL executed without error; `false` if the
     * backend threw (e.g. permission denied, locked database).
     */
    virtual bool Bootstrap(const std::string& sql, RepoError* error) = 0;

    /**
     * @brief Inserts a new link row using the dialect's INSERT SQL.
     * @param sql INSERT statement from `SqlDialect::InsertSql`.
     * @param row Column values to bind; `short_code`, `target_url`,
     * `created_at`, `updated_at`, and `is_active` must be populated.
     * @param[out] error If non-null and the call fails, receives
     * `RepoError::already_exists` on a unique-constraint violation,
     * `RepoError::transient_failure` on a locked/busy backend, or
     * `RepoError::permanent_failure` otherwise; set to `RepoError::none`
     * on success.
     * @return `true` if the row was inserted; `false` on any SOCI exception.
     */
    virtual bool InsertLink(const std::string& sql,
                            const SqlRow& row,
                            RepoError* error) = 0;

    /**
     * @brief Looks up a link row by its slug.
     * @param sql SELECT statement from `SqlDialect::SelectByShortCodeSql`.
     * @param short_code Slug to look up.
     * @param[out] error If non-null, set to `RepoError::not_found` when no
     * row matches, the mapped exception error on a SOCI failure, or
     * `RepoError::none` on a successful read.
     * @return The matching `SqlRow` on success; `std::nullopt` if no row was
     * found or if a SOCI exception occurred. Callers distinguish the two by
     * checking `error`.
     */
    virtual std::optional<SqlRow> SelectByShortCode(
        const std::string& sql,
        const std::string& short_code,
        RepoError* error) = 0;

    /**
     * @brief Updates an existing link row using the dialect's UPDATE SQL.
     * @param sql UPDATE statement from `SqlDialect::UpdateSql`.
     * @param row New column values; `short_code` identifies the row to
     * update.
     * @param[out] error If non-null, set to `RepoError::not_found` if no
     * row matched the slug, the mapped exception error on a SOCI failure,
     * or `RepoError::none` on success.
     * @return `true` if exactly one row was updated; `false` if no row
     * matched the slug or on a SOCI exception.
     */
    virtual bool UpdateLink(const std::string& sql,
                            const SqlRow& row,
                            RepoError* error) = 0;

    /**
     * @brief Deletes the link row with the given slug.
     * @param sql DELETE statement from `SqlDialect::DeleteSql`.
     * @param short_code Slug of the row to delete.
     * @param[out] error If non-null, set to `RepoError::none` on success
     * (even if no row matched the slug) or the mapped exception error on a
     * SOCI failure.
     * @return `true` if the DELETE statement executed without throwing;
     * `false` on a SOCI exception. Does not distinguish between "deleted"
     * and "slug not found" — both return `true`.
     */
    virtual bool DeleteLink(const std::string& sql,
                            const std::string& short_code,
                            RepoError* error) = 0;

    /**
     * @brief Checks whether a link with the given slug exists.
     * @param sql COUNT query from `SqlDialect::ExistsSql`.
     * @param short_code Slug to check.
     * @param[out] exists Receives `true` if a matching row was found,
     * `false` otherwise; must be non-null.
     * @param[out] error If non-null, set to `RepoError::none` on success
     * (regardless of whether the slug exists) or the mapped exception error
     * on a SOCI failure.
     * @return `true` if the query executed without error; `false` on a
     * SOCI exception (`*exists` is indeterminate on `false`).
     */
    virtual bool Exists(const std::string& sql,
                        const std::string& short_code,
                        bool* exists,
                        RepoError* error) = 0;

    /**
     * @brief Lists link rows matching the given filter criteria.
     * @param sql SELECT statement from `SqlDialect::ListSql`.
     * @param owner If set, restricts results to this owner;
     * `std::nullopt` returns links regardless of owner.
     * @param include_inactive If `true`, inactive links are included; if
     * `false`, only active links are returned.
     * @param limit Maximum number of rows to return.
     * @param offset Number of rows to skip (for pagination).
     * @param[out] error If non-null, set to `RepoError::none` on success
     * or the mapped exception error on a SOCI failure.
     * @return The matching rows in creation-time order; an empty vector on
     * a SOCI exception or when no rows match the filter.
     */
    virtual std::vector<SqlRow> List(const std::string& sql,
                                     const std::optional<std::string>& owner,
                                     bool include_inactive,
                                     std::size_t limit,
                                     std::size_t offset,
                                     RepoError* error) = 0;
};

/**
 * @brief Factory interface for creating configured SQL sessions.
 *
 * `SqlMetadataRepository` calls `Create` once per repository operation so
 * each operation gets its own backend connection, avoiding shared mutable
 * state between concurrent callers.
 */
class ISqlSessionFactory
{
public:
    /**
     * @brief Virtual destructor; implementations may be destroyed through a
     * base `ISqlSessionFactory*`.
     */
    virtual ~ISqlSessionFactory() = default;

    /**
     * @brief Allocates and opens a new backend session.
     * @param[out] error If non-null and the call fails, receives
     * `RepoError::invalid_argument` if the config is malformed or backend
     * mismatch, `RepoError::transient_failure` or
     * `RepoError::permanent_failure` depending on the connection error;
     * left untouched on success.
     * @return A fully-opened `ISqlSession` on success; `nullptr` if the
     * backend connection could not be established.
     */
    virtual std::unique_ptr<ISqlSession> Create(RepoError* error) const = 0;
};
