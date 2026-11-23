#pragma once

#include "url_shortener/storage/sql/sql_dialect.hpp"

/**
 * @brief `SqlDialect` implementation for PostgreSQL. Timestamps are stored
 * as `TIMESTAMPTZ` and converted to/from Unix epoch seconds via
 * `to_timestamp()` / `EXTRACT(EPOCH FROM …)`. `is_active` is stored as
 * `BOOLEAN`; `attributes_json` as `JSONB`. SELECT statements cast all
 * columns back to the types that SOCI can bind to `SqlRow` fields.
 *
 * @see SqlDialect for the contract each method fulfills.
 */
class PostgresSqlDialect final : public SqlDialect
{
public:
    /**
     * @brief Returns `CREATE TABLE IF NOT EXISTS links (…)` DDL with
     * PostgreSQL-native column types (`TIMESTAMPTZ`, `BOOLEAN`, `JSONB`).
     */
    std::string BootstrapSchemaSql() const override;

    /**
     * @brief Returns `INSERT INTO links(…) VALUES(…)` using
     * `to_timestamp(:created_at)` / `to_timestamp(:updated_at)` for
     * timestamps and `CAST(:attributes_json AS JSONB)`.
     */
    std::string InsertSql() const override;

    /**
     * @brief Returns `SELECT … FROM links WHERE short_code = :short_code`,
     * casting `TIMESTAMPTZ` columns back to `BIGINT` epoch seconds and
     * `BOOLEAN` to `0`/`1` for `SqlRow` binding.
     */
    std::string SelectByShortCodeSql() const override;

    /**
     * @brief Returns `UPDATE links SET … WHERE short_code = :short_code`
     * using `to_timestamp()` for timestamp parameters.
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
     * Casts `TIMESTAMPTZ` and `BOOLEAN` columns for `SqlRow` binding.
     */
    std::string ListSql() const override;
};
