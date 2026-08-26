#pragma once

#include <filesystem>

#include "url_shortener/storage/sql/sql_connection_config.hpp"
#include "url_shortener/storage/sql/sql_metadata_repository.hpp"
#include "url_shortener/storage/sql/sql_metadata_row_mapper.hpp"
#include "url_shortener/storage/sqlite/sqlite_session_factory.hpp"
#include "url_shortener/storage/sqlite/sqlite_sql_dialect.hpp"

inline std::shared_ptr<SqlMetadataRepository> MakeSqliteRepo(const std::string& filename = "url_shortener_unit.sqlite3")
{
    auto db = std::filesystem::temp_directory_path() / std::filesystem::path(filename);
    std::filesystem::remove(db);

    SqlConnectionConfig cfg;
    cfg.backend = SqlBackendKind::sqlite;
    cfg.dsn = db.string();

    return std::make_shared<SqlMetadataRepository>(
        std::make_shared<SqliteSessionFactory>(cfg),
        std::make_shared<SqliteSqlDialect>(),
        std::make_shared<SqlMetadataRowMapper>());
}

inline CreateLinkRequest Req(const std::string& code, const std::string& target = "https://example.com")
{
    CreateLinkRequest req;
    req.short_code = code;
    req.target_url = target;
    return req;
}
