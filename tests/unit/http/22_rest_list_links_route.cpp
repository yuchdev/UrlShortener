/** @file 22_rest_list_links_route.cpp
 *  @brief Characterizes the gated GET /api/v1/links list route (milestone 0006,
 *  Task 04.0 subtask 07): the config gate, the auth model (threat model T-71),
 *  query-to-args mapping, and the hostile-input rejections.
 *
 *  Auth model (2026-11-16 user decision): the route REQUIRES the command API to
 *  be enabled; when it is off it answers exactly like today's GET /api/v1/links
 *  (no enumeration oracle). When on, a User caller may list only non-deleted
 *  links; status=all|deleted require write/admin (loopback + role admin).
 *
 *  Numbering note: the spec assigned 21, but 20 (lifecycle) is taken, so the
 *  parser is 21 and this route test is 22.
 */
#define BOOST_TEST_MODULE RestListLinksRoute
#include <optional>
#include <string>
#include <vector>

#include <boost/asio/ip/address.hpp>
#include <boost/test/unit_test.hpp>
#include <url_shortener/core/config.hpp>
#include <url_shortener/core/types.hpp>
#include <url_shortener/http/request_handlers.hpp>
#include <url_shortener/http/route_registry.hpp>
#include <url_shortener/http/router_builder.hpp>
#include <url_shortener/storage/link_repository.hpp>

namespace bhttp = boost::beast::http;
using url_shortener::Link;
using url_shortener::RedirectType;
using url_shortener::http::BeastRequest;
using url_shortener::http::RouterBuilder;

namespace
{

using BeastResponse = bhttp::response<bhttp::string_body>;

const boost::asio::ip::address k_loopback =
    boost::asio::ip::make_address("127.0.0.1");
const boost::asio::ip::address k_remote =
    boost::asio::ip::make_address("10.0.0.5");

ServerConfig make_config(const bool enabled, const std::string& role)
{
    ServerConfig config;
    config.command_api_enabled = enabled;
    config.command_api_role = role;
    config.shortener_allow_private_targets = true;
    return config;
}

BeastResponse dispatch(
    const BeastRequest& req,
    const ServerConfig& config,
    std::optional<boost::asio::ip::address> peer = std::nullopt)
{
    const auto router = RouterBuilder::buildApplicationRouter();
    return router.dispatch(req, config, false, std::move(peer));
}

BeastRequest make_request(const bhttp::verb method,
                          const std::string& target,
                          const std::string& body = "{}")
{
    BeastRequest req {method, target, 11};
    req.set(bhttp::field::host, "short.test");
    req.set(bhttp::field::content_type, "application/json");
    req.body() = body;
    req.prepare_payload();
    return req;
}

enum class State
{
    active,
    disabled,
    expired,
    deleted
};

Link make_link(const std::string& slug,
               const State state,
               const std::string& created_at)
{
    Link link;
    link.id = "id-" + slug;
    link.slug = slug;
    link.target_url = "https://example.com/" + slug;
    link.created_at = created_at;
    link.updated_at = created_at;
    link.enabled = true;
    link.redirect_type = RedirectType::temporary;
    switch (state) {
        case State::active:
            break;
        case State::disabled:
            link.enabled = false;
            break;
        case State::expired:
            link.expires_at = "2000-01-01T00:00:00Z";
            break;
        case State::deleted:
            link.deleted_at = "2020-06-01T00:00:00Z";
            break;
    }
    return link;
}

void seed(const Link& link)
{
    BOOST_REQUIRE(url_shortener::linkRepository().create(link));
    url_shortener::linkCache().erase(link.slug);
}

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

bool contains(const std::string& haystack, const std::string& needle)
{
    return haystack.find(needle) != std::string::npos;
}

}  // namespace

// Type: unit (gate off, body byte-equal to the method-mismatch reply, store
//   not read).
// Scenario: with the default config (command_api_enabled == false), GET
//   /api/v1/links returns the router's method-mismatch reply for the POST-only
//   path - 405 invalid_method "Only POST is supported" with Allow: POST (RFC
//   9110 15.5.6) - and never reads the store.
// Boundaries: the gate is the whole perimeter; off means "route does not exist"
//   so no enumeration oracle (threat model T-71). The JSON body is byte-equal
//   via the same makeApiErrorResponse the router's error handler uses; the
//   Allow header is added on top.
// On failure first check: did the gate branch change its status/message/Allow,
//   or did it start reading the store?
BOOST_AUTO_TEST_CASE(gate_off_is_method_not_allowed_and_store_not_read)
{
    seed(make_link("l22gate", State::active, "2021-01-01T00:00:01Z"));
    const ServerConfig config = make_config(false, "user");

    // Pin the internal request id so both the dispatched response and the
    // expected error envelope carry the same id and the byte comparison is
    // deterministic (otherwise each call would generate a random id).
    BeastRequest req = make_request(bhttp::verb::get, "/api/v1/links");
    req.set("X-Internal-Request-Id", "req-gate-off");
    const auto res = dispatch(req, config, k_remote);
    const auto expected = url_shortener::makeApiErrorResponse(
        req, config, false, 405, "invalid_method", "Only POST is supported");

    BOOST_TEST(res.result_int() == 405);
    BOOST_TEST(extract_code(res.body()) == "invalid_method");
    BOOST_TEST(res.body() == expected.body());
    BOOST_TEST(std::string(res.base()[bhttp::field::allow]) == "POST");
    // The store was never enumerated: the seeded slug is absent.
    BOOST_TEST(!contains(res.body(), "l22gate"));
}

// Type: unit (gate on, User default lists only active).
// Scenario: a non-loopback User caller with no query lists active links but not
//   disabled or deleted ones.
// Boundaries: the default status is active; a User may read it.
// On failure first check: did the default status change, or did a User get the
//   deleted/disabled rows?
BOOST_AUTO_TEST_CASE(user_default_lists_only_active)
{
    const std::string p = "l22ua";
    seed(make_link(p + "act", State::active, "2021-01-01T00:00:01Z"));
    seed(make_link(p + "dis", State::disabled, "2021-01-01T00:00:02Z"));
    seed(make_link(p + "del", State::deleted, "2021-01-01T00:00:03Z"));

    const ServerConfig config = make_config(true, "user");
    const auto res = dispatch(
        make_request(bhttp::verb::get, "/api/v1/links"), config, k_remote);

    BOOST_TEST(res.result_int() == 200);
    BOOST_TEST(contains(res.body(), p + "act"));
    BOOST_TEST(!contains(res.body(), p + "dis"));
    BOOST_TEST(!contains(res.body(), p + "del"));
}

// Type: unit (gate on, User denied status=all|deleted, T-71).
// Scenario: a User caller requesting status=all or status=deleted gets 403
//   not_authorized and the deleted/disabled rows are never enumerated.
// Boundaries: the permission hook raises read to write for these two statuses,
//   enforced in execute before the lister runs.
// On failure first check: did the hook stop raising, or did the store get
//   enumerated before the denial?
BOOST_AUTO_TEST_CASE(user_denied_status_all_and_deleted)
{
    const std::string p = "l22uf";
    seed(make_link(p + "act", State::active, "2021-01-01T00:00:01Z"));
    seed(make_link(p + "dis", State::disabled, "2021-01-01T00:00:02Z"));
    seed(make_link(p + "del", State::deleted, "2021-01-01T00:00:03Z"));

    const ServerConfig config = make_config(true, "user");
    for (const std::string& status :
         {std::string("all"), std::string("deleted")})
    {
        const auto res = dispatch(
            make_request(bhttp::verb::get, "/api/v1/links?status=" + status),
            config,
            k_remote);
        BOOST_TEST(res.result_int() == 403, status);
        BOOST_TEST(extract_code(res.body()) == "not_authorized", status);
        BOOST_TEST(!contains(res.body(), p + "del"), status);
        BOOST_TEST(!contains(res.body(), p + "dis"), status);
    }
}

// Type: unit (gate on, User allowed status=disabled|expired).
// Scenario: a User caller may list disabled and expired links (both
// non-deleted). Boundaries: only all/deleted are write-gated; disabled/expired
// stay read. On failure first check: did the hook over-raise to a non-deleted
// status?
BOOST_AUTO_TEST_CASE(user_allowed_status_disabled_and_expired)
{
    const std::string p = "l22ud";
    seed(make_link(p + "dis", State::disabled, "2021-01-01T00:00:01Z"));
    seed(make_link(p + "exp", State::expired, "2021-01-01T00:00:02Z"));

    const ServerConfig config = make_config(true, "user");

    const auto disabled = dispatch(
        make_request(bhttp::verb::get, "/api/v1/links?status=disabled&q=" + p),
        config,
        k_remote);
    BOOST_TEST(disabled.result_int() == 200);
    BOOST_TEST(contains(disabled.body(), p + "dis"));

    const auto expired = dispatch(
        make_request(bhttp::verb::get, "/api/v1/links?status=expired&q=" + p),
        config,
        k_remote);
    BOOST_TEST(expired.result_int() == 200);
    BOOST_TEST(contains(expired.body(), p + "exp"));
}

// Type: unit (gate on, loopback admin may use every status, T-71).
// Scenario: a loopback caller under role admin lists status=all (all lifecycle
//   states) and status=deleted (only soft-deleted).
// Boundaries: Admin satisfies the raised write permission; all includes the
//   deleted/disabled/expired rows a User is denied.
// On failure first check: did the loopback+admin escalation stop granting
//   Admin, or did status=all/deleted filter wrongly?
BOOST_AUTO_TEST_CASE(loopback_admin_lists_all_and_deleted)
{
    const std::string p = "l22al";
    seed(make_link(p + "act", State::active, "2021-01-01T00:00:01Z"));
    seed(make_link(p + "dis", State::disabled, "2021-01-01T00:00:02Z"));
    seed(make_link(p + "exp", State::expired, "2021-01-01T00:00:03Z"));
    seed(make_link(p + "del", State::deleted, "2021-01-01T00:00:04Z"));

    const ServerConfig config = make_config(true, "admin");

    const auto all = dispatch(
        make_request(bhttp::verb::get, "/api/v1/links?status=all&q=" + p),
        config,
        k_loopback);
    BOOST_TEST(all.result_int() == 200);
    BOOST_TEST(contains(all.body(), p + "act"));
    BOOST_TEST(contains(all.body(), p + "dis"));
    BOOST_TEST(contains(all.body(), p + "exp"));
    BOOST_TEST(contains(all.body(), p + "del"));

    const auto deleted = dispatch(
        make_request(bhttp::verb::get, "/api/v1/links?status=deleted&q=" + p),
        config,
        k_loopback);
    BOOST_TEST(deleted.result_int() == 200);
    BOOST_TEST(contains(deleted.body(), p + "del"));
    BOOST_TEST(!contains(deleted.body(), p + "act"));
}

// Type: unit (gate on, admin role from a non-loopback peer runs as User).
// Scenario: role admin but a non-loopback peer -> User, so status=all is 403.
// Boundaries: the loopback peer check is the only locality guard (threat model
//   T-64); an absent/remote peer never escalates.
// On failure first check: did the peer check become an OR, or did a remote peer
//   get Admin?
BOOST_AUTO_TEST_CASE(admin_role_from_remote_peer_is_user)
{
    const std::string p = "l22ar";
    seed(make_link(p + "del", State::deleted, "2021-01-01T00:00:01Z"));

    const ServerConfig config = make_config(true, "admin");
    const auto res = dispatch(
        make_request(bhttp::verb::get, "/api/v1/links?status=all&q=" + p),
        config,
        k_remote);
    BOOST_TEST(res.result_int() == 403);
    BOOST_TEST(extract_code(res.body()) == "not_authorized");
}

// Type: unit (admin paging/sorting: newest first, default limit, window).
// Scenario: four active links are listed newest-first by default (limit 50),
//   and limit=2&offset=2 returns the correct window with the right total.
// Boundaries: default sort is created_at desc; the window/total come from the
//   lister. q isolates this case's links from the shared global store.
// On failure first check: did the default sort/limit drift, or did offset/limit
//   stop reaching the lister as numbers?
BOOST_AUTO_TEST_CASE(admin_paging_newest_first_and_window)
{
    const std::string p = "l22win";
    seed(make_link(p + "1", State::active, "2021-01-01T00:00:01Z"));
    seed(make_link(p + "2", State::active, "2021-01-01T00:00:02Z"));
    seed(make_link(p + "3", State::active, "2021-01-01T00:00:03Z"));
    seed(make_link(p + "4", State::active, "2021-01-01T00:00:04Z"));

    const ServerConfig config = make_config(true, "admin");

    const auto all =
        dispatch(make_request(bhttp::verb::get, "/api/v1/links?q=" + p),
                 config,
                 k_loopback);
    BOOST_TEST(all.result_int() == 200);
    BOOST_TEST(contains(all.body(), "\"offset\":0"));
    BOOST_TEST(contains(all.body(), "\"limit\":50"));
    BOOST_TEST(contains(all.body(), "\"total\":4"));
    // Newest first: l22win4's item precedes l22win1's.
    BOOST_TEST(all.body().find(p + "4") < all.body().find(p + "1"));

    const auto window =
        dispatch(make_request(bhttp::verb::get,
                              "/api/v1/links?q=" + p + "&limit=2&offset=2"),
                 config,
                 k_loopback);
    BOOST_TEST(window.result_int() == 200);
    BOOST_TEST(contains(window.body(), "\"offset\":2"));
    BOOST_TEST(contains(window.body(), "\"limit\":2"));
    BOOST_TEST(contains(window.body(), "\"total\":4"));
    // Window [a2, a1] from the newest-first order.
    BOOST_TEST(contains(window.body(), p + "2"));
    BOOST_TEST(contains(window.body(), p + "1"));
    BOOST_TEST(!contains(window.body(), p + "4"));
    BOOST_TEST(!contains(window.body(), p + "3"));
}

// Type: unit (admin sort=slug&order=asc and q decoding).
// Scenario: sort=slug&order=asc orders ascending by slug; q=ex%20ample decodes
//   the space without error; a positive q matches by substring.
// Boundaries: sort/order reach the lister; %20 decodes to a space.
// On failure first check: did sort/order stop being forwarded, or did q stop
//   percent-decoding?
BOOST_AUTO_TEST_CASE(admin_sort_order_and_query_decoding)
{
    const std::string p = "l22srt";
    seed(make_link(p + "1", State::active, "2021-01-01T00:00:01Z"));
    seed(make_link(p + "2", State::active, "2021-01-01T00:00:02Z"));

    const ServerConfig config = make_config(true, "admin");

    const auto asc =
        dispatch(make_request(bhttp::verb::get,
                              "/api/v1/links?q=" + p + "&sort=slug&order=asc"),
                 config,
                 k_loopback);
    BOOST_TEST(asc.result_int() == 200);
    BOOST_TEST(asc.body().find(p + "1") < asc.body().find(p + "2"));

    // %20 decodes to a space; the (unmatched) filter is still a valid 200.
    const auto decoded =
        dispatch(make_request(bhttp::verb::get, "/api/v1/links?q=ex%20ample"),
                 config,
                 k_loopback);
    BOOST_TEST(decoded.result_int() == 200);

    // A positive substring filter returns the matching link.
    const auto positive =
        dispatch(make_request(bhttp::verb::get, "/api/v1/links?q=" + p + "1"),
                 config,
                 k_loopback);
    BOOST_TEST(positive.result_int() == 200);
    BOOST_TEST(contains(positive.body(), p + "1"));

    // Empty tag/campaign values are accepted (no 400).
    for (const std::string& q : {std::string("tag="), std::string("campaign=")})
    {
        const auto res = dispatch(
            make_request(bhttp::verb::get, "/api/v1/links?q=" + p + "&" + q),
            config,
            k_loopback);
        BOOST_TEST(res.result_int() == 200, q);
    }
}

// Type: unit (hostile parameters -> 400 invalid_request, never 500/crash).
// Scenario: out-of-range/wrong-typed/unknown/duplicate/malformed params each
//   return 400 invalid_request with a <arg>: <reason> or fixed message.
// Boundaries: the registry maps invalid_field to 400 invalid_request; the
//   adapter handles duplicate/malformed/invalid-name directly.
// On failure first check: did a bad param escape to 500, or did a message
//   token change?
BOOST_AUTO_TEST_CASE(hostile_parameters_are_400_invalid_request)
{
    const ServerConfig config = make_config(true, "admin");

    const std::vector<std::pair<std::string, std::string>> cases = {
        {"limit=501", "limit: out_of_range"},
        {"limit=0", "limit: out_of_range"},
        {"offset=-1", "offset: wrong_type"},
        // offset at link_list_max_offset + 1 (1000001): the descriptor caps
        // offset, so an over-max value is rejected with the same shape as
        // limit=501 (out_of_range -> 400 invalid_request), threat model T-56.
        {"offset=1000001", "offset: out_of_range"},
        {"limit=abc", "limit: wrong_type"},
        {"status=bogus", "status: not_in_enum"},
        {"foo=1", "foo: unknown"},
        {"limit=1&limit=2", "limit: duplicate"},
        {"q=%zz", "malformed query string"},
        {"Limit=1", "invalid parameter name"},
    };
    for (const auto& [query, needle] : cases) {
        const auto res =
            dispatch(make_request(bhttp::verb::get, "/api/v1/links?" + query),
                     config,
                     k_loopback);
        BOOST_TEST(res.result_int() == 400, query);
        BOOST_TEST(extract_code(res.body()) == "invalid_request", query);
        BOOST_TEST(contains(res.body(), needle), query);
    }
}

// Type: unit (leading-zero integer tokens are canonical base-10, never octal).
// Scenario: limit=007 is interpreted as 7, offset=000 as 0, and limit=00 as 0
//   (below the min -> out_of_range). No value is parsed as octal or forms
//   invalid JSON.
// Boundaries: an all-digits value is emitted as a canonical JSON number with
//   leading zeros stripped; this is the explicitly-defined behaviour.
// On failure first check: did leading-zero handling start producing invalid
// JSON
//   or an octal value?
BOOST_AUTO_TEST_CASE(leading_zero_integers_are_decimal)
{
    const std::string p = "l22lz";
    seed(make_link(p, State::active, "2021-01-01T00:00:01Z"));
    const ServerConfig config = make_config(true, "admin");

    const auto limit7 = dispatch(
        make_request(bhttp::verb::get, "/api/v1/links?q=" + p + "&limit=007"),
        config,
        k_loopback);
    BOOST_TEST(limit7.result_int() == 200);
    BOOST_TEST(contains(limit7.body(), "\"limit\":7"));
    BOOST_TEST(contains(limit7.body(), p));

    const auto offset0 = dispatch(
        make_request(bhttp::verb::get, "/api/v1/links?q=" + p + "&offset=000"),
        config,
        k_loopback);
    BOOST_TEST(offset0.result_int() == 200);
    BOOST_TEST(contains(offset0.body(), "\"offset\":0"));

    // "00" canonicalizes to 0, which is below the inclusive min of 1.
    const auto limit00 =
        dispatch(make_request(bhttp::verb::get, "/api/v1/links?limit=00"),
                 config,
                 k_loopback);
    BOOST_TEST(limit00.result_int() == 400);
    BOOST_TEST(contains(limit00.body(), "limit: out_of_range"));
}

// Type: unit (route coexistence with the sibling link routes).
// Scenario: POST /api/v1/links still creates (201), and the single-link GET
//   routes (by slug, by id) still reach their handlers, unaffected by the new
//   list route.
// Boundaries: the list route is a distinct 3-segment pattern; it must not
//   hijack the 4/5-segment sibling routes (contract C3/C10).
// On failure first check: did the new GET route change POST dispatch, or shadow
//   a sibling route?
BOOST_AUTO_TEST_CASE(route_coexistence_create_and_single_gets)
{
    const ServerConfig config = make_config(false, "user");

    const auto created = dispatch(
        make_request(
            bhttp::verb::post,
            "/api/v1/links",
            R"({"target_url":"https://example.com/l22co","slug":"l22co"})"),
        config);
    BOOST_TEST(created.result_int() == 201);

    const auto by_slug =
        dispatch(make_request(bhttp::verb::get, "/api/v1/links/l22co"), config);
    BOOST_TEST(by_slug.result_int() == 200);
    BOOST_TEST(contains(by_slug.body(), "l22co"));

    const auto by_id_unknown = dispatch(
        make_request(bhttp::verb::get, "/api/v1/links/id/l22nope"), config);
    BOOST_TEST(by_id_unknown.result_int() == 404);
    BOOST_TEST(extract_code(by_id_unknown.body()) == "not_found");
}

// Type: unit (method handling and metrics label unchanged).
// Scenario: HEAD and DELETE on /api/v1/links are 405 invalid_method with
//   Allow: GET, POST (RFC 9110 15.5.6), the target's metric label is api_links,
//   and a successful list carries an X-Request-Id.
// Boundaries: method rejection is a router concern; the path serves GET and
//   POST, so the Allow header lists exactly those (HEAD is never advertised).
// On failure first check: did a non-GET/POST method start matching, or did the
//   label/header/Allow drift?
BOOST_AUTO_TEST_CASE(method_handling_label_and_request_id)
{
    const ServerConfig config = make_config(true, "admin");

    for (const bhttp::verb method : {bhttp::verb::head, bhttp::verb::delete_}) {
        const auto res =
            dispatch(make_request(method, "/api/v1/links"), config, k_loopback);
        BOOST_TEST(res.result_int() == 405);
        BOOST_TEST(extract_code(res.body()) == "invalid_method");
        BOOST_TEST(std::string(res.base()[bhttp::field::allow]) == "GET, POST");
    }

    BOOST_TEST(url_shortener::http::routeLabelForTarget("/api/v1/links")
               == "api_links");

    const auto listed = dispatch(
        make_request(bhttp::verb::get, "/api/v1/links"), config, k_loopback);
    BOOST_TEST(listed.result_int() == 200);
    BOOST_TEST(!std::string(listed.base()["X-Request-Id"]).empty());
}

// Type: unit (list items equal the single-link GET body).
// Scenario: a listed item is byte-for-byte the same JSON as GET
//   /api/v1/links/{slug} returns, proving both share serializeLinkViewJson.
// Boundaries: both resolve through the same fallback runtime, so the base
// domain
//   and item shape match exactly.
// On failure first check: did the list item projection diverge from the single
//   link view serializer?
BOOST_AUTO_TEST_CASE(list_item_equals_single_get_body)
{
    seed(make_link("l22item", State::active, "2021-01-01T00:00:01Z"));
    const ServerConfig config = make_config(true, "admin");

    const auto single =
        dispatch(make_request(bhttp::verb::get, "/api/v1/links/l22item"),
                 config,
                 k_loopback);
    BOOST_REQUIRE(single.result_int() == 200);

    const auto listed =
        dispatch(make_request(bhttp::verb::get, "/api/v1/links?q=l22item"),
                 config,
                 k_loopback);
    BOOST_TEST(listed.result_int() == 200);
    BOOST_TEST(contains(listed.body(), single.body()));
}
