#pragma once

#include <string>

/**
 * @brief SQL dialect abstraction that supplies backend-specific SQL
 * statements for the `links` metadata table. Implementations carry all
 * backend differences (e.g. `TIMESTAMPTZ` vs. `INTEGER` for timestamps,
 * `JSONB` vs. `TEXT` for attributes) so that `ISqlSession` implementations
 * and `SqlMetadataRepository` remain backend-neutral.
 *
 * @see SqliteSqlDialect, PostgresSqlDialect
 */
class SqlDialect
{
public:
    /**
     * @brief Virtual destructor; implementations may be destroyed through a
     * base `SqlDialect*`.
     */
    virtual ~SqlDialect() = default;

    /**
     * @brief Returns the DDL to create the `links` table and its indexes
     * if they do not already exist (`CREATE TABLE IF NOT EXISTS …`).
     * @return A complete, self-contained SQL DDL string.
     */
    virtual std::string BootstrapSchemaSql() const = 0;

    /**
     * @brief Returns the DDL to create the non-unique secondary index on
     * the `links(id)` column (`CREATE INDEX IF NOT EXISTS idx_links_id …`).
     *
     * Issued as a separate `ISqlSession::Bootstrap` statement after the
     * table DDL: SOCI/SQLite executes one statement per `session_ << sql`,
     * so this must not be concatenated onto `BootstrapSchemaSql`. The index
     * makes `GetById` O(log n) instead of a table scan (threat model T-36).
     * @return A complete, self-contained `CREATE INDEX IF NOT EXISTS` DDL
     * string.
     */
    virtual std::string CreateIdIndexSql() const = 0;

    /**
     * @brief Returns the INSERT statement for a new link row, with named
     * SOCI bind parameters matching `SqlRow` fields.
     * @return A complete INSERT SQL string.
     */
    virtual std::string InsertSql() const = 0;

    /**
     * @brief Returns the SELECT statement to fetch one link row by slug,
     * with a `:short_code` named parameter.
     * @return A complete SELECT SQL string.
     */
    virtual std::string SelectByShortCodeSql() const = 0;

    /**
     * @brief Returns the SELECT statement to fetch one link row by its
     * stable id, with an `:id` named parameter.
     *
     * Same column list as `SelectByShortCodeSql`, but filters on the `id`
     * column and appends `LIMIT 1` because `id` is indexed but not unique
     * (uniqueness is a caller invariant, not a DB constraint).
     * @return A complete `SELECT … WHERE id = :id … LIMIT 1` SQL string.
     */
    virtual std::string SelectByIdSql() const = 0;

    /**
     * @brief Returns the UPDATE statement to modify an existing link row,
     * keyed on `:short_code`.
     * @return A complete UPDATE SQL string.
     */
    virtual std::string UpdateSql() const = 0;

    /**
     * @brief Returns the DELETE statement to remove a link row by slug,
     * with a `:short_code` named parameter.
     * @return A complete DELETE SQL string.
     */
    virtual std::string DeleteSql() const = 0;

    /**
     * @brief Returns the query to count rows matching a given slug, with a
     * `:short_code` named parameter. Result is used to determine existence.
     * @return A complete SELECT COUNT SQL string.
     */
    virtual std::string ExistsSql() const = 0;

    /**
     * @brief Returns the SELECT statement to list links with filter and
     * pagination, using named parameters `:owner_id`, `:include_inactive`,
     * `:limit`, and `:offset`.
     * @return A complete SELECT SQL string ordered by creation time.
     */
    virtual std::string ListSql() const = 0;
};
