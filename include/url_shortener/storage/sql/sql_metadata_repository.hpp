#pragma once

#include <memory>
#include <mutex>

#include "url_shortener/storage/i_metadata_repository.hpp"
#include "url_shortener/storage/sql/i_sql_session_factory.hpp"
#include "url_shortener/storage/sql/sql_dialect.hpp"
#include "url_shortener/storage/sql/sql_metadata_row_mapper.hpp"

/**
 * @brief `IMetadataRepository` implementation backed by any SQL backend
 * (SQLite or PostgreSQL) via `ISqlSessionFactory` and `SqlDialect`.
 *
 * Thread-safety: each operation creates a new `ISqlSession` via the factory,
 * so concurrent callers do not share a backend connection. The one-time
 * schema bootstrap is serialized through `bootstrap_mutex_` and is safe to
 * trigger from multiple threads. The `session_factory_`, `dialect_`, and
 * `mapper_` are owned via `shared_ptr` and must remain valid for the
 * lifetime of this object.
 *
 * @see SqliteSessionFactory, PostgresSessionFactory, SqliteSqlDialect,
 * PostgresSqlDialect
 */
class SqlMetadataRepository final : public IMetadataRepository
{
public:
    /**
     * @brief Stores the shared components; does not connect to the backend
     * or create the schema (see `EnsureBootstrapped`).
     * @param session_factory Factory used to open a fresh session per
     * operation; must be non-null and outlive this object.
     * @param dialect SQL statement provider for the target backend; must be
     * non-null and outlive this object.
     * @param mapper Domain-to-row converter; must be non-null and outlive
     * this object.
     */
    SqlMetadataRepository(std::shared_ptr<ISqlSessionFactory> session_factory,
                          std::shared_ptr<SqlDialect> dialect,
                          std::shared_ptr<SqlMetadataRowMapper> mapper);

    /**
     * @brief Persists a new link. @see `IMetadataRepository::CreateLink`.
     * @param[out] error If non-null and the call fails, receives
     * `RepoError::invalid_argument` if `request` fails validation,
     * `RepoError::already_exists` if `short_code` is taken, or a session /
     * bootstrap error otherwise; left untouched on success.
     * @return `true` if the row was inserted; `false` on validation failure,
     * slug collision, or backend error.
     */
    bool CreateLink(const CreateLinkRequest& request,
                    const std::string& id,
                    const std::chrono::system_clock::time_point& now,
                    RepoError* error = nullptr) override;

    /**
     * @brief Looks up a link by slug.
     * @see `IMetadataRepository::GetByShortCode`.
     * @param[out] error If non-null, set to `RepoError::not_found` when no
     * row matches, `RepoError::permanent_failure` on a row-mapping error,
     * or a session / bootstrap error; set to `RepoError::none` on success.
     * @return The `LinkRecord` on success; `std::nullopt` if not found or
     * on any error.
     */
    std::optional<LinkRecord> GetByShortCode(
        const std::string& short_code,
        RepoError* error = nullptr) const override;

    /**
     * @brief Looks up a link by its stable id using the indexed
     * `SelectByIdSql` query. @see `IMetadataRepository::GetById`.
     * @param[out] error If non-null, set to `RepoError::not_found` when no
     * row matches, `RepoError::permanent_failure` on a row-mapping error,
     * or a session / bootstrap error; set to `RepoError::none` on success.
     * On an empty id, left untouched and no query is issued.
     * @return The `LinkRecord` on success; `std::nullopt` if the id is
     * empty, not found, or on any error.
     */
    std::optional<LinkRecord> GetById(
        const std::string& id, RepoError* error = nullptr) const override;

    /**
     * @brief Replaces a link's mutable fields.
     * @see `IMetadataRepository::UpdateLink`.
     * @param[out] error If non-null and the call fails, receives
     * `RepoError::not_found` if the slug does not exist, or a session /
     * bootstrap error; left untouched on success.
     * @return `true` if the row was updated; `false` if the slug was not
     * found or on a backend error.
     */
    bool UpdateLink(const LinkRecord& record,
                    RepoError* error = nullptr) override;

    /**
     * @brief Deletes a link by slug. @see `IMetadataRepository::DeleteLink`.
     * @param[out] error If non-null and the call fails, receives a session /
     * bootstrap error; set to `RepoError::none` on success. Does not set
     * `RepoError::not_found` when the slug is absent — the DELETE succeeds
     * silently.
     * @return `true` if the DELETE executed without backend error; `false`
     * on a session or bootstrap failure.
     */
    bool DeleteLink(const std::string& short_code,
                    RepoError* error = nullptr) override;

    /**
     * @brief Returns all links matching the query. @see
     * `IMetadataRepository::ListLinks`.
     * @param[out] error If non-null and the call fails, receives a session /
     * bootstrap error or `RepoError::permanent_failure` on a row-mapping
     * error; set to `RepoError::none` on success.
     * @return The matching `LinkRecord` list; an empty vector on any error
     * or when no rows match.
     */
    std::vector<LinkRecord> ListLinks(
        const ListLinksQuery& query, RepoError* error = nullptr) const override;

    /**
     * @brief Checks whether a link with the given slug exists. @see
     * `IMetadataRepository::Exists`.
     * @param[out] error If non-null and the call fails, receives a session /
     * bootstrap error; left untouched on success.
     * @return `true` if the slug exists; `false` if it does not exist or on
     * a backend error.
     */
    bool Exists(const std::string& short_code,
                RepoError* error = nullptr) const override;

private:
    /**
     * @brief Ensures the `links` table and the `idx_links_id` secondary
     * index exist, bootstrapping them on the first call. The table DDL and
     * the index DDL are run as two separate `ISqlSession::Bootstrap`
     * statements (one statement per call, a SOCI/SQLite constraint). Both
     * are `IF NOT EXISTS` and idempotent; protected by `bootstrap_mutex_`
     * so concurrent calls from `const` methods are safe.
     * @param session Open session against which to run the DDL if needed.
     * @param[out] error Forwarded from `ISqlSession::Bootstrap` on failure;
     * set to `RepoError::none` if already bootstrapped.
     * @return `true` if the schema is ready; `false` if either DDL failed.
     */
    bool EnsureBootstrapped(ISqlSession& session, RepoError* error) const;

    ///< Factory used to open a fresh session per operation. Owned via
    ///< `shared_ptr`.
    std::shared_ptr<ISqlSessionFactory> session_factory_;

    ///< SQL statement provider for the target backend. Owned via
    ///< `shared_ptr`.
    std::shared_ptr<SqlDialect> dialect_;

    ///< Domain-to-row converter. Owned via `shared_ptr`.
    std::shared_ptr<SqlMetadataRowMapper> mapper_;

    ///< Guards `bootstrapped_`; `mutable` so that `const` methods
    ///< (`GetByShortCode`, `ListLinks`, `Exists`) can call
    ///< `EnsureBootstrapped` on the first operation.
    mutable std::mutex bootstrap_mutex_;

    ///< Set to `true` after the first successful `ISqlSession::Bootstrap`
    ///< call; subsequent calls skip the DDL. Guarded by `bootstrap_mutex_`.
    mutable bool bootstrapped_ = false;
};
