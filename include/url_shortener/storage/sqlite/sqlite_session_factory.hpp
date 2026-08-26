#pragma once

#include "url_shortener/storage/sql/i_sql_session_factory.hpp"
#include "url_shortener/storage/sql/sql_connection_config.hpp"
#include "url_shortener/storage/sqlite/sqlite_error_mapper.hpp"

/**
 * @brief SQLite implementation of SQL session factory backed by SOCI.
 */
class SqliteSessionFactory final : public ISqlSessionFactory {
public:
    explicit SqliteSessionFactory(SqlConnectionConfig config);

    std::unique_ptr<ISqlSession> Create(RepoError* error) const override;

private:
    SqlConnectionConfig config_;
    SqliteErrorMapper error_mapper_;
};
