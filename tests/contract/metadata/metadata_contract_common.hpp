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

// Defined below; resets a single harness to an empty `links` table so
// MakeMetadataHarnesses can give the Postgres harness the same per-case
// isolation the SQLite file removal already provides (threat model T-50).
inline void ResetMetadataHarness(MetadataHarness& harness);

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
        // Mirror the SQLite file reset above: start every harness build
        // from an empty shared Postgres `links` table.
        ResetMetadataHarness(out.back());
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

/**
 * @brief Restores a single harness to an empty `links` table so each
 * contract case starts from a known-empty store, including the Postgres
 * harness which otherwise shares one database across every case.
 *
 * The in-memory harness is given a fresh `InMemoryMetadataRepository`; the
 * SQLite harness deletes its temp database file behind a fresh repository
 * (mirroring `MakeMetadataHarnesses`, which also re-creates the file), so
 * the next operation lazily re-bootstraps an empty schema; the Postgres
 * harness issues `DELETE FROM links` through a short-lived
 * `PostgresSessionFactory` session, after an idempotent schema bootstrap so
 * the statement never fails on the very first reset. Mirrors the existing
 * SQLite file reset and gives the Postgres harness per-case isolation
 * (threat model T-50).
 *
 * @param harness Harness to reset in place; `harness.name` selects the
 * backend-specific reset. An unknown name leaves the harness untouched.
 */
inline void ResetMetadataHarness(MetadataHarness& harness)
{
    if (harness.name == "inmemory") {
        harness.repo = std::make_shared<InMemoryMetadataRepository>();
        return;
    }
    if (harness.name == "sqlite") {
        auto db = std::filesystem::temp_directory_path()
            / std::filesystem::path("url_shortener_contract.sqlite3");
        SqlConnectionConfig config;
        config.backend = SqlBackendKind::sqlite;
        config.dsn = db.string();
        std::filesystem::remove(db);
        harness.repo = std::make_shared<SqlMetadataRepository>(
            std::make_shared<SqliteSessionFactory>(config),
            std::make_shared<SqliteSqlDialect>(),
            std::make_shared<SqlMetadataRowMapper>());
        return;
    }
    if (harness.name == "postgres") {
        const char* dsn = std::getenv("URL_SHORTENER_TEST_POSTGRES_DSN");
        if (dsn == nullptr || std::string(dsn).empty()) {
            return;
        }
        SqlConnectionConfig config;
        config.backend = SqlBackendKind::postgres;
        config.dsn = dsn;
        config.max_retries = 1;
        PostgresSessionFactory factory(config);
        PostgresSqlDialect dialect;
        RepoError error = RepoError::none;
        auto session = factory.Create(&error);
        if (session == nullptr) {
            return;
        }
        session->Bootstrap(dialect.BootstrapSchemaSql(), &error);
        session->Bootstrap(dialect.CreateIdIndexSql(), &error);
        session->Bootstrap("DELETE FROM links", &error);
    }
}

/**
 * @brief Harness set shared by every case in a contract test module, built
 * exactly once so that per-case `ResetMetadataHarness` - not a rebuild - is
 * what clears the shared Postgres database between cases.
 * @return Reference to the module-wide harness vector.
 */
inline std::vector<MetadataHarness>& SharedMetadataHarnesses()
{
    static std::vector<MetadataHarness> harnesses = MakeMetadataHarnesses();
    return harnesses;
}

/**
 * @brief Boost per-case fixture that resets every shared harness before the
 * case body runs, giving the Postgres harness the same per-case isolation
 * the SQLite file reset already provides (threat model T-50).
 */
struct ResetSharedHarnesses
{
    /**
     * @brief Resets each shared harness to an empty `links` table.
     */
    ResetSharedHarnesses()
    {
        for (auto& harness : SharedMetadataHarnesses()) {
            ResetMetadataHarness(harness);
        }
    }
};
