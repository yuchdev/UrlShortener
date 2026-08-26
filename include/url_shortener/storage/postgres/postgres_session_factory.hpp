#pragma once

#include "url_shortener/storage/postgres/postgres_error_mapper.hpp"
#include "url_shortener/storage/sql/i_sql_session_factory.hpp"
#include "url_shortener/storage/sql/sql_connection_config.hpp"

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
