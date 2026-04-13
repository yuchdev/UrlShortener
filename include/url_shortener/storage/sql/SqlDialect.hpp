#pragma once

#include <string>

/**
 * @brief SQL dialect abstraction that owns backend-specific SQL statements.
 */
class SqlDialect {
public:
    virtual ~SqlDialect() = default;

    /** @brief Returns SQL used to bootstrap metadata schema. */
    virtual std::string BootstrapSchemaSql() const = 0;
    virtual std::string InsertSql() const = 0;
    virtual std::string SelectByShortCodeSql() const = 0;
    virtual std::string UpdateSql() const = 0;
    virtual std::string DeleteSql() const = 0;
    virtual std::string ExistsSql() const = 0;
    virtual std::string ListSql() const = 0;
};
