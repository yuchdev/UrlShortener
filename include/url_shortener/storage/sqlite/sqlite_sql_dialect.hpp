#pragma once

#include "url_shortener/storage/sql/sql_dialect.hpp"

/**
 * @brief `SqlDialect` implementation for SQLite. Timestamps are stored as
 * `INTEGER` (Unix epoch seconds); `is_active` as `INTEGER` (0/1);
 * `attributes_json` as `TEXT`. All column names and parameter names are
 * identical to the `SqlRow` field names for direct SOCI binding.
 *
 * @see SqlDialect for the contract each method fulfills.
 */
class SqliteSqlDialect final : public SqlDialect
{
public:
    /**
     * @brief Returns `CREATE TABLE IF NOT EXISTS links (…)` DDL with
     * SQLite-native column types.
     */
    std::string BootstrapSchemaSql() const override;

    /**
     * @brief Returns `CREATE INDEX IF NOT EXISTS idx_links_id ON
     * links(id)`.
     */
    std::string CreateIdIndexSql() const override;

    /**
     * @brief Returns `INSERT INTO links(…) VALUES(…)` with named SOCI
     * parameters matching all `SqlRow` fields.
     */
    std::string InsertSql() const override;

    /**
     * @brief Returns `SELECT … FROM links WHERE short_code = :short_code`.
     */
    std::string SelectByShortCodeSql() const override;

    /**
     * @brief Returns `SELECT … FROM links WHERE id = :id LIMIT 1`.
     */
    std::string SelectByIdSql() const override;

    /**
     * @brief Returns `UPDATE links SET … WHERE short_code = :short_code`.
     */
    std::string UpdateSql() const override;

    /**
     * @brief Returns `DELETE FROM links WHERE short_code = :short_code`.
     */
    std::string DeleteSql() const override;

    /**
     * @brief Returns `SELECT COUNT(1) FROM links WHERE short_code =
     * :short_code`.
     */
    std::string ExistsSql() const override;

    /**
     * @brief Returns a paginated SELECT with `:owner_id`, `:include_inactive`,
     * `:limit`, and `:offset` named parameters, ordered by `created_at`.
     */
    std::string ListSql() const override;
};
