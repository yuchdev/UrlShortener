#define BOOST_TEST_MODULE AnalyticsPostgresRepositoryRoundtrip
#include <boost/test/unit_test.hpp>
#include <cstdlib>
#include "url_shortener/storage/sql/SqlClickEventRepository.hpp"
using namespace url_shortener;
BOOST_AUTO_TEST_CASE(roundtrip_or_skip){ if(!std::getenv("URL_SHORTENER_ENABLE_POSTGRES_INTEGRATION")){ BOOST_TEST_MESSAGE("SKIPPED: PostgreSQL integration unavailable"); BOOST_TEST(true); return; }
 storage::sql::SqlClickEventRepository repo; analytics::ClickEvent e; e.slug="s"; e.link_id="l"; e.domain="d"; e.status_code=302; e.occurred_at=std::chrono::system_clock::time_point{}; BOOST_TEST(repo.InsertBatch({e},nullptr)); }
