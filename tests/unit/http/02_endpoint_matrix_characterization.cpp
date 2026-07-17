/** @file 02_endpoint_matrix_characterization.cpp
 *  @brief Characterizes the current public HTTP endpoint matrix.
 */
#define BOOST_TEST_MODULE HttpEndpointMatrixCharacterization
#include <boost/test/unit_test.hpp>

#include <url_shortener/http/RouteRegistry.hpp>
#include <url_shortener/url_shortener.h>

#include <algorithm>
#include <string>

using namespace url_shortener;
namespace bhttp = boost::beast::http;
namespace route_http = url_shortener::http;

namespace {

bhttp::request<bhttp::string_body> make_request(
    const bhttp::verb method,
    const std::string& target,
    const std::string& body = "{}")
{
    bhttp::request<bhttp::string_body> req{method, target, 11};
    req.set(bhttp::field::host, "short.test");
    req.set(bhttp::field::content_type, "application/json");
    req.body() = body;
    req.prepare_payload();
    return req;
}

ServerConfig test_config()
{
    ServerConfig config;
    config.shortener_allow_private_targets = true;
    config.shortener_generated_slug_length = 8;
    return config;
}

Link make_link(
    const std::string& slug,
    const std::string& target_url = "https://example.com/docs",
    const RedirectType redirect_type = RedirectType::temporary)
{
    Link link;
    link.id = generateId();
    link.slug = slug;
    link.target_url = target_url;
    link.created_at = currentTimestamp();
    link.updated_at = link.created_at;
    link.redirect_type = redirect_type;
    return link;
}

Link create_link(
    const std::string& slug,
    const RedirectType redirect_type = RedirectType::temporary)
{
    auto link = make_link(slug, "https://example.com/" + slug, redirect_type);
    BOOST_REQUIRE(linkRepository().create(link));
    linkCache().erase(slug);
    return link;
}

bool has_header(const bhttp::response<bhttp::string_body>& res, const char* name)
{
    return !res.base()[name].empty();
}

void expect_request_id(const bhttp::response<bhttp::string_body>& res)
{
    BOOST_TEST(has_header(res, "X-Request-Id"));
}

void expect_content_type(
    const bhttp::response<bhttp::string_body>& res,
    const std::string& expected)
{
    BOOST_TEST(std::string(res[bhttp::field::content_type]) == expected);
}

bool registry_has_route(const std::string& method, const std::string& pattern)
{
    const auto& routes = route_http::registeredRoutes();
    return std::any_of(routes.begin(), routes.end(), [&](const auto& route) {
        return route.method == method && route.path_pattern == pattern;
    });
}

} // namespace

/**
 * [Unit][HTTP] Observability endpoints keep their public response shape.
 *
 * Scenario:
 *   Given the current Beast request handler.
 *   When health, readiness, and metrics endpoints are requested.
 *   Then status, content type, request id, and labels match the baseline.
 */
BOOST_AUTO_TEST_CASE(observability_endpoints)
{
    const auto config = test_config();

    auto health = handleShortenerRequest(
        make_request(bhttp::verb::get, "/healthz"), config, false);
    BOOST_TEST(health.result_int() == 200);
    BOOST_TEST(health.body() == "ok\n");
    expect_content_type(health, "text/plain");
    expect_request_id(health);
    BOOST_TEST(routeLabelFor("/healthz") == "healthz");

    auto ready = handleShortenerRequest(
        make_request(bhttp::verb::get, "/readyz"), config, false);
    BOOST_TEST(ready.result_int() == 200);
    BOOST_TEST(ready.body() == "ok\n");
    expect_content_type(ready, "text/plain");
    expect_request_id(ready);
    BOOST_TEST(routeLabelFor("/readyz") == "readyz");

    auto metrics = handleShortenerRequest(
        make_request(bhttp::verb::get, "/metrics"), config, false);
    BOOST_TEST(metrics.result_int() == 200);
    expect_content_type(metrics, "text/plain");
    expect_request_id(metrics);
    BOOST_TEST(routeLabelFor("/metrics") == "metrics");
    BOOST_TEST(metrics.body().find("http_inflight_requests")
               != std::string::npos);
}

/**
 * [Unit][HTTP] Canonical link management endpoints keep baseline behavior.
 *
 * Scenario:
 *   Given existing links and a create request.
 *   When canonical /api/v1/links endpoints are called.
 *   Then each endpoint returns its current status and representative payload.
 */
BOOST_AUTO_TEST_CASE(canonical_link_endpoint_matrix)
{
    const auto config = test_config();
    const auto read_link = create_link("mxread001");
    create_link("mxpatch001");
    create_link("mxdelete001");
    create_link("mxpreview001");
    create_link("mxstats001");
    create_link("mxenable001");
    create_link("mxdisable001");
    create_link("mxrestore001");

    auto create_res = handleShortenerRequest(
        make_request(
            bhttp::verb::post,
            "/api/v1/links",
            "{\"url\":\"https://example.com/new\",\"slug\":\"mxcreate001\"}"),
        config,
        false);
    BOOST_TEST(create_res.result_int() == 201);
    expect_content_type(create_res, "application/json");
    expect_request_id(create_res);
    BOOST_TEST(create_res.body().find("\"slug\":\"mxcreate001\"")
               != std::string::npos);

    auto by_id = handleShortenerRequest(
        make_request(bhttp::verb::get, "/api/v1/links/id/" + read_link.id),
        config,
        false);
    BOOST_TEST(by_id.result_int() == 200);
    BOOST_TEST(by_id.body().find("\"slug\":\"mxread001\"")
               != std::string::npos);
    expect_content_type(by_id, "application/json");
    expect_request_id(by_id);

    auto by_slug = handleShortenerRequest(
        make_request(bhttp::verb::get, "/api/v1/links/mxread001"),
        config,
        false);
    BOOST_TEST(by_slug.result_int() == 200);
    BOOST_TEST(by_slug.body().find("\"slug\":\"mxread001\"")
               != std::string::npos);
    expect_content_type(by_slug, "application/json");

    auto patch = handleShortenerRequest(
        make_request(
            bhttp::verb::patch,
            "/api/v1/links/mxpatch001",
            "{\"enabled\":false,\"tags\":[\"docs\"]}"),
        config,
        false);
    BOOST_TEST(patch.result_int() == 200);
    BOOST_TEST(patch.body().find("\"status\":\"disabled\"")
               != std::string::npos);
    expect_content_type(patch, "application/json");

    auto deleted = handleShortenerRequest(
        make_request(bhttp::verb::delete_, "/api/v1/links/mxdelete001"),
        config,
        false);
    BOOST_TEST(deleted.result_int() == 200);
    BOOST_TEST(deleted.body().find("\"deleted_at\":null")
               == std::string::npos);
    expect_content_type(deleted, "application/json");

    auto preview = handleShortenerRequest(
        make_request(bhttp::verb::get, "/api/v1/links/mxpreview001/preview"),
        config,
        false);
    BOOST_TEST(preview.result_int() == 200);
    BOOST_TEST(preview.body().find("\"status\":\"active\"")
               != std::string::npos);
    expect_content_type(preview, "application/json");

    auto qr = handleShortenerRequest(
        make_request(bhttp::verb::get, "/api/v1/links/mxpreview001/qr"),
        config,
        false);
    BOOST_TEST(qr.result_int() == 501);
    BOOST_TEST(qr.body().find("feature_not_enabled") != std::string::npos);
    expect_content_type(qr, "application/json");

    auto routing = handleShortenerRequest(
        make_request(bhttp::verb::get, "/api/v1/links/mxpreview001/routing"),
        config,
        false);
    BOOST_TEST(routing.result_int() == 501);
    BOOST_TEST(routing.body().find("feature_not_enabled")
               != std::string::npos);

    auto stats = handleShortenerRequest(
        make_request(bhttp::verb::get, "/api/v1/links/mxstats001/stats"),
        config,
        false);
    BOOST_TEST(stats.result_int() == 200);
    BOOST_TEST(stats.body().find("\"total_redirects\":0")
               != std::string::npos);

    auto enable = handleShortenerRequest(
        make_request(bhttp::verb::post, "/api/v1/links/mxenable001/enable"),
        config,
        false);
    BOOST_TEST(enable.result_int() == 200);
    BOOST_TEST(enable.body().find("\"status\":\"active\"") != std::string::npos);

    auto disable = handleShortenerRequest(
        make_request(bhttp::verb::post, "/api/v1/links/mxdisable001/disable"),
        config,
        false);
    BOOST_TEST(disable.result_int() == 200);
    BOOST_TEST(disable.body().find("\"status\":\"disabled\"")
               != std::string::npos);

    auto soft_deleted = create_link("mxrestore001-deleted");
    soft_deleted.deleted_at = currentTimestamp();
    BOOST_REQUIRE(updateLinkAndInvalidateCache(soft_deleted));
    auto restore = handleShortenerRequest(
        make_request(
            bhttp::verb::post,
            "/api/v1/links/mxrestore001-deleted/restore"),
        config,
        false);
    BOOST_TEST(restore.result_int() == 200);
    BOOST_TEST(restore.body().find("\"status\":\"active\"")
               != std::string::npos);

    BOOST_TEST(routeLabelFor("/api/v1/links/mxstats001/stats")
               == "api_links");
}

/**
 * [Unit][HTTP] Compatibility endpoints keep baseline behavior.
 */
BOOST_AUTO_TEST_CASE(compatibility_endpoint_matrix)
{
    const auto config = test_config();
    create_link("mxcompatread001");

    auto create_res = handleShortenerRequest(
        make_request(
            bhttp::verb::post,
            "/api/v1/short-urls",
            "{\"url\":\"https://example.com/compat\","
            "\"code\":\"mxcompatcreate001\"}"),
        config,
        false);
    BOOST_TEST(create_res.result_int() == 201);
    BOOST_TEST(create_res.body().find("\"slug\":\"mxcompatcreate001\"")
               != std::string::npos);
    expect_content_type(create_res, "application/json");
    expect_request_id(create_res);

    auto read_res = handleShortenerRequest(
        make_request(bhttp::verb::get, "/api/v1/short-urls/mxcompatread001"),
        config,
        false);
    BOOST_TEST(read_res.result_int() == 200);
    BOOST_TEST(read_res.body().find("\"slug\":\"mxcompatread001\"")
               != std::string::npos);
    expect_content_type(read_res, "application/json");
    BOOST_TEST(routeLabelFor("/api/v1/short-urls/mxcompatread001")
               == "api_links");
}

/**
 * [Unit][HTTP] Redirect and fallback endpoints remain present in the matrix.
 */
BOOST_AUTO_TEST_CASE(redirect_and_fallback_endpoint_matrix)
{
    const auto config = test_config();
    create_link("mxredir001");
    create_link("mxroot001");

    auto prefixed = handleShortenerRequest(
        make_request(bhttp::verb::get, "/r/mxredir001"), config, false);
    BOOST_TEST(prefixed.result_int() == 302);
    BOOST_TEST(!prefixed[bhttp::field::location].empty());
    BOOST_TEST(routeLabelFor("/r/mxredir001") == "redirect_prefixed");

    auto root = handleShortenerRequest(
        make_request(bhttp::verb::get, "/mxroot001"), config, false);
    BOOST_TEST(root.result_int() == 302);
    BOOST_TEST(!root[bhttp::field::location].empty());
    BOOST_TEST(routeLabelFor("/mxroot001") == "redirect_root");

    auto store = handleShortenerRequest(
        make_request(
            bhttp::verb::post,
            "/mx.store.path",
            "stored body"),
        config,
        false);
    BOOST_TEST(store.result_int() == 200);
    expect_content_type(store, "text/plain");

    auto fetch = handleShortenerRequest(
        make_request(bhttp::verb::get, "/mx.store.path"), config, false);
    BOOST_TEST(fetch.result_int() == 200);
    BOOST_TEST(fetch.body() == "stored body\n");

    auto remove = handleShortenerRequest(
        make_request(bhttp::verb::delete_, "/mx.store.path"), config, false);
    BOOST_TEST(remove.result_int() == 200);
}

/**
 * [Unit][HTTP] Route registry lists every route exercised by the matrix.
 */
BOOST_AUTO_TEST_CASE(route_registry_covers_endpoint_matrix)
{
    BOOST_TEST(registry_has_route("GET", "/healthz"));
    BOOST_TEST(registry_has_route("GET", "/readyz"));
    BOOST_TEST(registry_has_route("GET", "/metrics"));
    BOOST_TEST(registry_has_route("POST", "/api/v1/links"));
    BOOST_TEST(registry_has_route("GET", "/api/v1/links/id/{id}"));
    BOOST_TEST(registry_has_route("GET", "/api/v1/links/{slug}"));
    BOOST_TEST(registry_has_route("PATCH", "/api/v1/links/{slug}"));
    BOOST_TEST(registry_has_route("DELETE", "/api/v1/links/{slug}"));
    BOOST_TEST(registry_has_route("GET", "/api/v1/links/{slug}/preview"));
    BOOST_TEST(registry_has_route("GET", "/api/v1/links/{slug}/qr"));
    BOOST_TEST(registry_has_route("GET", "/api/v1/links/{slug}/routing"));
    BOOST_TEST(registry_has_route("GET", "/api/v1/links/{slug}/stats"));
    BOOST_TEST(registry_has_route("POST", "/api/v1/links/{slug}/enable"));
    BOOST_TEST(registry_has_route("POST", "/api/v1/links/{slug}/disable"));
    BOOST_TEST(registry_has_route("POST", "/api/v1/links/{slug}/restore"));
    BOOST_TEST(registry_has_route("POST", "/api/v1/short-urls"));
    BOOST_TEST(registry_has_route("GET", "/api/v1/short-urls/{slug}"));
    BOOST_TEST(registry_has_route("GET", "/r/{slug}"));
    BOOST_TEST(registry_has_route("GET", "/{slug}"));
    BOOST_TEST(registry_has_route("GET", "/{path}"));
    BOOST_TEST(registry_has_route("POST", "/{path}"));
    BOOST_TEST(registry_has_route("DELETE", "/{path}"));
}
