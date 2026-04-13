#define BOOST_TEST_MODULE PostgresMigrationRunnerOrderingTest
#include <boost/test/unit_test.hpp>

#include <fstream>

#include "url_shortener/storage/postgres/PostgresMigrationRunner.hpp"

BOOST_AUTO_TEST_CASE(up_and_down_migrations_are_ordered_deterministically)
{
    auto tmp = std::filesystem::temp_directory_path() / "pg_migrations_unit";
    std::filesystem::create_directories(tmp);
    std::ofstream(tmp / "002_more.up.sql") << "SELECT 1;";
    std::ofstream(tmp / "001_base.up.sql") << "SELECT 1;";
    std::ofstream(tmp / "001_base.down.sql") << "SELECT 1;";
    std::ofstream(tmp / "002_more.down.sql") << "SELECT 1;";

    PostgresMigrationRunner r(tmp);
    auto up = r.OrderedUpMigrations();
    auto down = r.OrderedDownMigrations();

    BOOST_TEST(up.size() == 2U);
    BOOST_TEST(down.size() == 2U);
    BOOST_TEST(up.front().filename().string() == "001_base.up.sql");
    BOOST_TEST(down.front().filename().string() == "002_more.down.sql");
}
