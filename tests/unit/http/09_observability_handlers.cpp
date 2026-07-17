#define BOOST_TEST_MODULE ObservabilityHandlers
#include <boost/test/unit_test.hpp>

#include <url_shortener/http/handlers/ObservabilityHandlers.hpp>

namespace bhttp = boost::beast::http;
using url_shortener::http::BeastRequest;
using url_shortener::http::RouteContext;

namespace {

BeastRequest make_request(const std::string& target)
{
    BeastRequest req{bhttp::verb::get, target, 11};
    req.set(bhttp::field::host, "short.test");
    req.prepare_payload();
    return req;
}

void expect_text_response(
    const url_shortener::http::BeastResponse& res,
    const std::string& body)
{
    BOOST_TEST(res.result_int() == 200);
    BOOST_TEST(res.body() == body);
    BOOST_TEST(std::string(res[bhttp::field::content_type]) == "text/plain");
    BOOST_TEST(!res.base()["X-Request-Id"].empty());
}

} // namespace

BOOST_AUTO_TEST_CASE(healthz_returns_current_response_shape)
{
    const auto res = url_shortener::http::handleHealthz(
        make_request("/healthz"),
        ServerConfig{},
        false,
        RouteContext{});

    expect_text_response(res, "ok\n");
}

BOOST_AUTO_TEST_CASE(readyz_returns_current_response_shape)
{
    const auto res = url_shortener::http::handleReadyz(
        make_request("/readyz"),
        ServerConfig{},
        false,
        RouteContext{});

    expect_text_response(res, "ok\n");
}

BOOST_AUTO_TEST_CASE(metrics_returns_current_response_shape)
{
    const auto res = url_shortener::http::handleMetrics(
        make_request("/metrics"),
        ServerConfig{},
        false,
        RouteContext{});

    BOOST_TEST(res.result_int() == 200);
    BOOST_TEST(std::string(res[bhttp::field::content_type]) == "text/plain");
    BOOST_TEST(!res.base()["X-Request-Id"].empty());
    BOOST_TEST(res.body().find("http_inflight_requests") != std::string::npos);
}
