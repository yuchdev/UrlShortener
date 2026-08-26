#pragma once

#include <cstdlib>
#include <filesystem>
#include <memory>
#include <optional>
#include <vector>

#include <boost/test/unit_test.hpp>

#include "url_shortener/core/clock.hpp"
#include "url_shortener/storage/inmemory/in_memory_metadata_repository.hpp"
#include "url_shortener/storage/postgres/postgres_session_factory.hpp"
#include "url_shortener/storage/postgres/postgres_sql_dialect.hpp"
#include "url_shortener/storage/sql/sql_connection_config.hpp"
#include "url_shortener/storage/sql/sql_metadata_repository.hpp"
#include "url_shortener/storage/sql/sql_metadata_row_mapper.hpp"
#include "url_shortener/storage/sqlite/sqlite_session_factory.hpp"
#include "url_shortener/storage/sqlite/sqlite_sql_dialect.hpp"

struct MetadataHarness {
    std::string name;
    std::shared_ptr<ManualClock> clock;
    std::shared_ptr<IMetadataRepository> repo;
};

inline std::vector<MetadataHarness> MakeMetadataHarnesses()
{
    std::vector<MetadataHarness> out;

    {
        auto clock = std::make_shared<ManualClock>(std::chrono::system_clock::time_point{});
        out.push_back({"inmemory", clock, std::make_shared<InMemoryMetadataRepository>()});
    }

    {
        auto clock = std::make_shared<ManualClock>(std::chrono::system_clock::time_point{});
        auto db = std::filesystem::temp_directory_path() / std::filesystem::path("url_shortener_contract.sqlite3");
        SqlConnectionConfig config;
        config.backend = SqlBackendKind::sqlite;
        config.dsn = db.string();
        std::filesystem::remove(db);
        auto repo = std::make_shared<SqlMetadataRepository>(
            std::make_shared<SqliteSessionFactory>(config),
            std::make_shared<SqliteSqlDialect>(),
            std::make_shared<SqlMetadataRowMapper>());
        out.push_back({"sqlite", clock, repo});
    }

    if (const char* dsn = std::getenv("URL_SHORTENER_TEST_POSTGRES_DSN"); dsn != nullptr && std::string(dsn).size() > 0) {
        auto clock = std::make_shared<ManualClock>(std::chrono::system_clock::time_point{});
        SqlConnectionConfig config;
        config.backend = SqlBackendKind::postgres;
        config.dsn = dsn;
        config.max_retries = 1;
        auto repo = std::make_shared<SqlMetadataRepository>(
            std::make_shared<PostgresSessionFactory>(config),
            std::make_shared<PostgresSqlDialect>(),
            std::make_shared<SqlMetadataRowMapper>());
        out.push_back({"postgres", clock, repo});
    }

    return out;
}

inline CreateLinkRequest BasicRequest(const std::string& code, const std::string& target = "https://example.com")
{
    CreateLinkRequest req;
    req.short_code = code;
    req.target_url = target;
    return req;
}
