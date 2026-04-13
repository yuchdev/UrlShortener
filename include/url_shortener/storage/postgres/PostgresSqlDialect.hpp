#pragma once

#include "url_shortener/storage/sql/SqlDialect.hpp"

/**
 * @brief PostgreSQL SQL dialect implementation for metadata statements.
 */
class PostgresSqlDialect final : public SqlDialect {
public:
    std::string BootstrapSchemaSql() const override;
    std::string InsertSql() const override;
    std::string SelectByShortCodeSql() const override;
    std::string UpdateSql() const override;
    std::string DeleteSql() const override;
    std::string ExistsSql() const override;
    std::string ListSql() const override;
};
