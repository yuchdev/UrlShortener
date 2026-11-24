/**
 * @file 15_repository_link_store_sqlite.cpp
 * @brief SQLite instantiation of the shared link-store contract
 * (subtask 02.0/06).
 *
 * Wires `run_link_store_contract` to a real SQLite file: each
 * `make_store()` builds a *new* `SqlMetadataRepository` +
 * `RepositoryLinkStore` over the same file (models "next process"), so
 * `persistent` is true and the reopen/stats cases (2, 8) run in addition
 * to the shared subset. This proves field-for-field persistence across
 * store instances including the write-behind redirect counters, with a
 * real SQL round trip through the attribute-packing codec.
 *
 * File isolation: the database path carries the process id so two
 * overlapping `ctest` runs never share a file (the repo's sqlite tests
 * otherwise use fixed names that collide); the file is removed before the
 * run and by the fixture cleanup afterwards.
 */
#define BOOST_TEST_MODULE RepositoryLinkStoreContractSqlite
#include <chrono>
#include <filesystem>
#include <memory>
#include <string>

#include <boost/test/unit_test.hpp>
#include <url_shortener/app/expiring_link_cache.hpp>
#include <url_shortener/app/repository_link_store.hpp>
#include <url_shortener/core/clock.hpp>
#include <url_shortener/storage/sql/sql_connection_config.hpp>
#include <url_shortener/storage/sql/sql_metadata_repository.hpp>
#include <url_shortener/storage/sql/sql_metadata_row_mapper.hpp>
#include <url_shortener/storage/sqlite/sqlite_session_factory.hpp>
#include <url_shortener/storage/sqlite/sqlite_sql_dialect.hpp>

#include "link_store_contract_common.hpp"

#ifdef _WIN32
#    include <process.h>
#else
#    include <unistd.h>
#endif

namespace
{

/**
 * @brief Returns this process's id, used to make the SQLite path unique
 * per process so concurrent `ctest` runs do not collide on one file.
 * @return The current process id.
 */
long current_pid()
{
#ifdef _WIN32
    return static_cast<long>(_getpid());
#else
    return static_cast<long>(::getpid());
#endif
}

}  // namespace

/**
 * [Integration][App] RepositoryLinkStore satisfies the link-store
 * contract over a real SQLite file, including persistence across store
 * instances.
 *
 * Scenario:
 *   Given a temp SQLite file and a fresh SqlMetadataRepository +
 *   RepositoryLinkStore built per make_store() call (models a new
 *   process re-opening the shared database).
 *   When the shared contract runs all ten cases.
 *   Then every field round-trips through the attribute-packing codec and
 *   a second store instance reads them back equal (case 2), soft-delete
 *   survives the schema's missing deleted_at column (case 5), write-behind
 *   redirect counters persist after flush_stats across a reopen while
 *   every other field is untouched (case 8), and concurrent creates of one
 *   slug yield exactly one winner on SQLite (case 9).
 *
 * If this breaks, first check:
 *   - the codec's attr_* packing (notably attr_deleted / lnk_stats) over
 *     a real SQL round trip.
 *   - RepositoryLinkStore::flush_stats persisting only the stats attribute.
 *   - the SQLite busy_timeout under the 8-way concurrent create (case 9).
 */
BOOST_AUTO_TEST_CASE(repository_link_store_contract_sqlite)
{
    const auto db = std::filesystem::temp_directory_path()
        / ("15_repository_link_store_sqlite_" + std::to_string(current_pid())
           + ".db");
    std::filesystem::remove(db);

    auto clock =
        std::make_shared<ManualClock>(std::chrono::system_clock::time_point {});

    LinkStoreFixture fixture;
    fixture.clock = clock;
    fixture.persistent = true;
    fixture.make_store = [db, clock]()
    {
        SqlConnectionConfig config;
        config.backend = SqlBackendKind::sqlite;
        config.dsn = db.string();
        auto repo = std::make_shared<SqlMetadataRepository>(
            std::make_shared<SqliteSessionFactory>(config),
            std::make_shared<SqliteSqlDialect>(),
            std::make_shared<SqlMetadataRowMapper>());
        auto l1 = std::make_shared<url_shortener::app::ExpiringLinkCache>(
            *clock, std::chrono::seconds(0));
        return std::make_unique<url_shortener::app::RepositoryLinkStore>(
            repo, nullptr, l1, *clock);
    };
    fixture.cleanup = [db]() { std::filesystem::remove(db); };

    run_link_store_contract(fixture);
}
