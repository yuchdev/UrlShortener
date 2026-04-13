#pragma once

#include <memory>
#include <filesystem>

#include <boost/test/unit_test.hpp>

#include "url_shortener/core/clock.hpp"
#include "url_shortener/storage/sql/SqlConnectionConfig.hpp"
#include "url_shortener/storage/sql/SqlMetadataRepository.hpp"
#include "url_shortener/storage/sql/SqlMetadataRowMapper.hpp"
#include "url_shortener/storage/sqlite/SqliteSessionFactory.hpp"
#include "url_shortener/storage/sqlite/SqliteSqlDialect.hpp"

struct MetadataHarness {
    std::shared_ptr<ManualClock> clock;
    std::shared_ptr<IMetadataRepository> repo;
};

inline MetadataHarness MakeMetadataHarness()
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
    return {clock, repo};
}

inline CreateLinkRequest BasicRequest(const std::string& code, const std::string& target = "https://example.com")
{
    CreateLinkRequest req;
    req.short_code = code;
    req.target_url = target;
    return req;
}
