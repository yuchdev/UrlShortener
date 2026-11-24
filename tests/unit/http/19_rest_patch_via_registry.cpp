/** @file 19_rest_patch_via_registry.cpp
 *  @brief Characterizes PATCH /api/v1/links/{slug} after update is re-pointed at
 *  the command registry (milestone 0006, Task 04.0 subtask 05).
 *
 *  Scope: the PATCH contract (status, body, per-field 400 codes/messages and
 *  their ORDER, the existence-before-body-validation rule, and the three
 *  absent / null / value states) must stay byte-for-byte what it is today
 *  (contract C3). The create throttle (threat model T-75) is deliberately NOT
 *  applied to PATCH. The redirect fast path is untouched (contract C10); this
 *  file only exercises the update management plane.
 *
 *  Numbering note: the spec named this file `18_...`, but `18_` is already taken
 *  by `18_target_requires_peer.cpp` (subtask 04 peer plumbing), so it is `19_`.
 */
#define BOOST_TEST_MODULE RestPatchViaRegistry
#include <cctype>
#include <string>

#include <boost/test/unit_test.hpp>
#include <url_shortener/core/config.hpp>
#include <url_shortener/http/router.hpp>
#include <url_shortener/http/router_builder.hpp>
#include <url_shortener/storage/link_repository.hpp>

namespace bhttp = boost::beast::http;
using url_shortener::http::BeastRequest;
using url_shortener::http::BeastResponse;
using url_shortener::http::RouterBuilder;

namespace
{

/// Builds a request with a JSON body, mirroring the production session shape.
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

/// The permissive config the patch tests dispatch with.
ServerConfig test_config()
{
    ServerConfig config;
    config.shortener_allow_private_targets = true;
    config.shortener_generated_slug_length = 8;
    return config;
}

/// Dispatches a request through the real application router (the production
/// seam that the fallback command runtime backs in the unit-test process).
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
/// store (the one the PATCH existence check reads) is the one it is written to.
void create_link(const std::string& slug)
{
    const auto res = dispatch(make_request(
        bhttp::verb::post,
        "/api/v1/links",
        R"({"url":"https://example.com/)" + slug + R"(","slug":")" + slug
            + R"("})"));
    BOOST_REQUIRE_EQUAL(res.result_int(), 201);
}

/// GETs the canonical link view body.
std::string get_body(const std::string& slug)
{
    return dispatch(make_request(bhttp::verb::get, "/api/v1/links/" + slug))
        .body();
}

/// GETs the preview body (exposes expires_at, which the view omits).
std::string preview_body(const std::string& slug)
{
    return dispatch(
               make_request(bhttp::verb::get,
                            "/api/v1/links/" + slug + "/preview"))
        .body();
}

/// Extracts the `"updated_at":"..."` value from a serialized link view body, or
/// "" when the field is absent.
std::string extract_updated_at(const std::string& body)
{
    const std::string key = "\"updated_at\":\"";
    const auto pos = body.find(key);
    if (pos == std::string::npos) {
        return {};
    }
    const auto start = pos + key.size();
    const auto end = body.find('"', start);
    return end == std::string::npos ? std::string {}
                                    : body.substr(start, end - start);
}

/// Returns the view body with the whole `"updated_at":"..."` member elided, so
/// two views can be compared for equality on every field except updated_at.
/// Both operands of a comparison are passed through this same transform, so any
/// structural artifact (e.g. an adjacent comma) is identical on both sides and
/// cancels out.
std::string without_updated_at(const std::string& body)
{
    const std::string key = "\"updated_at\":\"";
    const auto pos = body.find(key);
    if (pos == std::string::npos) {
        return body;
    }
    const auto start = pos + key.size();
    const auto end = body.find('"', start);
    if (end == std::string::npos) {
        return body;
    }
    return body.substr(0, pos) + body.substr(end + 1);
}

/// True when `value` is an RFC 3339 UTC timestamp of the exact fixed-width shape
/// the serializer emits (`currentTimestamp`): `YYYY-MM-DDTHH:MM:SSZ` - length
/// 20, digits in the numeric positions, `-`/`T`/`:` separators and a `Z`
/// suffix. Because the width is fixed, lexicographic ordering of two such
/// strings equals chronological ordering.
bool is_rfc3339_utc(const std::string& value)
{
    if (value.size() != 20 || value[19] != 'Z') {
        return false;
    }
    static const int digit_positions[] = {
        0, 1, 2, 3, 5, 6, 8, 9, 11, 12, 14, 15, 17, 18};
    for (const int p : digit_positions) {
        if (std::isdigit(static_cast<unsigned char>(value[p])) == 0) {
            return false;
        }
    }
    return value[4] == '-' && value[7] == '-' && value[10] == 'T'
        && value[13] == ':' && value[16] == ':';
}

std::string patch(const std::string& slug, const std::string& body)
{
    const auto res =
        dispatch(make_request(bhttp::verb::patch, "/api/v1/links/" + slug, body));
    BOOST_REQUIRE_EQUAL(res.result_int(), 200);
    return res.body();
}

}  // namespace

// Type: unit (enabled toggles both ways).
// Scenario: PATCH enabled:false then enabled:true flips the serialized status
//   between "disabled" and "active", returning 200 each time.
// Boundaries: enabled is forwarded as a JSON bool through the registry arg.
// On failure first check: did the enabled arg stop reaching the service, or did
//   the status projection change?
BOOST_AUTO_TEST_CASE(patch_enabled_false_then_true)
{
    create_link("p19enabled");

    const auto disabled = patch("p19enabled", R"({"enabled":false})");
    BOOST_TEST(disabled.find("\"status\":\"disabled\"") != std::string::npos);

    const auto enabled = patch("p19enabled", R"({"enabled":true})");
    BOOST_TEST(enabled.find("\"status\":\"active\"") != std::string::npos);
}

// Type: unit (expires_at tri-state: set / null clears / absent keeps).
// Scenario: setting expires_at records it (observable via preview); a later
//   expires_at:null clears it; a PATCH that omits expires_at keeps the current
//   value (absent != null).
// Boundaries: the three states map to add_string / add_null / omitted; expires
//   is not part of the view, so it is read through preview.
// On failure first check: did absent start clearing, or null start keeping?
BOOST_AUTO_TEST_CASE(patch_expires_at_set_null_clears_absent_keeps)
{
    create_link("p19expiry");

    patch("p19expiry", R"({"expires_at":"2999-01-01T00:00:00Z"})");
    BOOST_TEST(preview_body("p19expiry").find(
                   "\"expires_at\":\"2999-01-01T00:00:00Z\"")
               != std::string::npos);

    // An unrelated field change (absent expires_at) keeps the expiry.
    patch("p19expiry", R"({"enabled":true})");
    BOOST_TEST(preview_body("p19expiry").find(
                   "\"expires_at\":\"2999-01-01T00:00:00Z\"")
               != std::string::npos);

    // Explicit null clears it.
    patch("p19expiry", R"({"expires_at":null})");
    BOOST_TEST(preview_body("p19expiry").find("\"expires_at\":null")
               != std::string::npos);
}

// Type: unit (campaign tri-state: set / null clears / absent keeps / {}).
// Scenario: a campaign object is stored; an absent campaign keeps it; an empty
//   object {} replaces it with an empty campaign; null clears it to null.
// Boundaries: the three present states (object / {} / null) are distinct, and
//   absence leaves the field untouched.
// On failure first check: did {} get treated as null, or absent as a clear?
BOOST_AUTO_TEST_CASE(patch_campaign_set_null_clears_absent_keeps_and_empty)
{
    create_link("p19camp");

    patch("p19camp", R"({"campaign":{"name":"spring","source":"news"}})");
    BOOST_TEST(get_body("p19camp").find(
                   "\"campaign\":{\"name\":\"spring\",\"source\":\"news\"}")
               != std::string::npos);

    // Absent campaign keeps it.
    patch("p19camp", R"({"enabled":true})");
    BOOST_TEST(get_body("p19camp").find("\"name\":\"spring\"")
               != std::string::npos);

    // Empty object replaces with an empty campaign (present, not cleared).
    patch("p19camp", R"({"campaign":{}})");
    BOOST_TEST(get_body("p19camp").find("\"name\":\"spring\"")
               == std::string::npos);

    // Null clears to null.
    patch("p19camp", R"({"campaign":null})");
    BOOST_TEST(get_body("p19camp").find("\"campaign\":null")
               != std::string::npos);
}

// Type: unit (unknown inner campaign key ignored).
// Scenario: a campaign with an unknown inner key is accepted (200); the unknown
//   key is dropped before the registry's object validator sees it.
// Boundaries: the REST layer rebuilds the campaign from only the six known
//   members (parity with create).
// On failure first check: did the rebuild start forwarding unknown inner keys?
BOOST_AUTO_TEST_CASE(patch_campaign_unknown_inner_key_ignored)
{
    create_link("p19campunknown");
    const auto body =
        patch("p19campunknown", R"({"campaign":{"source":"s","bogus":"x"}})");
    BOOST_TEST(body.find("\"source\":\"s\"") != std::string::npos);
    BOOST_TEST(body.find("bogus") == std::string::npos);
}

// Type: unit (tags normalization and clearing).
// Scenario: tags [" a ","a","b"] normalize to ["a","b"] (trim + de-dup); a later
//   tags:[] clears every tag.
// Boundaries: the normalized list (not the raw one) is what is applied.
// On failure first check: did normalization stop running in the handler, or did
//   [] stop clearing?
BOOST_AUTO_TEST_CASE(patch_tags_normalized_then_cleared)
{
    create_link("p19tags");

    const auto normalized =
        patch("p19tags", R"({"tags":[" a ","a","b"]})");
    BOOST_TEST(normalized.find("\"tags\":[\"a\",\"b\"]") != std::string::npos);

    const auto cleared = patch("p19tags", R"({"tags":[]})");
    BOOST_TEST(cleared.find("\"tags\":[]") != std::string::npos);
}

// Type: unit (metadata empty object clears).
// Scenario: metadata is set, then metadata:{} replaces it with an empty map.
// Boundaries: a present empty object is a replacement (clear), not a no-op.
// On failure first check: did {} stop clearing the metadata map?
BOOST_AUTO_TEST_CASE(patch_metadata_empty_object_clears)
{
    create_link("p19meta");

    patch("p19meta", R"({"metadata":{"team":"core"}})");
    BOOST_TEST(get_body("p19meta").find("\"team\":\"core\"")
               != std::string::npos);

    const auto cleared = patch("p19meta", R"({"metadata":{}})");
    BOOST_TEST(cleared.find("\"metadata\":{}") != std::string::npos);
}

// Type: unit (existence before body validation: unknown slug).
// Scenario: a PATCH to an unknown slug returns 404 not_found for both a valid
//   body and a per-field-invalid body (existence is checked first).
// Boundaries: the 404 must win over any 400 the body would otherwise produce.
// On failure first check: did body validation move ahead of the existence
//   check?
BOOST_AUTO_TEST_CASE(patch_unknown_slug_is_404_even_with_bad_body)
{
    const auto valid_body = dispatch(make_request(
        bhttp::verb::patch, "/api/v1/links/p19missing1", R"({"enabled":true})"));
    BOOST_TEST(valid_body.result_int() == 404);
    BOOST_TEST(extract_code(valid_body.body()) == "not_found");

    // A malformed / per-field-invalid body on an unknown slug still 404.
    const auto bad_body = dispatch(make_request(
        bhttp::verb::patch,
        "/api/v1/links/p19missing2",
        R"({"enabled":"not-a-bool","tags":"not-an-array"})"));
    BOOST_TEST(bad_body.result_int() == 404);
    BOOST_TEST(extract_code(bad_body.body()) == "not_found");

    // A completely non-JSON body on an unknown slug is still 404, never 500.
    const auto garbage = dispatch(make_request(
        bhttp::verb::patch, "/api/v1/links/p19missing3", "this is not json"));
    BOOST_TEST(garbage.result_int() == 404);
    BOOST_TEST(extract_code(garbage.body()) == "not_found");
}

// Type: unit (per-field 400 codes and messages).
// Scenario: each malformed field on an existing link reports its own 400 code
//   and the exact message the REST contract locks in.
// Boundaries: the code AND the message are part of the public contract.
// On failure first check: did a field's code or message string drift?
BOOST_AUTO_TEST_CASE(patch_per_field_400_codes_and_messages)
{
    create_link("p19fields");
    struct field_case
    {
        std::string body;
        std::string code;
        std::string message;
    };
    const field_case cases[] = {
        {R"({"enabled":"nope"})", "invalid_enabled", "enabled must be boolean"},
        {R"({"expires_at":"not-a-date"})",
         "invalid_expires_at",
         "expires_at must be RFC3339 UTC or null"},
        {R"({"tags":"not-array"})", "invalid_tags", "tags must be string array"},
        {R"({"tags":["bad tag"]})",
         "invalid_tags",
         "tags violate constraints"},
        {R"({"metadata":"nope"})",
         "invalid_metadata",
         "metadata must be flat object with string values"},
        {R"({"campaign":5})",
         "invalid_campaign",
         "campaign must be object or null"},
    };
    for (const auto& c : cases) {
        const auto res = dispatch(make_request(
            bhttp::verb::patch, "/api/v1/links/p19fields", c.body));
        BOOST_TEST(res.result_int() == 400, c.code);
        BOOST_TEST(extract_code(res.body()) == c.code, c.code);
        BOOST_TEST(res.body().find(c.message) != std::string::npos, c.message);
    }
}

// Type: unit (per-field check ORDER).
// Scenario: a body invalid in both enabled and tags reports invalid_enabled -
//   enabled is checked before tags.
// Boundaries: the field order is the public contract and must not change.
// On failure first check: did the check order change?
BOOST_AUTO_TEST_CASE(patch_field_order_enabled_before_tags)
{
    create_link("p19order");
    const auto res = dispatch(make_request(
        bhttp::verb::patch,
        "/api/v1/links/p19order",
        R"({"enabled":"bad","tags":"also-bad"})"));
    BOOST_TEST(res.result_int() == 400);
    BOOST_TEST(extract_code(res.body()) == "invalid_enabled");
}

// Type: unit (service-level tag constraint surfaces as invalid_tags).
// Scenario: 21 tags pass the shape check but violate validateTags, returning
//   400 invalid_tags "tags violate constraints".
// Boundaries: this is REST error-code selection in the handler (the same
//   validator the service calls), not duplicated business logic.
// On failure first check: did the constraint check stop running in the handler?
BOOST_AUTO_TEST_CASE(patch_too_many_tags_is_invalid_tags)
{
    create_link("p19manytags");
    std::string tags;
    for (int i = 0; i < 21; ++i) {
        tags += (i == 0 ? "\"" : ",\"") + std::string("t") + std::to_string(i)
            + "\"";
    }
    const auto res = dispatch(make_request(
        bhttp::verb::patch,
        "/api/v1/links/p19manytags",
        R"({"tags":[)" + tags + "]}"));
    BOOST_TEST(res.result_int() == 400);
    BOOST_TEST(extract_code(res.body()) == "invalid_tags");
    BOOST_TEST(res.body().find("tags violate constraints")
               != std::string::npos);
}

// Type: unit (metadata value-length constraint surfaces as invalid_metadata).
// Scenario: a 513-character metadata value violates validateMetadata, returning
//   400 invalid_metadata.
// Boundaries: the handler runs validateMetadata for REST error-code selection.
// On failure first check: did the metadata limit check stop running?
BOOST_AUTO_TEST_CASE(patch_oversized_metadata_value_is_invalid_metadata)
{
    create_link("p19bigmeta");
    const std::string value(513, 'x');
    const auto res = dispatch(make_request(
        bhttp::verb::patch,
        "/api/v1/links/p19bigmeta",
        R"({"metadata":{"k":")" + value + R"("}})"));
    BOOST_TEST(res.result_int() == 400);
    BOOST_TEST(extract_code(res.body()) == "invalid_metadata");
}

// Type: unit (empty / non-JSON / unknown-keys body is a no-op update).
// Scenario: an empty {} body, a non-JSON body, and a body with only unknown
//   top-level keys each return 200 and leave every known field unchanged.
// Boundaries: unknown keys are ignored; a garbage body updates nothing.
// On failure first check: did an unknown key start being forwarded, or did a
//   garbage body stop returning 200?
BOOST_AUTO_TEST_CASE(patch_noop_bodies_return_200_unchanged)
{
    create_link("p19noop");
    patch("p19noop", R"({"tags":["keep"],"metadata":{"team":"core"}})");
    const auto before = get_body("p19noop");
    const auto before_updated = extract_updated_at(before);
    BOOST_TEST(is_rfc3339_utc(before_updated));

    BOOST_TEST(patch("p19noop", "{}").find("\"tags\":[\"keep\"]")
               != std::string::npos);
    BOOST_TEST(patch("p19noop", "not json at all").find("\"team\":\"core\"")
               != std::string::npos);
    BOOST_TEST(patch("p19noop", R"({"surprise":"ignored","url":"x"})")
                   .find("\"tags\":[\"keep\"]")
               != std::string::npos);

    // The stored view is unchanged by the no-op bodies on every field except
    // updated_at, which a no-op PATCH refreshes to the current wall-clock
    // second (so a naive full-body comparison flakes across a second
    // boundary). Compare all other fields byte-for-byte, and assert separately
    // that updated_at stays a valid RFC 3339 UTC timestamp that never moves
    // backwards.
    const auto after = get_body("p19noop");
    const auto after_updated = extract_updated_at(after);
    BOOST_TEST(without_updated_at(after) == without_updated_at(before));
    BOOST_TEST(is_rfc3339_utc(after_updated));
    BOOST_TEST(after_updated >= before_updated);
}

// Type: unit (target_url/url in the body is ignored).
// Scenario: a PATCH carrying a url (and target_url) leaves the stored target
//   unchanged - PATCH never mutates the target.
// Boundaries: the handler forwards only the known mutable keys; the target is
//   immutable through PATCH.
// On failure first check: did url/target_url leak into the link.update args?
BOOST_AUTO_TEST_CASE(patch_target_url_is_ignored)
{
    create_link("p19target");
    const auto original =
        url_shortener::linkRepository().getBySlug("p19target");
    BOOST_REQUIRE(original.has_value());

    patch("p19target",
          R"({"url":"https://evil.example/x",)"
          R"("target_url":"https://evil.example/y","enabled":true})");

    const auto after = url_shortener::linkRepository().getBySlug("p19target");
    BOOST_REQUIRE(after.has_value());
    BOOST_TEST(after->target_url == original->target_url);
}

// Type: unit (PATCH response equals the stored link's serialization).
// Scenario: the body a successful PATCH returns is byte-identical to the body a
//   subsequent GET of the same link returns (both are the registry serializer
//   over the same stored record).
// Boundaries: the PATCH 200 body is the serialized updated view, not a bespoke
//   shape.
// On failure first check: did the PATCH success path stop rendering the view
//   through the shared serializer?
BOOST_AUTO_TEST_CASE(patch_response_equals_stored_view)
{
    create_link("p19view");
    const auto patch_body =
        patch("p19view", R"({"tags":["a","b"],"enabled":false})");
    BOOST_TEST(patch_body == get_body("p19view"));
}
