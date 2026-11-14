#define BOOST_TEST_MODULE ClickEventListEventsDefault
#include <vector>

#include <boost/test/unit_test.hpp>

#include "url_shortener/analytics/i_click_event_repository.hpp"

using namespace url_shortener::analytics;

namespace
{

/**
 * @brief Minimal `IClickEventRepository` that implements only the three pure
 * methods and deliberately does NOT override `ListEvents`, so it exercises the
 * interface's default "unsupported" body.
 */
struct NoListRepo final : IClickEventRepository
{
    bool InsertBatch(const std::vector<ClickEvent>&, AnalyticsError*) override
    {
        return true;
    }
    bool GetAggregateStats(const AggregateQuery&,
                           AggregateStats*,
                           AnalyticsError*) override
    {
        return true;
    }
    bool DeleteOlderThan(Timestamp, AnalyticsError*) override { return true; }
};

}  // namespace

/**
 * Scenario: a repository that does not override `ListEvents` falls back to the
 * interface default, which reports failure with the repository error code and
 * the "not supported" message.
 * On failure first check: that `ListEvents` was left non-pure (default body
 * present) and that the fake does not override it.
 */
BOOST_AUTO_TEST_CASE(default_list_events_reports_unsupported)
{
    NoListRepo repo;
    ClickEventQuery q;
    ClickEventPage page;
    AnalyticsError err;
    BOOST_TEST(!repo.ListEvents(q, &page, &err));
    BOOST_TEST((err.code == AnalyticsErrorCode::repository));
    BOOST_TEST(err.message == "ListEvents is not supported by this repository");
}

/**
 * Scenario: the default `ListEvents` is safe to call with a null `error`
 * out-param.
 */
BOOST_AUTO_TEST_CASE(default_list_events_null_error_is_safe)
{
    NoListRepo repo;
    ClickEventQuery q;
    ClickEventPage page;
    BOOST_TEST(!repo.ListEvents(q, &page, nullptr));
}
