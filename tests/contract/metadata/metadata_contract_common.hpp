#pragma once

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <optional>
#include <random>
#include <string>
#include <system_error>
#include <vector>

#if defined(_WIN32)
#    include <process.h>
#else
#    include <unistd.h>
#endif

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

struct MetadataHarness
{
    std::string name;
    std::shared_ptr<ManualClock> clock;
    std::shared_ptr<IMetadataRepository> repo;
};

/**
 * @brief Path of this process's SQLite contract database.
 *
 * The 12 `metadata__*` contract binaries run concurrently under `ctest -j`.
 * A single fixed file under `temp_directory_path()` would be shared by all of
 * them, so one binary's reset would delete rows another binary had just
 * written. The name therefore carries a per-process token: a random-device
 * draw plus the steady-clock start time, computed once.
 *
 * @return The same path for every call within one process.
 */
inline const std::filesystem::path& ContractSqlitePath()
{
    static const std::filesystem::path path = []
    {
        std::random_device device;
        const auto draw = (static_cast<unsigned long long>(device()) << 32U)
            ^ static_cast<unsigned long long>(device());
        const auto start = static_cast<unsigned long long>(
            std::chrono::steady_clock::now().time_since_epoch().count());
        char buffer[48];
        std::snprintf(buffer, sizeof(buffer), "%llx_%llx", draw, start);
        return std::filesystem::temp_directory_path()
            / ("url_shortener_contract_" + std::string(buffer) + ".sqlite3");
    }();
    // Constructed after `path`, so destroyed before it: removes this
    // process's database (and any SQLite side files) at exit.
    static const struct Cleanup
    {
        ~Cleanup()
        {
            std::error_code ignored;
            for (const char* suffix : {"", "-journal", "-wal", "-shm"}) {
                std::filesystem::remove(path.string() + suffix, ignored);
            }
        }
    } cleanup;
    (void)cleanup;
    return path;
}

/**
 * @brief Current process id, as a portable integer for identifier building.
 *
 * Used only to make this process's Postgres contract schema name unique; it
 * never carries into product code. Wraps `getpid()` on POSIX and `_getpid()`
 * on Windows so the header compiles on every platform the contract tests build
 * on.
 *
 * @return The process id of the caller.
 */
inline long long ContractCurrentPid()
{
#if defined(_WIN32)
    return static_cast<long long>(_getpid());
#else
    return static_cast<long long>(::getpid());
#endif
}

/**
 * @brief Name of this process's private PostgreSQL schema for the contract
 * suite.
 *
 * Unlike SQLite (a per-process file, see `ContractSqlitePath`), every Postgres
 * contract process connects to the *same* database behind
 * `URL_SHORTENER_TEST_POSTGRES_DSN`. Running the `metadata__*` contract
 * binaries (plus the SQLite/Postgres integration runners and the psql Python
 * smoke tests) under `ctest -j` therefore made them share one `public.links`
 * table, so one process's reset or `DELETE FROM links` clobbered rows another
 * had just written. Giving each process its own schema - into which the
 * bootstrap creates `links` - restores the same isolation the SQLite file path
 * already provides. The name carries the pid plus a random-device draw so two
 * processes (or a pid reused after wraparound) never collide.
 *
 * @return The same lower-case, identifier-safe schema name for every call
 * within one process (e.g. `us_test_12345_a1b2c3d4`).
 */
inline const std::string& ContractPostgresSchema()
{
    static const std::string schema = []
    {
        std::random_device device;
        const auto draw = (static_cast<unsigned long long>(device()) << 32U)
            ^ static_cast<unsigned long long>(device());
        char buffer[64];
        std::snprintf(buffer,
                      sizeof(buffer),
                      "us_test_%lld_%llx",
                      ContractCurrentPid(),
                      draw);
        return std::string(buffer);
    }();
    return schema;
}

/**
 * @brief Base Postgres DSN for the contract suite, or empty when disabled.
 *
 * @return The value of `URL_SHORTENER_TEST_POSTGRES_DSN`, or an empty string
 * when the variable is unset or empty - in which case the Postgres harness is
 * skipped entirely and the contract tests run SQLite/in-memory only.
 */
inline std::string ContractPostgresBaseDsn()
{
    const char* dsn = std::getenv("URL_SHORTENER_TEST_POSTGRES_DSN");
    return (dsn != nullptr) ? std::string(dsn) : std::string();
}

/**
 * @brief The base DSN augmented to route every unqualified object reference
 * into this process's private schema (see `ContractPostgresSchema`).
 *
 * Appends a libpq `options` keyword that sets the `search_path` GUC on every
 * connection the `PostgresSessionFactory` opens from this DSN, so the schema
 * bootstrap creates `links` inside the per-process schema and all reads/writes
 * stay scoped to it. The base DSN must be libpq keyword/value form (the factory
 * already appends ` connect_timeout=...` the same way).
 *
 * @param base Base DSN; must be non-empty.
 * @return `base` with ` options='-c search_path=<schema>'` appended.
 */
inline std::string ContractPostgresDsnWithSchema(const std::string& base)
{
    return base + " options='-c search_path=" + ContractPostgresSchema() + "'";
}

/**
 * @brief Process-wide augmented Postgres DSN; creates this process's schema on
 * first use and registers its drop at process exit.
 *
 * On first call (when a base DSN is configured) it opens one session on the
 * *base* DSN - still on the default `search_path` - and runs
 * `CREATE SCHEMA IF NOT EXISTS <schema>`, then returns
 * `ContractPostgresDsnWithSchema`. A function-local static whose destructor
 * runs at process exit drops the schema with `CASCADE`, removing `links` and
 * anything else the process created. Both the create and drop best-effort
 * ignore errors (a server that became unreachable must not crash teardown).
 *
 * @return The augmented DSN (search_path set), or an empty string when
 * `URL_SHORTENER_TEST_POSTGRES_DSN` is unset/empty.
 */
inline const std::string& ContractPostgresDsn()
{
    static const std::string dsn = []
    {
        const std::string base = ContractPostgresBaseDsn();
        if (base.empty()) {
            return std::string();
        }
        SqlConnectionConfig config;
        config.backend = SqlBackendKind::postgres;
        config.dsn = base;
        config.max_retries = 1;
        PostgresSessionFactory factory(config);
        RepoError error = RepoError::none;
        auto session = factory.Create(&error);
        if (session != nullptr) {
            session->Bootstrap(
                "CREATE SCHEMA IF NOT EXISTS " + ContractPostgresSchema(),
                &error);
        }
        return ContractPostgresDsnWithSchema(base);
    }();
    // Drops this process's schema (and every object in it) at static
    // destruction. It recomputes the DSN and schema name itself and does not
    // depend on `dsn`. Best-effort; a dead server is ignored.
    static const struct SchemaCleanup
    {
        ~SchemaCleanup()
        {
            const std::string base = ContractPostgresBaseDsn();
            if (base.empty()) {
                return;
            }
            SqlConnectionConfig config;
            config.backend = SqlBackendKind::postgres;
            config.dsn = base;
            config.max_retries = 1;
            PostgresSessionFactory factory(config);
            RepoError error = RepoError::none;
            auto session = factory.Create(&error);
            if (session != nullptr) {
                session->Bootstrap("DROP SCHEMA IF EXISTS "
                                       + ContractPostgresSchema() + " CASCADE",
                                   &error);
            }
        }
    } cleanup;
    (void)cleanup;
    return dsn;
}

// Defined below; resets a single harness to an empty `links` table so
// MakeMetadataHarnesses can give the Postgres harness the same per-case
// isolation the SQLite file removal already provides (threat model T-50).
inline void ResetMetadataHarness(MetadataHarness& harness);

inline std::vector<MetadataHarness> MakeMetadataHarnesses()
{
    std::vector<MetadataHarness> out;

    {
        auto clock = std::make_shared<ManualClock>(
            std::chrono::system_clock::time_point {});
        out.push_back({"inmemory",
                       clock,
                       std::make_shared<InMemoryMetadataRepository>()});
    }

    {
        auto clock = std::make_shared<ManualClock>(
            std::chrono::system_clock::time_point {});
        const auto& db = ContractSqlitePath();
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

    if (const std::string dsn = ContractPostgresDsn(); !dsn.empty()) {
        auto clock = std::make_shared<ManualClock>(
            std::chrono::system_clock::time_point {});
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

inline CreateLinkRequest BasicRequest(
    const std::string& code, const std::string& target = "https://example.com")
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
        const auto& db = ContractSqlitePath();
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
        const std::string dsn = ContractPostgresDsn();
        if (dsn.empty()) {
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
