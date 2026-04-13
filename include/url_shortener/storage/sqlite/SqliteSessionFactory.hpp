#pragma once

#include "url_shortener/storage/sql/ISqlSessionFactory.hpp"
#include "url_shortener/storage/sql/SqlConnectionConfig.hpp"
#include "url_shortener/storage/sqlite/SqliteErrorMapper.hpp"

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
