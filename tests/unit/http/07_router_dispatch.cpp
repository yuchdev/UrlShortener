#define BOOST_TEST_MODULE RouterDispatch
#include <boost/test/unit_test.hpp>

#include <url_shortener/http/Router.hpp>
#include <url_shortener/http/RouterBuilder.hpp>
#include <url_shortener/core/config.h>
#include <url_shortener/http/request_handlers.h>

namespace bhttp = boost::beast::http;
using url_shortener::http::BeastRequest;
using url_shortener::http::Router;
using url_shortener::http::RouterBuilder;
using url_shortener::http::RouteContext;

namespace {

BeastRequest make_request(const bhttp::verb method, const std::string& target)
{
    BeastRequest req{method, target, 11};
    req.set(bhttp::field::host, "short.test");
    req.prepare_payload();
    return req;
}

url_shortener::http::HandlerFn text_handler(const std::string& body)
{
    return [body](
               const BeastRequest& req,
               const ::ServerConfig& config,
               const bool is_tls,
               const RouteContext&) {
        return url_shortener::makeResponse(req, config, is_tls, 200, body, "text/plain");
    };
}

url_shortener::http::ErrorHandlerFn application_error_handler()
{
    return [](
               const BeastRequest& req,
               const ::ServerConfig& config,
               const bool is_tls,
               const unsigned status,
               const std::string& code,
               const std::string& message) {
        return url_shortener::makeApiErrorResponse(
            req,
            config,
            is_tls,
            status,
            code,
            message);
    };
}

} // namespace

BOOST_AUTO_TEST_CASE(first_match_wins)
{
    Router router{application_error_handler()};
    router.add(bhttp::verb::get, "/{slug}", "redirect_root", text_handler("first"));
    router.add(bhttp::verb::get, "/{path}", "redirect_root", text_handler("second"));

    const auto res = router.dispatch(make_request(bhttp::verb::get, "/docs"), ServerConfig{}, false);
    BOOST_TEST(res.result_int() == 200);
    BOOST_TEST(res.body() == "first");
}

BOOST_AUTO_TEST_CASE(specific_patterns_can_precede_general_patterns)
{
    Router router{application_error_handler()};
    router.add(bhttp::verb::get, "/api/v1/links/id/{id}", "api_links", text_handler("id"));
    router.add(bhttp::verb::get, "/api/v1/links/{slug}", "api_links", text_handler("slug"));

    const auto res = router.dispatch(make_request(bhttp::verb::get, "/api/v1/links/id/abc"), ServerConfig{}, false);
    BOOST_TEST(res.result_int() == 200);
    BOOST_TEST(res.body() == "id");
}

BOOST_AUTO_TEST_CASE(specificity_beats_registration_order)
{
    Router router{application_error_handler()};
    router.add(bhttp::verb::get, "/{id}", "redirect_root", text_handler("capture"));
    router.add(bhttp::verb::get, "/fixed", "app", text_handler("fixed"));

    const auto res = router.dispatch(make_request(bhttp::verb::get, "/fixed"), ServerConfig{}, false);
    BOOST_TEST(res.result_int() == 200);
    BOOST_TEST(res.body() == "fixed");
}

BOOST_AUTO_TEST_CASE(path_wildcard_matches_nested_paths)
{
    Router router{application_error_handler()};
    router.add(bhttp::verb::get, "/{path}", "redirect_root", text_handler("fallback"));

    const auto res = router.dispatch(make_request(bhttp::verb::get, "/nested/fallback/path"), ServerConfig{}, false);
    BOOST_TEST(res.result_int() == 200);
    BOOST_TEST(res.body() == "fallback");
}

BOOST_AUTO_TEST_CASE(redirect_prefix_can_precede_root_slug)
{
    Router router;
    router.add(bhttp::verb::get, "/r/{slug}", "redirect_prefixed", text_handler("prefixed"));
    router.add(bhttp::verb::get, "/{slug}", "redirect_root", text_handler("root"));

    const auto res = router.dispatch(make_request(bhttp::verb::get, "/r/docs"), ServerConfig{}, false);
    BOOST_TEST(res.result_int() == 200);
    BOOST_TEST(res.body() == "prefixed");
}

BOOST_AUTO_TEST_CASE(multiple_allowed_methods_return_unsupported_operation)
{
    Router router{application_error_handler()};
    router.add(bhttp::verb::get, "/{path}", "redirect_root", text_handler("get"));
    router.add(bhttp::verb::post, "/{path}", "redirect_root", text_handler("post"));
    router.add(bhttp::verb::delete_, "/{path}", "redirect_root", text_handler("delete"));

    const auto res = router.dispatch(make_request(bhttp::verb::patch, "/stored-path"), ServerConfig{}, false);
    BOOST_TEST(res.result_int() == 400);
    BOOST_TEST(res.body().find("\"code\":\"invalid_method\"") != std::string::npos);
    BOOST_TEST(res.body().find("Unsupported operation") != std::string::npos);
}

BOOST_AUTO_TEST_CASE(method_mismatch_returns_current_invalid_method_shape)
{
    Router router{application_error_handler()};
    router.add(bhttp::verb::get, "/healthz", "healthz", text_handler("ok"));

    const auto res = router.dispatch(make_request(bhttp::verb::post, "/healthz"), ServerConfig{}, false);
    BOOST_TEST(res.result_int() == 400);
    BOOST_TEST(res.body().find("\"code\":\"invalid_method\"") != std::string::npos);
    BOOST_TEST(res.body().find("Only GET is supported") != std::string::npos);
    BOOST_TEST(!res.base()["X-Request-Id"].empty());
}

BOOST_AUTO_TEST_CASE(no_match_returns_not_found)
{
    Router router{application_error_handler()};
    router.add(bhttp::verb::get, "/healthz", "healthz", text_handler("ok"));

    const auto res = router.dispatch(make_request(bhttp::verb::get, "/missing/path"), ServerConfig{}, false);
    BOOST_TEST(res.result_int() == 404);
    BOOST_TEST(res.body().find("\"code\":\"not_found\"") != std::string::npos);
    BOOST_TEST(!res.base()["X-Request-Id"].empty());
}

BOOST_AUTO_TEST_CASE(application_router_dispatches_observability_handlers)
{
    const auto router = RouterBuilder::buildApplicationRouter();

    const auto health = router.dispatch(
        make_request(bhttp::verb::get, "/healthz"),
        ServerConfig{},
        false);
    BOOST_TEST(health.result_int() == 200);
    BOOST_TEST(health.body() == "ok\n");
    BOOST_TEST(std::string(health[bhttp::field::content_type]) == "text/plain");

    const auto ready = router.dispatch(
        make_request(bhttp::verb::get, "/readyz"),
        ServerConfig{},
        false);
    BOOST_TEST(ready.result_int() == 200);
    BOOST_TEST(ready.body() == "ok\n");
    BOOST_TEST(std::string(ready[bhttp::field::content_type]) == "text/plain");

    const auto metrics = router.dispatch(
        make_request(bhttp::verb::get, "/metrics"),
        ServerConfig{},
        false);
    BOOST_TEST(metrics.result_int() == 200);
    BOOST_TEST(std::string(metrics[bhttp::field::content_type]) == "text/plain");
    BOOST_TEST(metrics.body().find("http_inflight_requests") != std::string::npos);
}

BOOST_AUTO_TEST_CASE(handler_receives_route_context)
{
    Router router;
    router.add(
        bhttp::verb::get,
        "/api/v1/links/{slug}/stats",
        "api_links",
        [](
            const BeastRequest& req,
            const ::ServerConfig& config,
            const bool is_tls,
            const RouteContext& context) {
            return url_shortener::makeResponse(
                req,
                config,
                is_tls,
                200,
                context.path_params.at("slug") + "|" + context.query_string + "|" + context.route_label,
                "text/plain");
        });

    const auto res = router.dispatch(
        make_request(bhttp::verb::get, "/api/v1/links/docs/stats?from=1&to=2"),
        ServerConfig{},
        false);
    BOOST_TEST(res.result_int() == 200);
    BOOST_TEST(res.body() == "docs|from=1&to=2|api_links");
}
