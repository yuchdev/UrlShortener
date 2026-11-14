#define BOOST_TEST_MODULE AnalyticsSqliteListEvents
#include <filesystem>

#include <boost/test/unit_test.hpp>

#include "click_event_list_events_shared.hpp"

using namespace url_shortener;

/**
 * Scenario: the backend-neutral `ListEvents` suite plus SQLite-only file
 * persistence, exercised against a temp-file database.
 * Boundaries: filters/sort/paging/escaping/injection/text-cap (shared), and
 * events surviving a fresh repository reopening the same file.
 * On failure first check: whether a prior overlapping SQLite test left the temp
 * file behind (re-run this target alone); then that the shared cases pass for
 * the in-memory backend (unit test 17) to isolate SQL-specific regressions.
 */
BOOST_AUTO_TEST_CASE(sqlite_list_events)
{
    const auto db_path = std::filesystem::temp_directory_path()
        / "analytics_sqlite_list_events_test.db";
    std::filesystem::remove(db_path);

    {
        storage::sql::SqlClickEventRepository repo(db_path.string());
        ce_list_test::run_all(repo);
    }

    // Persistence: seed through one repository, then verify a fresh repository
    // opened on the same file sees the rows.
    {
        storage::sql::SqlClickEventRepository repo(db_path.string());
        ce_list_test::clear(repo);
        ce_list_test::seed12(repo);
    }
    {
        storage::sql::SqlClickEventRepository repo(db_path.string());
        analytics::ClickEventQuery q;
        q.limit = 100;
        analytics::ClickEventPage page;
        BOOST_TEST(repo.ListEvents(q, &page, nullptr));
        BOOST_TEST(page.total == 12u);
        BOOST_TEST(page.items.size() == 12u);
    }

    std::filesystem::remove(db_path);
}
