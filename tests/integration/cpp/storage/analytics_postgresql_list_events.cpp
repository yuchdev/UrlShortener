#define BOOST_TEST_MODULE AnalyticsPostgresListEvents
#include <cstdlib>
#include <string>

#include <boost/test/unit_test.hpp>

#include "click_event_list_events_shared.hpp"

using namespace url_shortener;

/**
 * Scenario: the backend-neutral `ListEvents` suite run against PostgreSQL,
 * asserting identical behavior to SQLite, plus an explicit lossless timestamp
 * round-trip through `to_timestamp`/`EXTRACT(EPOCH ...)`.
 * Boundaries: same filters/sort/paging/escaping/injection/text-cap cases as the
 * SQLite suite; `occurred_at` preserved to the second.
 * On failure first check: that `URL_SHORTENER_TEST_POSTGRES_DSN` points at a
 * reachable database (the test SKIPS and passes when it is unset); then whether
 * a lower()/`to_timestamp` difference changed results versus the SQLite target.
 */
BOOST_AUTO_TEST_CASE(postgresql_list_events)
{
    const char* dsn = std::getenv("URL_SHORTENER_TEST_POSTGRES_DSN");
    if (dsn == nullptr || std::string(dsn).empty()) {
        BOOST_TEST_MESSAGE("SKIPPED: URL_SHORTENER_TEST_POSTGRES_DSN not set");
        BOOST_TEST(true);
        return;
    }

    storage::sql::SqlClickEventRepository repo(dsn);
    ce_list_test::run_all(repo);

    // Explicit lossless timestamp round-trip through to_timestamp/EXTRACT.
    ce_list_test::clear(repo);
    const analytics::ClickEvent e =
        ce_list_test::ev("pgts", "s", "d.example", 302, 1700000000LL);
    BOOST_TEST(repo.InsertBatch({e}, nullptr));
    analytics::ClickEventQuery q;
    q.slug = "s";
    q.limit = 10;
    analytics::ClickEventPage page;
    BOOST_TEST(repo.ListEvents(q, &page, nullptr));
    BOOST_REQUIRE_EQUAL(page.items.size(), 1u);
    BOOST_TEST((page.items[0].occurred_at == ce_list_test::ts(1700000000LL)));

    // Leave the shared database clean for the next run.
    ce_list_test::clear(repo);
}
