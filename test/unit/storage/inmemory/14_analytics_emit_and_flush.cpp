#define BOOST_TEST_MODULE InMemoryAnalyticsEmitFlushTest
#include <boost/test/unit_test.hpp>
#include "url_shortener/storage/inmemory/InMemoryAnalyticsSink.hpp"
BOOST_AUTO_TEST_CASE(emit_and_flush_and_order)
{
    InMemoryAnalyticsSink sink;
    sink.Emit({"a", std::chrono::system_clock::time_point{}, 302, false, std::nullopt});
    sink.Emit({"b", std::chrono::system_clock::time_point{}, 404, false, std::nullopt});
    auto snapshot = sink.Snapshot();
    BOOST_TEST(snapshot.size() == 2);
    BOOST_TEST(snapshot[0].short_code == "a");
    BOOST_TEST(snapshot[1].short_code == "b");
    BOOST_TEST(sink.Flush());
}
