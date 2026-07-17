/** @file 04_redirect_fallback_characterization.cpp
 *  @brief Characterizes redirect fast-path and fallback URI-store behavior.
 */
#define BOOST_TEST_MODULE HttpRedirectFallbackCharacterization
#include <boost/test/unit_test.hpp>

#include <url_shortener/url_shortener.h>

#include <string>

using namespace url_shortener;
namespace bhttp = boost::beast::http;

namespace {

bhttp::request<bhttp::string_body> make_request(
    const bhttp::verb method,
    const std::string& target,
    const std::string& body = "{}",
    const std::string& content_type = "application/json")
{
    bhttp::request<bhttp::string_body> req{method, target, 11};
    req.set(bhttp::field::host, "short.test");
    req.set(bhttp::field::content_type, content_type);
    req.body() = body;
    req.prepare_payload();
    return req;
}

ServerConfig test_config()
{
    ServerConfig config;
    config.shortener_allow_private_targets = true;
    return config;
}

Link make_link(
    const std::string& slug,
    const RedirectType redirect_type = RedirectType::temporary)
{
    Link link;
    link.id = generateId();
    link.slug = slug;
    link.target_url = "https://example.com/" + slug;
    link.created_at = currentTimestamp();
    link.updated_at = link.created_at;
    link.redirect_type = redirect_type;
    return link;
}

Link create_link(
    const std::string& slug,
    const RedirectType redirect_type = RedirectType::temporary)
{
    auto link = make_link(slug, redirect_type);
    BOOST_REQUIRE(linkRepository().create(link));
    linkCache().erase(slug);
    return link;
}

void expect_error(
    const bhttp::response<bhttp::string_body>& res,
    const int status,
    const std::string& code)
{
    BOOST_TEST(res.result_int() == status);
    BOOST_TEST(res.body().find("\"code\":\"" + code + "\"")
               != std::string::npos);
    BOOST_TEST(!res.base()["X-Request-Id"].empty());
}

void expect_redirect(
    const bhttp::response<bhttp::string_body>& res,
    const int status,
    const std::string& location)
{
    BOOST_TEST(res.result_int() == status);
    BOOST_TEST(std::string(res[bhttp::field::location]) == location);
    BOOST_TEST(!res.keep_alive());
    BOOST_TEST(res.body().find("\"slug\"") == std::string::npos);
    BOOST_TEST(!res.base()["X-Request-Id"].empty());
}

} // namespace

/**
 * [Unit][HTTP] Prefixed redirects preserve status, Location, and counters.
 */
BOOST_AUTO_TEST_CASE(prefixed_redirect_success_cases)
{
    const auto config = test_config();
    create_link("rftemp001", RedirectType::temporary);
    create_link("rfperm001", RedirectType::permanent);

    const auto temporary = handleShortenerRequest(
        make_request(bhttp::verb::get, "/r/rftemp001"), config, false);
    expect_redirect(temporary, 302, "https://example.com/rftemp001");

    const auto permanent = handleShortenerRequest(
        make_request(bhttp::verb::get, "/r/rfperm001"), config, false);
    expect_redirect(permanent, 301, "https://example.com/rfperm001");

    const auto updated = linkRepository().getBySlug("rftemp001");
    BOOST_REQUIRE(updated.has_value());
    BOOST_TEST(updated->stats.total_redirects == 1U);
    BOOST_TEST(updated->stats.redirects_24h == 1U);
    BOOST_TEST(updated->stats.redirects_7d == 1U);
    BOOST_TEST(updated->stats.last_accessed_at.has_value());
}

/**
 * [Unit][HTTP] Root redirects preserve success behavior and counter updates.
 */
BOOST_AUTO_TEST_CASE(root_redirect_success_case)
{
    const auto config = test_config();
    create_link("rfroot001", RedirectType::temporary);

    const auto res = handleShortenerRequest(
        make_request(bhttp::verb::get, "/rfroot001"), config, false);
    expect_redirect(res, 302, "https://example.com/rfroot001");

    const auto updated = linkRepository().getBySlug("rfroot001");
    BOOST_REQUIRE(updated.has_value());
    BOOST_TEST(updated->stats.total_redirects == 1U);
}

/**
 * [Unit][HTTP] Redirect terminal states keep current error codes.
 */
BOOST_AUTO_TEST_CASE(redirect_terminal_states)
{
    const auto config = test_config();

    auto deleted = create_link("rfdeleted001");
    deleted.deleted_at = currentTimestamp();
    BOOST_REQUIRE(updateLinkAndInvalidateCache(deleted));

    auto disabled = create_link("rfdisabled001");
    disabled.enabled = false;
    BOOST_REQUIRE(updateLinkAndInvalidateCache(disabled));

    auto expired = create_link("rfexpired001");
    expired.expires_at = "2000-01-01T00:00:00Z";
    BOOST_REQUIRE(updateLinkAndInvalidateCache(expired));

    expect_error(
        handleShortenerRequest(
            make_request(bhttp::verb::get, "/r/rfdeleted001"),
            config,
            false),
        404,
        "link_deleted");

    expect_error(
        handleShortenerRequest(
            make_request(bhttp::verb::get, "/r/rfdisabled001"),
            config,
            false),
        410,
        "link_disabled");

    expect_error(
        handleShortenerRequest(
            make_request(bhttp::verb::get, "/r/rfexpired001"),
            config,
            false),
        410,
        "link_expired");
}

/**
 * [Unit][HTTP] Missing and invalid prefixed slugs keep current not-found shape.
 */
BOOST_AUTO_TEST_CASE(prefixed_redirect_missing_and_invalid_slug)
{
    const auto config = test_config();

    expect_error(
        handleShortenerRequest(
            make_request(bhttp::verb::get, "/r/rfmissing001"),
            config,
            false),
        404,
        "not_found");

    expect_error(
        handleShortenerRequest(
            make_request(bhttp::verb::get, "/r/bad.slug"),
            config,
            false),
        404,
        "not_found");
}

/**
 * [Unit][HTTP] Fallback URI store keeps read/write/delete behavior.
 */
BOOST_AUTO_TEST_CASE(fallback_uri_store_roundtrip)
{
    const auto config = test_config();

    auto post = handleShortenerRequest(
        make_request(
            bhttp::verb::post,
            "/fallback.path",
            "stored text",
            "text/plain"),
        config,
        false);
    BOOST_TEST(post.result_int() == 200);
    BOOST_TEST(post.body() == "URI saved\n");

    auto get = handleShortenerRequest(
        make_request(bhttp::verb::get, "/fallback.path"), config, false);
    BOOST_TEST(get.result_int() == 200);
    BOOST_TEST(get.body() == "stored text\n");
    BOOST_TEST(std::string(get[bhttp::field::content_type]) == "text/plain");

    auto remove = handleShortenerRequest(
        make_request(bhttp::verb::delete_, "/fallback.path"), config, false);
    BOOST_TEST(remove.result_int() == 200);
    BOOST_TEST(remove.body() == "Data deleted\n");

    auto after_delete = handleShortenerRequest(
        make_request(bhttp::verb::get, "/fallback.path"), config, false);
    BOOST_TEST(after_delete.result_int() == 404);
    BOOST_TEST(after_delete.body() == "URI not found\n");
}

/**
 * [Unit][HTTP] Root fallback behavior remains distinct from root redirects.
 */
BOOST_AUTO_TEST_CASE(root_fallback_behavior)
{
    const auto config = test_config();

    auto root_get = handleShortenerRequest(
        make_request(bhttp::verb::get, "/"), config, false);
    BOOST_TEST(root_get.result_int() == 404);
    BOOST_TEST(root_get.body() == "URI not found\n");

    auto root_post = handleShortenerRequest(
        make_request(bhttp::verb::post, "/", "root body", "text/plain"),
        config,
        false);
    BOOST_TEST(root_post.result_int() == 200);
    BOOST_TEST(root_post.body() == "URI saved\n");

    auto root_get_after_post = handleShortenerRequest(
        make_request(bhttp::verb::get, "/"), config, false);
    BOOST_TEST(root_get_after_post.result_int() == 200);
    BOOST_TEST(root_get_after_post.body() == "root body\n");
}

/**
 * [Unit][HTTP] Root redirect intentionally falls through for non-GET and
 * invalid-slug targets.
 */
BOOST_AUTO_TEST_CASE(root_redirect_fallthrough_cases)
{
    const auto config = test_config();

    auto non_get = handleShortenerRequest(
        make_request(bhttp::verb::post, "/rfpostfallback", "body"),
        config,
        false);
    BOOST_TEST(non_get.result_int() == 200);
    BOOST_TEST(non_get.body() == "URI saved\n");

    auto invalid_slug = handleShortenerRequest(
        make_request(bhttp::verb::get, "/bad.slug"), config, false);
    BOOST_TEST(invalid_slug.result_int() == 404);
    BOOST_TEST(invalid_slug.body() == "URI not found\n");
}
