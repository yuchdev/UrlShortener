#define BOOST_TEST_MODULE RedirectAnalyticsHook
#include <boost/test/unit_test.hpp>
#include "url_shortener/http/RedirectAnalyticsHook.hpp"

using namespace url_shortener;
using namespace url_shortener::analytics;
using namespace url_shortener::http;

class Q final : public IClickEventQueue { public: ClickEvent last; int count=0; EnqueueResult TryEnqueue(ClickEvent e) noexcept override { last=std::move(e); ++count; return EnqueueResult::Enqueued; } bool TryDequeue(ClickEvent&) noexcept override { return false; } QueueStats GetStats() const noexcept override { return {}; } };

BOOST_AUTO_TEST_CASE(extracts_and_never_throws)
{
    AnalyticsConfig cfg; cfg.enabled=true; cfg.client_hash_salt="salt";
    Q q; NullAnalyticsMetrics m; AnalyticsService svc(cfg,q,m); RedirectAnalyticsHook hook(svc);
    RedirectAnalyticsInput in; in.slug="slug"; in.link_id="id"; in.host="example.com"; in.referrer=""; in.user_agent="ua"; in.client_network_identifier="1.2.3.4"; in.status_code=404;
    BOOST_CHECK_NO_THROW(hook.OnRedirectOutcome(in));
    BOOST_TEST(q.count==1);
    BOOST_TEST(q.last.slug=="slug");
    BOOST_TEST(q.last.domain=="example.com");
    BOOST_TEST(q.last.status_code==404);
}
