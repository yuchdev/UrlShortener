/** @file 20_rest_lifecycle_via_registry.cpp
 *  @brief Characterizes DELETE /api/v1/links/{slug} and the lifecycle routes
 *  POST /api/v1/links/{slug}/enable|disable|restore after they are re-pointed
 *  at the command registry (milestone 0006, Task 04.0 subtask 06).
 *
 *  Scope: the delete + lifecycle contract (200 with the serialized updated
 *  view, 404 not_found "Link not found" for an unknown slug, no request body
 *  read) must stay byte-for-byte what it is today (contract C3). These are
 *  management-plane mutations; the redirect fast path is untouched (contract
 *  C10) and is re-run unchanged by 12_prefixed_redirect_handler /
 *  13_root_redirect_handler.
 *
 *  Numbering note: the spec named this file `19_...`, but `19_` is already
 *  taken by `19_rest_patch_via_registry.cpp` (subtask 05), so it is `20_` (next
 *  free in the tests/unit/http 15-29 range).
 */
#define BOOST_TEST_MODULE RestLifecycleViaRegistry
#include <string>

#include <boost/test/unit_test.hpp>
#include <url_shortener/core/config.hpp>
#include <url_shortener/http/router.hpp>
#include <url_shortener/http/router_builder.hpp>

namespace bhttp = boost::beast::http;
using url_shortener::http::BeastRequest;
using url_shortener::http::BeastResponse;
using url_shortener::http::RouterBuilder;

namespace
{

/// Builds a request, mirroring the production session shape. A body is accepted
/// so the "garbage body is ignored" cases can carry one.
BeastRequest make_request(const bhttp::verb method,
                          const std::string& target,
                          const std::string& body = "")
{
    BeastRequest req {method, target, 11};
    req.set(bhttp::field::host, "short.test");
    if (!body.empty()) {
        req.set(bhttp::field::content_type, "application/json");
        req.body() = body;
        req.prepare_payload();
    }
    return req;
}

/// The permissive config the lifecycle tests dispatch with.
ServerConfig test_config()
{
    ServerConfig config;
    config.shortener_allow_private_targets = true;
    config.shortener_generated_slug_length = 8;
    return config;
}

/// Dispatches a request through the real application router (the production
/// seam the fallback command runtime backs in the unit-test process).
BeastResponse dispatch(const BeastRequest& req)
{
    const auto router = RouterBuilder::buildApplicationRouter();
    return router.dispatch(req, test_config(), false);
}

/// Extracts the `"code":"..."` token from a REST error body, or "" when absent.
std::string extract_code(const std::string& body)
{
    const std::string key = "\"code\":\"";
    const auto pos = body.find(key);
    if (pos == std::string::npos) {
        return {};
    }
    const auto start = pos + key.size();
    const auto end = body.find('"', start);
    return end == std::string::npos ? std::string {}
                                    : body.substr(start, end - start);
}

/// Creates a link by POSTing through the same router, so the fallback runtime's
/// store (the one the lifecycle mutations read/write) is the one it is in.
void create_link(const std::string& slug)
{
    const auto res = dispatch(make_request(
        bhttp::verb::post,
        "/api/v1/links",
        R"({"url":"https://example.com/)" + slug + R"(","slug":")" + slug
            + R"("})"));
    BOOST_REQUIRE_EQUAL(res.result_int(), 201);
}

/// Runs DELETE /api/v1/links/{slug} and returns the whole response.
BeastResponse del(const std::string& slug, const std::string& body = "")
{
    return dispatch(make_request(bhttp::verb::delete_,
                                 "/api/v1/links/" + slug,
                                 body));
}

/// Runs POST /api/v1/links/{slug}/{action} (enable|disable|restore).
BeastResponse lifecycle(const std::string& slug,
                        const std::string& action,
                        const std::string& body = "")
{
    return dispatch(make_request(bhttp::verb::post,
                                 "/api/v1/links/" + slug + "/" + action,
                                 body));
}

}  // namespace

// Type: unit (delete success projects the soft-deleted view).
// Scenario: DELETE on an existing link returns 200; the body's status is
//   "deleted". The shared full-view serializer (serializeLinkViewJson) encodes
//   the soft-deletion only through the status field - it carries no
//   `deleted_at` or `enabled` member (resolveLinkStatus folds them into
//   status), so the observable contract (C3) for the lifecycle state is the
//   status value.
// Boundaries: delete runs link.delete through the bridge; the 200 body is the
//   shared serializer over the post-mutation view.
// On failure first check: did link.delete stop soft-deleting, or did the bridge
//   stop rendering the view at 200?
BOOST_AUTO_TEST_CASE(delete_returns_200_deleted_status)
{
    create_link("lc20del");
    const auto res = del("lc20del");
    BOOST_TEST(res.result_int() == 200);
    BOOST_TEST(res.body().find("\"status\":\"deleted\"") != std::string::npos);
}

// Type: unit (delete is idempotent at the transport layer).
// Scenario: deleting the same link twice returns 200 both times (the soft-
//   deleted record is still found and re-stamped).
// Boundaries: a second delete is not a 404 - findBySlug still returns the
//   soft-deleted link.
// On failure first check: did the second delete start returning 404?
BOOST_AUTO_TEST_CASE(delete_twice_is_200_each_time)
{
    create_link("lc20twice");
    BOOST_TEST(del("lc20twice").result_int() == 200);
    const auto second = del("lc20twice");
    BOOST_TEST(second.result_int() == 200);
    BOOST_TEST(second.body().find("\"status\":\"deleted\"")
               != std::string::npos);
}

// Type: unit (enable / disable round trip).
// Scenario: disable then enable flip the serialized status between "disabled"
//   and "active", 200 each time. The disabled flag surfaces only through the
//   status field (the view omits a raw `enabled` member).
// Boundaries: enable/disable map to link.enable/link.disable (slug only).
// On failure first check: did the action->command mapping drift, or did the
//   status projection change?
BOOST_AUTO_TEST_CASE(enable_disable_round_trip)
{
    create_link("lc20toggle");

    const auto disabled = lifecycle("lc20toggle", "disable");
    BOOST_TEST(disabled.result_int() == 200);
    BOOST_TEST(disabled.body().find("\"status\":\"disabled\"")
               != std::string::npos);

    const auto enabled = lifecycle("lc20toggle", "enable");
    BOOST_TEST(enabled.result_int() == 200);
    BOOST_TEST(enabled.body().find("\"status\":\"active\"")
               != std::string::npos);
}

// Type: unit (restore after delete returns the link to active).
// Scenario: deleting then restoring a link returns 200; the restored body's
//   status is "active" again (restore clears the soft-deletion, which the
//   status projection reflects - the view carries no raw deleted_at member).
// Boundaries: restore maps to link.restore, which resets deleted_at.
// On failure first check: did restore stop clearing the soft-deletion?
BOOST_AUTO_TEST_CASE(restore_after_delete_returns_active)
{
    create_link("lc20restore");
    BOOST_TEST(del("lc20restore").result_int() == 200);

    const auto restored = lifecycle("lc20restore", "restore");
    BOOST_TEST(restored.result_int() == 200);
    BOOST_TEST(restored.body().find("\"status\":\"active\"")
               != std::string::npos);
}

// Type: unit (restore of a never-deleted link is a 200 no-op).
// Scenario: restoring a link that was never deleted returns 200 with the status
//   unchanged (still active).
// Boundaries: restore on a live link resets an already-empty soft-deletion.
// On failure first check: did restore start erroring on a live link?
BOOST_AUTO_TEST_CASE(restore_never_deleted_is_200_unchanged)
{
    create_link("lc20liverestore");
    const auto res = lifecycle("lc20liverestore", "restore");
    BOOST_TEST(res.result_int() == 200);
    BOOST_TEST(res.body().find("\"status\":\"active\"") != std::string::npos);
}

// Type: unit (enable on a soft-deleted link does not resurrect it).
// Scenario: enabling a deleted link returns 200 but the status stays "deleted"
//   (the soft-deletion wins over the enabled flag in resolveLinkStatus).
// Boundaries: enable only toggles the enabled flag; it never clears the
//   soft-deletion, and the status projection prefers deleted.
// On failure first check: did enable start clearing the soft-deletion, or did
//   the status projection stop preferring deleted?
BOOST_AUTO_TEST_CASE(enable_on_deleted_link_keeps_deleted_status)
{
    create_link("lc20endeleted");
    BOOST_TEST(del("lc20endeleted").result_int() == 200);

    const auto res = lifecycle("lc20endeleted", "enable");
    BOOST_TEST(res.result_int() == 200);
    BOOST_TEST(res.body().find("\"status\":\"deleted\"") != std::string::npos);
}

// Type: unit (unknown slug is 404 not_found on every route).
// Scenario: DELETE and each lifecycle action on an unknown slug return 404 with
//   code not_found and the message "Link not found".
// Boundaries: the not_found path is produced by the service and mapped by the
//   bridge; code and message are the locked REST contract (C3).
// On failure first check: did a route stop mapping not_found, or did the
//   message string drift?
BOOST_AUTO_TEST_CASE(unknown_slug_is_404_not_found_on_every_route)
{
    struct route_case
    {
        bhttp::verb method;
        std::string target;
    };
    const route_case cases[] = {
        {bhttp::verb::delete_, "/api/v1/links/lc20missing"},
        {bhttp::verb::post, "/api/v1/links/lc20missing/enable"},
        {bhttp::verb::post, "/api/v1/links/lc20missing/disable"},
        {bhttp::verb::post, "/api/v1/links/lc20missing/restore"},
    };
    for (const auto& c : cases) {
        const auto res = dispatch(make_request(c.method, c.target));
        BOOST_TEST(res.result_int() == 404, c.target);
        BOOST_TEST(extract_code(res.body()) == "not_found", c.target);
        BOOST_TEST(res.body().find("Link not found") != std::string::npos,
                   c.target);
    }
}

// Type: unit (wrong method on a lifecycle route is unchanged).
// Scenario: GET /api/v1/links/{slug}/enable keeps its pre-existing 400
//   invalid_method behavior (decided by the router matcher, before any
//   handler), exactly as 03_method_and_error_characterization pins it.
// Boundaries: the method mismatch is a router concern this subtask does not
//   touch; the handler is never entered.
// On failure first check: did the route registration for enable change method?
BOOST_AUTO_TEST_CASE(wrong_method_on_enable_is_unchanged_invalid_method)
{
    create_link("lc20wrongmethod");
    const auto res =
        dispatch(make_request(bhttp::verb::get,
                              "/api/v1/links/lc20wrongmethod/enable"));
    BOOST_TEST(res.result_int() == 400);
    BOOST_TEST(extract_code(res.body()) == "invalid_method");
}

// Type: unit (request body is ignored on delete and lifecycle).
// Scenario: a garbage non-JSON body on DELETE and on each lifecycle action is
//   ignored; every call still returns 200 (no body is read).
// Boundaries: these routes address the link by slug only; the body never
//   reaches the registry.
// On failure first check: did a route start parsing the body?
BOOST_AUTO_TEST_CASE(request_body_is_ignored)
{
    create_link("lc20garbage");
    BOOST_TEST(lifecycle("lc20garbage", "disable", "this is not json {{{")
                   .result_int()
               == 200);
    BOOST_TEST(
        lifecycle("lc20garbage", "enable", "<xml>nope</xml>").result_int()
        == 200);
    BOOST_TEST(del("lc20garbage", "garbage body here").result_int() == 200);
}

// Type: unit (X-Request-Id is echoed on delete and lifecycle).
// Scenario: a validated internal request id is echoed back in the X-Request-Id
//   response header on a successful delete and a successful lifecycle action.
// Boundaries: the response helpers echo the id for every outcome; the bridge
//   does not drop it.
// On failure first check: did the success path stop threading the request id?
BOOST_AUTO_TEST_CASE(request_id_is_echoed)
{
    create_link("lc20rid");

    auto enable_req =
        make_request(bhttp::verb::post, "/api/v1/links/lc20rid/enable");
    enable_req.set("X-Internal-Request-Id", "rid-enable-20");
    const auto enabled = dispatch(enable_req);
    BOOST_TEST(enabled.result_int() == 200);
    BOOST_TEST(enabled.base()["X-Request-Id"] == "rid-enable-20");

    auto delete_req =
        make_request(bhttp::verb::delete_, "/api/v1/links/lc20rid");
    delete_req.set("X-Internal-Request-Id", "rid-delete-20");
    const auto deleted = dispatch(delete_req);
    BOOST_TEST(deleted.result_int() == 200);
    BOOST_TEST(deleted.base()["X-Request-Id"] == "rid-delete-20");
}

// Type: unit (delete / lifecycle response is a well-formed JSON view).
// Scenario: a successful delete body is a JSON object whose fields match the
//   shared link view (slug + status), content type application/json.
// Boundaries: the 200 body is result_json from link.delete embedded verbatim by
//   the bridge, not a bespoke shape.
// On failure first check: did the bridge stop embedding result_json, or did the
//   content type drift?
BOOST_AUTO_TEST_CASE(delete_response_is_json_link_view)
{
    create_link("lc20json");
    const auto res = del("lc20json");
    BOOST_TEST(res.result_int() == 200);
    BOOST_TEST(res.base()[bhttp::field::content_type] == "application/json");
    BOOST_TEST(res.body().front() == '{');
    BOOST_TEST(res.body().back() == '}');
    BOOST_TEST(res.body().find("\"slug\":\"lc20json\"") != std::string::npos);
}
