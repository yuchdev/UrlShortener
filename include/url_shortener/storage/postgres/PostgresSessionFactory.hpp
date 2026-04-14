#pragma once

#include "url_shortener/storage/postgres/PostgresErrorMapper.hpp"
#include "url_shortener/storage/sql/ISqlSessionFactory.hpp"
#include "url_shortener/storage/sql/SqlConnectionConfig.hpp"

/**
 * @brief PostgreSQL implementation of SQL session factory backed by SOCI.
 */
class PostgresSessionFactory final : public ISqlSessionFactory {
public:
    explicit PostgresSessionFactory(SqlConnectionConfig config);

    std::unique_ptr<ISqlSession> Create(RepoError* error) const override;

private:
    SqlConnectionConfig config_;
    PostgresErrorMapper error_mapper_;
};
