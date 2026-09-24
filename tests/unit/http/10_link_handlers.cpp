#define BOOST_TEST_MODULE LinkHandlers
#include <boost/test/unit_test.hpp>

#include <string>
#include <vector>

#include <url_shortener/core/config.h>
#include <url_shortener/core/utils.h>
#include <url_shortener/http/router_builder.hpp>
#include <url_shortener/storage/link_repository.h>

namespace bhttp = boost::beast::http;
using url_shortener::Link;
using url_shortener::RedirectType;
using url_shortener::http::BeastRequest;
using url_shortener::http::RouterBuilder;

namespace {

BeastRequest make_request(
    const bhttp::verb method,
    const std::string& target,
    const std::string& body = "{}")
{
    BeastRequest req{method, target, 11};
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

Link create_link(
    const std::string& slug,
    const RedirectType redirect_type = RedirectType::temporary)
{
    Link link;
    link.id = url_shortener::generateId();
    link.slug = slug;
    link.target_url = "https://example.com/" + slug;
    link.created_at = url_shortener::currentTimestamp();
    link.updated_at = link.created_at;
    link.redirect_type = redirect_type;
    BOOST_REQUIRE(url_shortener::linkRepository().create(link));
    url_shortener::linkCache().erase(slug);
    return link;
}

boost::beast::http::response<boost::beast::http::string_body> dispatch(
    const BeastRequest& req)
{
    const auto router = RouterBuilder::buildApplicationRouter();
    return router.dispatch(req, test_config(), false);
}

boost::beast::http::response<boost::beast::http::string_body> dispatch_with_config(
    const BeastRequest& req,
    const ServerConfig& config)
{
    const auto router = RouterBuilder::buildApplicationRouter();
    return router.dispatch(req, config, false);
}

void expect_api_error(
    const boost::beast::http::response<boost::beast::http::string_body>& res,
    const int status,
    const std::string& code)
{
    BOOST_TEST(res.result_int() == status);
    BOOST_TEST(res.body().find("\"code\":\"" + code + "\"") != std::string::npos);
    BOOST_TEST(!res.base()["X-Request-Id"].empty());
}

// Assert that the given substrings appear in `body` in the exact order
// listed, with each subsequent match starting after the previous one. Used to
// lock in JSON field ordering as a characterization guard against the
// upcoming command-layer migration (Milestone 0003, Task 01.0, subtask 04).
void expect_ordered_fields(
    const std::string& body,
    const std::vector<std::string>& tokens)
{
    std::string::size_type cursor = 0;
    for (const auto& token : tokens) {
        const auto pos = body.find(token, cursor);
        BOOST_TEST(
            (pos != std::string::npos),
            "missing/out-of-order token '" << token << "' in body: " << body);
        if (pos == std::string::npos) {
            return;
        }
        cursor = pos + token.size();
    }
}

} // namespace

BOOST_AUTO_TEST_CASE(create_custom_slug_and_read_by_id_and_slug)
{
    auto create = dispatch(make_request(
        bhttp::verb::post,
        "/api/v1/links",
        "{\"url\":\"https://example.com/new\",\"slug\":\"lhcreate001\"}"));
    BOOST_TEST(create.result_int() == 201);
    BOOST_TEST(create.body().find("\"slug\":\"lhcreate001\"") != std::string::npos);
    BOOST_TEST(std::string(create[bhttp::field::content_type]) == "application/json");

    const auto stored = url_shortener::linkRepository().getBySlug("lhcreate001");
    BOOST_REQUIRE(stored.has_value());

    auto by_id = dispatch(make_request(
        bhttp::verb::get,
        "/api/v1/links/id/" + stored->id));
    BOOST_TEST(by_id.result_int() == 200);
    BOOST_TEST(by_id.body().find("\"slug\":\"lhcreate001\"") != std::string::npos);

    auto by_slug = dispatch(make_request(
        bhttp::verb::get,
        "/api/v1/links/lhcreate001"));
    BOOST_TEST(by_slug.result_int() == 200);
    BOOST_TEST(by_slug.body().find("\"slug\":\"lhcreate001\"") != std::string::npos);
}

BOOST_AUTO_TEST_CASE(create_generated_slug_and_validation_errors)
{
    auto generated = dispatch(make_request(
        bhttp::verb::post,
        "/api/v1/links",
        "{\"url\":\"https://example.com/generated\"}"));
    BOOST_TEST(generated.result_int() == 201);
    BOOST_TEST(generated.body().find("\"slug\":") != std::string::npos);

    expect_api_error(
        dispatch(make_request(bhttp::verb::post, "/api/v1/links", "{}")),
        400,
        "invalid_url");

    expect_api_error(
        dispatch(make_request(
            bhttp::verb::post,
            "/api/v1/links",
            "{\"url\":\"https://example.com\",\"slug\":\"bad slug\"}")),
        400,
        "invalid_slug");

    expect_api_error(
        dispatch(make_request(
            bhttp::verb::post,
            "/api/v1/links",
            "{\"url\":\"https://example.com\",\"slug\":\"api\"}")),
        409,
        "reserved_slug");

    create_link("lhdupe001");
    expect_api_error(
        dispatch(make_request(
            bhttp::verb::post,
            "/api/v1/links",
            "{\"url\":\"https://example.com\",\"slug\":\"lhdupe001\"}")),
        409,
        "slug_conflict");
}

BOOST_AUTO_TEST_CASE(create_rejects_private_targets_when_disabled)
{
    ServerConfig config;
    config.shortener_allow_private_targets = false;
    config.shortener_generated_slug_length = 8;

    expect_api_error(
        dispatch_with_config(
            make_request(
                bhttp::verb::post,
                "/api/v1/links",
                "{\"url\":\"http://127.0.0.1/internal\",\"slug\":\"lhprivate001\"}"),
            config),
        400,
        "invalid_url");
}

BOOST_AUTO_TEST_CASE(patch_delete_preview_and_actions)
{
    create_link("lhpatch001");
    create_link("lhdelete001");
    create_link("lhpreview001");
    create_link("lhaction001");
    auto deleted = create_link("lhrestore001");
    deleted.deleted_at = url_shortener::currentTimestamp();
    BOOST_REQUIRE(url_shortener::updateLinkAndInvalidateCache(deleted));

    auto patch = dispatch(make_request(
        bhttp::verb::patch,
        "/api/v1/links/lhpatch001",
        "{\"enabled\":false,\"tags\":[\"docs\"],"
        "\"metadata\":{\"team\":\"core\"},"
        "\"campaign\":{\"name\":\"spring\"}}"));
    BOOST_TEST(patch.result_int() == 200);
    BOOST_TEST(patch.body().find("\"status\":\"disabled\"") != std::string::npos);
    BOOST_TEST(patch.body().find("\"docs\"") != std::string::npos);
    BOOST_TEST(patch.body().find("\"team\":\"core\"") != std::string::npos);
    BOOST_TEST(patch.body().find("\"name\":\"spring\"") != std::string::npos);

    expect_api_error(
        dispatch(make_request(
            bhttp::verb::patch,
            "/api/v1/links/lhpatch001",
            "{\"enabled\":\"false\"}")),
        400,
        "invalid_enabled");

    auto remove = dispatch(make_request(
        bhttp::verb::delete_,
        "/api/v1/links/lhdelete001"));
    BOOST_TEST(remove.result_int() == 200);
    BOOST_TEST(remove.body().find("\"status\":\"deleted\"") != std::string::npos);

    auto preview = dispatch(make_request(
        bhttp::verb::get,
        "/api/v1/links/lhpreview001/preview"));
    BOOST_TEST(preview.result_int() == 200);
    BOOST_TEST(preview.body().find("\"status\":\"active\"") != std::string::npos);

    auto disable = dispatch(make_request(
        bhttp::verb::post,
        "/api/v1/links/lhaction001/disable"));
    BOOST_TEST(disable.result_int() == 200);
    BOOST_TEST(disable.body().find("\"status\":\"disabled\"") != std::string::npos);

    auto enable = dispatch(make_request(
        bhttp::verb::post,
        "/api/v1/links/lhaction001/enable"));
    BOOST_TEST(enable.result_int() == 200);
    BOOST_TEST(enable.body().find("\"status\":\"active\"") != std::string::npos);

    auto restore = dispatch(make_request(
        bhttp::verb::post,
        "/api/v1/links/lhrestore001/restore"));
    BOOST_TEST(restore.result_int() == 200);
    BOOST_TEST(restore.body().find("\"status\":\"active\"") != std::string::npos);
}

BOOST_AUTO_TEST_CASE(stats_and_placeholder_routes)
{
    auto link = create_link("lhstats001");
    link.stats.total_redirects = 3;
    link.stats.redirects_24h = 2;
    link.stats.redirects_7d = 3;
    BOOST_REQUIRE(url_shortener::updateLinkAndInvalidateCache(link));
    create_link("lhplaceholder001");

    auto stats = dispatch(make_request(
        bhttp::verb::get,
        "/api/v1/links/lhstats001/stats"));
    BOOST_TEST(stats.result_int() == 200);
    BOOST_TEST(stats.body().find("\"total_redirects\":3") != std::string::npos);

    auto invalid_stats = dispatch(make_request(
        bhttp::verb::get,
        "/api/v1/links/lhstats001/stats?from=x&to=2&bucket=day"));
    BOOST_TEST(invalid_stats.result_int() == 400);
    BOOST_TEST(invalid_stats.body().find("invalid_timestamp") != std::string::npos);
    BOOST_TEST(!invalid_stats.base()["X-Request-Id"].empty());

    auto qr = dispatch(make_request(
        bhttp::verb::get,
        "/api/v1/links/lhplaceholder001/qr"));
    expect_api_error(qr, 501, "feature_not_enabled");

    auto routing = dispatch(make_request(
        bhttp::verb::get,
        "/api/v1/links/lhplaceholder001/routing"));
    expect_api_error(routing, 501, "feature_not_enabled");

    expect_api_error(
        dispatch(make_request(
            bhttp::verb::get,
            "/api/v1/links/lhmissing001/qr")),
        404,
        "not_found");
}

// ---------------------------------------------------------------------------
// Characterization tests (Milestone 0003, Task 01.0, subtask 04).
//
// These lock in the CURRENT observable REST behavior of handlePatchLink,
// handleDeleteLink, handleLifecycleAction (enable/disable/restore) and
// handlePreviewLink BEFORE they are migrated onto LinkCommandService in
// subtasks 02/03. Per shared contract C3 the migration must not change any
// status code, JSON field name, field order, field value or error code. If a
// later change makes one of these fail, that is a behavior regression to fix
// in the migration, never a test to loosen.
// ---------------------------------------------------------------------------

BOOST_AUTO_TEST_CASE(characterize_patch_success_full_body_field_order)
{
    create_link("lhcharpatch01");

    auto patch = dispatch(make_request(
        bhttp::verb::patch,
        "/api/v1/links/lhcharpatch01",
        "{\"enabled\":false,\"tags\":[\"docs\",\"release\"],"
        "\"metadata\":{\"team\":\"core\"},"
        "\"campaign\":{\"name\":\"spring\",\"source\":\"newsletter\"}}"));

    BOOST_TEST(patch.result_int() == 200);
    BOOST_TEST(std::string(patch[bhttp::field::content_type]) == "application/json");

    // Full serialized field order emitted by serializeLink().
    expect_ordered_fields(
        patch.body(),
        {"\"id\":", "\"slug\":", "\"url\":", "\"short_url\":",
         "\"created_at\":", "\"updated_at\":", "\"status\":",
         "\"redirect_type\":", "\"tags\":", "\"metadata\":", "\"campaign\":",
         "\"stats\":"});

    // Deterministic values.
    BOOST_TEST(patch.body().find("\"slug\":\"lhcharpatch01\"") != std::string::npos);
    BOOST_TEST(patch.body().find("\"short_url\":\"http://localhost:8000/lhcharpatch01\"") != std::string::npos);
    BOOST_TEST(patch.body().find("\"status\":\"disabled\"") != std::string::npos);
    BOOST_TEST(patch.body().find("\"redirect_type\":\"temporary\"") != std::string::npos);
    BOOST_TEST(patch.body().find("\"tags\":[\"docs\",\"release\"]") != std::string::npos);
    BOOST_TEST(patch.body().find("\"metadata\":{\"team\":\"core\"}") != std::string::npos);
    BOOST_TEST(patch.body().find("\"campaign\":{\"name\":\"spring\",\"source\":\"newsletter\"}") != std::string::npos);
    BOOST_TEST(patch.body().find("\"stats\":{\"total_redirects\":0,\"redirects_24h\":0,\"redirects_7d\":0,\"last_accessed_at\":null}}") != std::string::npos);
}

BOOST_AUTO_TEST_CASE(characterize_patch_campaign_clearing)
{
    create_link("lhcharpatch02");

    // Establish a campaign first.
    auto set_campaign = dispatch(make_request(
        bhttp::verb::patch,
        "/api/v1/links/lhcharpatch02",
        "{\"campaign\":{\"name\":\"spring\"}}"));
    BOOST_TEST(set_campaign.result_int() == 200);
    BOOST_TEST(set_campaign.body().find("\"campaign\":{\"name\":\"spring\"}") != std::string::npos);

    // Clearing it with null must serialize campaign back to null.
    auto clear_campaign = dispatch(make_request(
        bhttp::verb::patch,
        "/api/v1/links/lhcharpatch02",
        "{\"campaign\":null}"));
    BOOST_TEST(clear_campaign.result_int() == 200);
    BOOST_TEST(clear_campaign.body().find("\"campaign\":null") != std::string::npos);
}

BOOST_AUTO_TEST_CASE(characterize_patch_expires_at_set_then_cleared)
{
    create_link("lhcharpatch03");

    auto set_expiry = dispatch(make_request(
        bhttp::verb::patch,
        "/api/v1/links/lhcharpatch03",
        "{\"expires_at\":\"2999-01-01T00:00:00Z\"}"));
    BOOST_TEST(set_expiry.result_int() == 200);

    // expires_at is not part of serializeLink(); it is observable via preview.
    auto preview_set = dispatch(make_request(
        bhttp::verb::get,
        "/api/v1/links/lhcharpatch03/preview"));
    BOOST_TEST(preview_set.body().find("\"expires_at\":\"2999-01-01T00:00:00Z\"") != std::string::npos);

    auto clear_expiry = dispatch(make_request(
        bhttp::verb::patch,
        "/api/v1/links/lhcharpatch03",
        "{\"expires_at\":null}"));
    BOOST_TEST(clear_expiry.result_int() == 200);

    auto preview_cleared = dispatch(make_request(
        bhttp::verb::get,
        "/api/v1/links/lhcharpatch03/preview"));
    BOOST_TEST(preview_cleared.body().find("\"expires_at\":null") != std::string::npos);
}

BOOST_AUTO_TEST_CASE(characterize_patch_error_paths)
{
    create_link("lhcharpatch04");

    // Missing slug -> 404 not_found.
    expect_api_error(
        dispatch(make_request(
            bhttp::verb::patch,
            "/api/v1/links/lhcharmissing01",
            "{\"enabled\":false}")),
        404,
        "not_found");

    // Non-boolean enabled -> 400 invalid_enabled.
    expect_api_error(
        dispatch(make_request(
            bhttp::verb::patch,
            "/api/v1/links/lhcharpatch04",
            "{\"enabled\":\"false\"}")),
        400,
        "invalid_enabled");

    // Malformed expires_at -> 400 invalid_expires_at.
    expect_api_error(
        dispatch(make_request(
            bhttp::verb::patch,
            "/api/v1/links/lhcharpatch04",
            "{\"expires_at\":\"not-a-timestamp\"}")),
        400,
        "invalid_expires_at");

    // Non-array tags -> 400 invalid_tags.
    expect_api_error(
        dispatch(make_request(
            bhttp::verb::patch,
            "/api/v1/links/lhcharpatch04",
            "{\"tags\":\"docs\"}")),
        400,
        "invalid_tags");

    // Tags violating constraints (whitespace) -> 400 invalid_tags.
    expect_api_error(
        dispatch(make_request(
            bhttp::verb::patch,
            "/api/v1/links/lhcharpatch04",
            "{\"tags\":[\"bad tag\"]}")),
        400,
        "invalid_tags");

    // Non-object metadata -> 400 invalid_metadata.
    expect_api_error(
        dispatch(make_request(
            bhttp::verb::patch,
            "/api/v1/links/lhcharpatch04",
            "{\"metadata\":\"nope\"}")),
        400,
        "invalid_metadata");
}

// Existence is resolved BEFORE body validation (shared contract C3): a PATCH
// to an unknown slug must surface 404 not_found even when the body would
// otherwise fail field validation, rather than leaking a 400 field-code.
BOOST_AUTO_TEST_CASE(characterize_patch_not_found_precedes_body_validation)
{
    // No link is created for this slug; the invalid body must not shadow 404.
    expect_api_error(
        dispatch(make_request(
            bhttp::verb::patch,
            "/api/v1/links/lhcharmissing02",
            "{\"enabled\":\"not-bool\"}")),
        404,
        "not_found");

    // A different invalid field on a still-unknown slug also yields 404.
    expect_api_error(
        dispatch(make_request(
            bhttp::verb::patch,
            "/api/v1/links/lhcharmissing03",
            "{\"tags\":\"not-an-array\"}")),
        404,
        "not_found");
}

BOOST_AUTO_TEST_CASE(characterize_delete_success_and_not_found)
{
    create_link("lhchardelete01");

    auto remove = dispatch(make_request(
        bhttp::verb::delete_,
        "/api/v1/links/lhchardelete01"));
    BOOST_TEST(remove.result_int() == 200);
    BOOST_TEST(std::string(remove[bhttp::field::content_type]) == "application/json");

    // Full serialized link body order, with status flipped to deleted.
    expect_ordered_fields(
        remove.body(),
        {"\"id\":", "\"slug\":", "\"url\":", "\"short_url\":",
         "\"created_at\":", "\"updated_at\":", "\"status\":",
         "\"redirect_type\":", "\"tags\":", "\"metadata\":", "\"campaign\":",
         "\"stats\":"});
    BOOST_TEST(remove.body().find("\"slug\":\"lhchardelete01\"") != std::string::npos);
    BOOST_TEST(remove.body().find("\"status\":\"deleted\"") != std::string::npos);

    // deleted_at persisted: observable via preview.
    auto preview = dispatch(make_request(
        bhttp::verb::get,
        "/api/v1/links/lhchardelete01/preview"));
    BOOST_TEST(preview.body().find("\"status\":\"deleted\"") != std::string::npos);
    BOOST_TEST(preview.body().find("\"deleted_at\":null") == std::string::npos);

    // Missing slug -> 404 not_found.
    expect_api_error(
        dispatch(make_request(
            bhttp::verb::delete_,
            "/api/v1/links/lhcharmissing02")),
        404,
        "not_found");
}

BOOST_AUTO_TEST_CASE(characterize_lifecycle_disable_enable_roundtrip)
{
    create_link("lhcharlife01");

    auto disable = dispatch(make_request(
        bhttp::verb::post,
        "/api/v1/links/lhcharlife01/disable"));
    BOOST_TEST(disable.result_int() == 200);
    BOOST_TEST(std::string(disable[bhttp::field::content_type]) == "application/json");
    BOOST_TEST(disable.body().find("\"status\":\"disabled\"") != std::string::npos);
    // Lifecycle responses use the same serializeLink field ordering.
    expect_ordered_fields(
        disable.body(),
        {"\"id\":", "\"slug\":", "\"status\":", "\"redirect_type\":",
         "\"tags\":", "\"metadata\":", "\"campaign\":", "\"stats\":"});

    auto preview_disabled = dispatch(make_request(
        bhttp::verb::get,
        "/api/v1/links/lhcharlife01/preview"));
    BOOST_TEST(preview_disabled.body().find("\"enabled\":false") != std::string::npos);

    auto enable = dispatch(make_request(
        bhttp::verb::post,
        "/api/v1/links/lhcharlife01/enable"));
    BOOST_TEST(enable.result_int() == 200);
    BOOST_TEST(enable.body().find("\"status\":\"active\"") != std::string::npos);

    auto preview_enabled = dispatch(make_request(
        bhttp::verb::get,
        "/api/v1/links/lhcharlife01/preview"));
    BOOST_TEST(preview_enabled.body().find("\"enabled\":true") != std::string::npos);
}

BOOST_AUTO_TEST_CASE(characterize_lifecycle_restore_soft_deleted)
{
    create_link("lhcharlife02");

    // Soft-delete through the delete handler, then restore.
    auto remove = dispatch(make_request(
        bhttp::verb::delete_,
        "/api/v1/links/lhcharlife02"));
    BOOST_TEST(remove.body().find("\"status\":\"deleted\"") != std::string::npos);

    auto restore = dispatch(make_request(
        bhttp::verb::post,
        "/api/v1/links/lhcharlife02/restore"));
    BOOST_TEST(restore.result_int() == 200);
    BOOST_TEST(restore.body().find("\"status\":\"active\"") != std::string::npos);

    auto preview = dispatch(make_request(
        bhttp::verb::get,
        "/api/v1/links/lhcharlife02/preview"));
    BOOST_TEST(preview.body().find("\"deleted_at\":null") != std::string::npos);
    BOOST_TEST(preview.body().find("\"status\":\"active\"") != std::string::npos);
}

BOOST_AUTO_TEST_CASE(characterize_lifecycle_not_found)
{
    for (const std::string& action : {"enable", "disable", "restore"}) {
        expect_api_error(
            dispatch(make_request(
                bhttp::verb::post,
                "/api/v1/links/lhcharmissing03/" + action)),
            404,
            "not_found");
    }
}

BOOST_AUTO_TEST_CASE(characterize_preview_active_exact_body)
{
    create_link("lhcharprev01");

    auto preview = dispatch(make_request(
        bhttp::verb::get,
        "/api/v1/links/lhcharprev01/preview"));
    BOOST_TEST(preview.result_int() == 200);
    BOOST_TEST(std::string(preview[bhttp::field::content_type]) == "application/json");

    // Preview of an untouched active link is fully deterministic; lock the
    // entire body byte-for-byte (field names, order, values).
    BOOST_TEST(preview.body() ==
        "{\"slug\":\"lhcharprev01\","
        "\"url\":\"https://example.com/lhcharprev01\","
        "\"status\":\"active\","
        "\"redirect_type\":\"temporary\","
        "\"enabled\":true,"
        "\"expires_at\":null,"
        "\"deleted_at\":null}");
}

BOOST_AUTO_TEST_CASE(characterize_preview_soft_deleted_link)
{
    create_link("lhcharprev02");
    auto remove = dispatch(make_request(
        bhttp::verb::delete_,
        "/api/v1/links/lhcharprev02"));
    BOOST_TEST(remove.result_int() == 200);

    auto preview = dispatch(make_request(
        bhttp::verb::get,
        "/api/v1/links/lhcharprev02/preview"));
    BOOST_TEST(preview.result_int() == 200);
    expect_ordered_fields(
        preview.body(),
        {"\"slug\":", "\"url\":", "\"status\":", "\"redirect_type\":",
         "\"enabled\":", "\"expires_at\":", "\"deleted_at\":"});
    BOOST_TEST(preview.body().find("\"status\":\"deleted\"") != std::string::npos);
    BOOST_TEST(preview.body().find("\"enabled\":true") != std::string::npos);
    // deleted_at carries a timestamp, not null.
    BOOST_TEST(preview.body().find("\"deleted_at\":null") == std::string::npos);
}

BOOST_AUTO_TEST_CASE(characterize_preview_not_found)
{
    expect_api_error(
        dispatch(make_request(
            bhttp::verb::get,
            "/api/v1/links/lhcharmissing04/preview")),
        404,
        "not_found");
}
