#define BOOST_TEST_MODULE CommandsLinkCommandsRestParity
#include <chrono>
#include <cstdint>
#include <functional>
#include <regex>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include <boost/test/unit_test.hpp>
#include <url_shortener/analytics/click_event.hpp>
#include <url_shortener/app/link_command_service.hpp>
#include <url_shortener/commands/command_registry.hpp>
#include <url_shortener/commands/envelope.hpp>
#include <url_shortener/commands/error_codes.hpp>
#include <url_shortener/commands/link_commands.hpp>
#include <url_shortener/composition/link_command_service_factory.hpp>
#include <url_shortener/core/config.hpp>
#include <url_shortener/core/types.hpp>
#include <url_shortener/core/utils.hpp>
#include <url_shortener/http/router_builder.hpp>
#include <url_shortener/storage/link_repository.hpp>
#include <url_shortener/storage/memory/in_memory_click_event_repository.hpp>

namespace bhttp = boost::beast::http;
using url_shortener::Link;
using url_shortener::RedirectType;
using url_shortener::app::BuildLegacyLinkCommandService;
using url_shortener::app::LinkCommandServiceBundle;
using url_shortener::commands::CallerContext;
using url_shortener::commands::CommandErrorCode;
using url_shortener::commands::CommandRegistry;
using url_shortener::commands::CommandRequest;
using url_shortener::commands::CommandResponse;
using url_shortener::commands::error_info_for;
using url_shortener::commands::register_link_commands;
using url_shortener::http::BeastRequest;
using url_shortener::http::RouterBuilder;

// The process-global analytics click store the legacy stats reader aggregates
// over; declared here exactly as `link_handlers.cpp` does so the parity test
// seeds the same instance both the REST handler and the registry service read.
namespace url_shortener
{
namespace storage::memory
{
class InMemoryClickEventRepository;
}
storage::memory::InMemoryClickEventRepository& analyticsClickRepository();
}  // namespace url_shortener

namespace
{

using BeastResponse = bhttp::response<bhttp::string_body>;

// The exact store seam the REST handlers use (`makeCommandService` in
// `link_handlers.cpp` -> `app::BuildLegacyLinkCommandService`, which binds to
// `app::LinkBackend::store()`). With no store installed the holder's
// `LegacyLinkStore` default resolves to the global `linkRepository()`
// singleton, which is exactly the store the REST router reads and writes, so
// the registry service and the REST handlers share one store (parity
// precondition from the spec dependency on `LinkBackend`). The same
// `ServerConfig` instance is handed to both sides so `short_url` and
// slug-length behavior are identical.
struct ParityFixture
{
    ServerConfig config = parity_config();
    LinkCommandServiceBundle bundle = BuildLegacyLinkCommandService(config);
    CommandRegistry registry;

    ParityFixture() { register_link_commands(registry, *bundle.service); }

    // Dispatches a REST request through the real application router with the
    // fixture's config, so the REST side reads the same store/config the
    // command side does.
    BeastResponse dispatch(const BeastRequest& req) const
    {
        const auto router = RouterBuilder::buildApplicationRouter();
        return router.dispatch(req, config, false);
    }

    // Executes a command through the registry as an Admin (write permission),
    // mirroring the trusted REST adapter caller.
    CommandResponse run_cmd(const std::string& command,
                            const std::string& args_json) const
    {
        CommandRequest request;
        request.id = "parity";
        request.command = command;
        request.args_json = args_json;
        CallerContext caller;
        caller.principal = "parity";
        caller.transport = "test";
        caller.control_set = ControlSet::Admin;
        return registry.execute(caller, request);
    }

    // Minimal config matching the REST characterization harness: private
    // targets allowed (so the create cases exercise serialization, not SSRF
    // rejection) and a fixed generated slug length.
    static ServerConfig parity_config()
    {
        ServerConfig config;
        config.shortener_allow_private_targets = true;
        config.shortener_generated_slug_length = 8;
        return config;
    }
};

// Builds a request with Host + JSON content-type headers set.
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

// Builds an active link with fixed id and timestamps so two seeds that differ
// only in slug normalize to byte-identical bodies. `id` embeds the slug so the
// by-id REST route can address it; the id value itself is masked by
// normalize().
Link make_seed(const std::string& slug)
{
    Link link;
    link.id = "id-" + slug;
    link.slug = slug;
    link.target_url = "https://example.com/parity";
    link.created_at = "2020-01-01T00:00:00Z";
    link.updated_at = "2020-01-01T00:00:00Z";
    link.enabled = true;
    link.redirect_type = RedirectType::temporary;
    return link;
}

// Seeds a link directly into the shared legacy store (the same store both
// transports read) and drops any stale cache entry for its slug.
void seed(const Link& link)
{
    BOOST_REQUIRE(url_shortener::linkRepository().create(link));
    url_shortener::linkCache().erase(link.slug);
}

// Appends one click event for `slug` to the shared analytics store so a
// range-mode stats query over both slugs aggregates identically.
void seed_click(const std::string& slug,
                const std::uint16_t status_code,
                const std::string& domain,
                const std::int64_t epoch_seconds)
{
    url_shortener::analytics::ClickEvent event;
    event.event_id = "evt-" + slug + "-" + std::to_string(epoch_seconds);
    event.occurred_at = url_shortener::analytics::Timestamp(
        std::chrono::seconds(epoch_seconds));
    event.slug = slug;
    event.domain = domain;
    event.status_code = status_code;
    url_shortener::analyticsClickRepository().InsertBatch({event}, nullptr);
}

// Masks the generated values (id, RFC3339 timestamps) and the slug so two
// outputs produced on different slugs compare byte-for-byte. Order: timestamps,
// then the whole `"id":"..."` value, then every occurrence of the slug text.
std::string normalize(std::string body, const std::string& slug)
{
    static const std::regex timestamp(
        R"(\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2}(\.\d+)?Z)");
    body = std::regex_replace(body, timestamp, "<ts>");

    static const std::regex id_value(R"("id":"[^"]*")");
    body = std::regex_replace(body, id_value, R"("id":"<id>")");

    if (!slug.empty()) {
        std::string::size_type pos = 0;
        while ((pos = body.find(slug, pos)) != std::string::npos) {
            body.replace(pos, slug.size(), "<slug>");
            pos += 6;  // length of "<slug>"
        }
    }
    return body;
}

// Extracts the `"code":"..."` value from a REST error body, or "" when absent.
std::string extract_code(const std::string& body)
{
    const std::string key = "\"code\":\"";
    const auto pos = body.find(key);
    if (pos == std::string::npos) {
        return {};
    }
    const auto start = pos + key.size();
    const auto end = body.find('"', start);
    if (end == std::string::npos) {
        return {};
    }
    return body.substr(start, end - start);
}

// Asserts the command succeeded and its result JSON equals the REST body once
// both are normalized (contract C3: same serializers, byte-for-byte).
void expect_body_parity(const BeastResponse& rest,
                        const CommandResponse& cmd,
                        const std::string& rest_slug,
                        const std::string& cmd_slug,
                        const std::string& label)
{
    BOOST_TEST_REQUIRE(cmd.ok, label + " cmd failed: " + cmd.error.message);
    const std::string rest_norm = normalize(rest.body(), rest_slug);
    const std::string cmd_norm = normalize(cmd.result_json, cmd_slug);
    BOOST_TEST(rest_norm == cmd_norm,
               label << "\n REST: " << rest_norm << "\n  CMD: " << cmd_norm);
}

// Asserts the failure classes line up: the command carries `expected_code`, and
// the REST status/`code` equal that code's legacy_rest_* columns (the mapping
// Task 04.0 will reproduce). Used only for failures that originate in the
// service, not REST per-field 400s.
void expect_failure_parity(const BeastResponse& rest,
                           const CommandResponse& cmd,
                           const CommandErrorCode expected_code,
                           const std::string& label)
{
    BOOST_TEST(!cmd.ok, label);
    BOOST_TEST((cmd.error.code == expected_code), label);
    const auto& info = error_info_for(expected_code);
    BOOST_TEST(rest.result_int() == info.legacy_rest_status, label);
    BOOST_TEST(extract_code(rest.body()) == std::string(info.legacy_rest_code),
               label);
}

}  // namespace

// ---------------------------------------------------------------------------
// Success-body parity (contract C3). Each case seeds two links that differ only
// in slug (`par-rest-*` vs `par-cmd-*`) because the singleton store has no
// delete, runs the operation once through the REST router and once through the
// registry, and compares the two bodies after masking generated values.
// ---------------------------------------------------------------------------

// Type: unit (parity, create full args).
// Scenario: POST /api/v1/links and link.create with identical url/slug/tags/
//   metadata/campaign produce byte-identical 201 bodies after normalization.
// Boundaries: both go through the shared serializeLinkViewJson; REST reads
//   `url`, the command reads the canonical `target_url`.
// On failure first check: did the create handler's decode drift from
//   createLinkFromRequest, or did serializeLinkViewJson change on one side?
BOOST_AUTO_TEST_CASE(create_full_args_body_parity)
{
    ParityFixture f;
    const std::string rest_body =
        R"({"url":"https://example.com/parity","slug":"par-rest-create",)"
        R"("tags":["docs","release"],"metadata":{"team":"core"},)"
        R"("campaign":{"name":"spring","source":"news"}})";
    const auto rest =
        f.dispatch(make_request(bhttp::verb::post, "/api/v1/links", rest_body));
    BOOST_TEST(rest.result_int() == 201);

    const std::string cmd_args =
        R"({"target_url":"https://example.com/parity","slug":"par-cmd-create",)"
        R"("tags":["docs","release"],"metadata":{"team":"core"},)"
        R"("campaign":{"name":"spring","source":"news"}})";
    const auto cmd = f.run_cmd("link.create", cmd_args);

    expect_body_parity(
        rest, cmd, "par-rest-create", "par-cmd-create", "create");
}

// Type: unit (parity, get by slug).
// Scenario: GET /api/v1/links/{slug} and link.get {slug} return the same view.
// Boundaries: both resolve through the same store's findBySlug.
// On failure first check: did GetLink or serializeLinkViewJson diverge?
BOOST_AUTO_TEST_CASE(get_by_slug_body_parity)
{
    ParityFixture f;
    seed(make_seed("par-rest-get"));
    seed(make_seed("par-cmd-get"));

    const auto rest = f.dispatch(
        make_request(bhttp::verb::get, "/api/v1/links/par-rest-get"));
    const auto cmd = f.run_cmd("link.get", R"({"slug":"par-cmd-get"})");

    expect_body_parity(rest, cmd, "par-rest-get", "par-cmd-get", "get by slug");
}

// Type: unit (parity, get by id).
// Scenario: GET /api/v1/links/id/{id} and link.get {id} return the same view.
// Boundaries: both resolve through the indexed findById lookup.
// On failure first check: did the by-id selector decode or route change?
BOOST_AUTO_TEST_CASE(get_by_id_body_parity)
{
    ParityFixture f;
    seed(make_seed("par-rest-getid"));
    seed(make_seed("par-cmd-getid"));

    const auto rest = f.dispatch(
        make_request(bhttp::verb::get, "/api/v1/links/id/id-par-rest-getid"));
    const auto cmd = f.run_cmd("link.get", R"({"id":"id-par-cmd-getid"})");

    expect_body_parity(
        rest, cmd, "par-rest-getid", "par-cmd-getid", "get by id");
}

// Type: unit (parity, update with values).
// Scenario: PATCH and link.update applying enabled=false + tags return the same
//   post-update view.
// Boundaries: the PATCH `value` state replaces the field on both paths.
// On failure first check: did UpdateLink decode or persist differently?
BOOST_AUTO_TEST_CASE(update_value_body_parity)
{
    ParityFixture f;
    seed(make_seed("par-rest-updv"));
    seed(make_seed("par-cmd-updv"));

    const auto rest = f.dispatch(make_request(bhttp::verb::patch,
                                              "/api/v1/links/par-rest-updv",
                                              R"({"enabled":false,)"
                                              R"("tags":["docs"]})"));
    const auto cmd = f.run_cmd("link.update",
                               R"({"slug":"par-cmd-updv","enabled":false,)"
                               R"("tags":["docs"]})");

    expect_body_parity(
        rest, cmd, "par-rest-updv", "par-cmd-updv", "update value");
}

// Type: unit (parity, update null-clear).
// Scenario: PATCH {campaign:null} and link.update {campaign:null} both clear a
//   previously-set campaign and serialize campaign:null.
// Boundaries: the PATCH `null` state clears the nullable field on both paths.
// On failure first check: did patch_state_of or the REST null decode diverge?
BOOST_AUTO_TEST_CASE(update_null_clear_body_parity)
{
    ParityFixture f;
    Link rest_link = make_seed("par-rest-updnull");
    rest_link.campaign = Link::Campaign {};
    rest_link.campaign->name = "spring";
    seed(rest_link);
    Link cmd_link = make_seed("par-cmd-updnull");
    cmd_link.campaign = Link::Campaign {};
    cmd_link.campaign->name = "spring";
    seed(cmd_link);

    const auto rest = f.dispatch(make_request(bhttp::verb::patch,
                                              "/api/v1/links/par-rest-updnull",
                                              R"({"campaign":null})"));
    const auto cmd = f.run_cmd("link.update",
                               R"({"slug":"par-cmd-updnull","campaign":null})");

    expect_body_parity(
        rest, cmd, "par-rest-updnull", "par-cmd-updnull", "update null-clear");
}

// Type: unit (parity, update absent fields).
// Scenario: an empty update leaves all fields untouched and returns the view.
// Boundaries: the PATCH `absent` state is a no-op on both paths.
// On failure first check: did an absent field get treated as a clear?
BOOST_AUTO_TEST_CASE(update_absent_body_parity)
{
    ParityFixture f;
    seed(make_seed("par-rest-upda"));
    seed(make_seed("par-cmd-upda"));

    const auto rest = f.dispatch(make_request(
        bhttp::verb::patch, "/api/v1/links/par-rest-upda", R"({})"));
    const auto cmd = f.run_cmd("link.update", R"({"slug":"par-cmd-upda"})");

    expect_body_parity(
        rest, cmd, "par-rest-upda", "par-cmd-upda", "update absent");
}

// Type: unit (parity, delete).
// Scenario: DELETE and link.delete soft-delete and return the view with status
//   "deleted".
// Boundaries: delete sets deleted_at (masked) without disabling.
// On failure first check: did DeleteLink or the serializer change?
BOOST_AUTO_TEST_CASE(delete_body_parity)
{
    ParityFixture f;
    seed(make_seed("par-rest-del"));
    seed(make_seed("par-cmd-del"));

    const auto rest = f.dispatch(
        make_request(bhttp::verb::delete_, "/api/v1/links/par-rest-del"));
    const auto cmd = f.run_cmd("link.delete", R"({"slug":"par-cmd-del"})");

    expect_body_parity(rest, cmd, "par-rest-del", "par-cmd-del", "delete");
}

// Type: unit (parity, enable).
// Scenario: POST /enable and link.enable re-enable a disabled link.
// Boundaries: enable flips enabled to true on both paths.
// On failure first check: did the enable descriptor bind the wrong flag?
BOOST_AUTO_TEST_CASE(enable_body_parity)
{
    ParityFixture f;
    Link rest_link = make_seed("par-rest-en");
    rest_link.enabled = false;
    seed(rest_link);
    Link cmd_link = make_seed("par-cmd-en");
    cmd_link.enabled = false;
    seed(cmd_link);

    const auto rest = f.dispatch(
        make_request(bhttp::verb::post, "/api/v1/links/par-rest-en/enable"));
    const auto cmd = f.run_cmd("link.enable", R"({"slug":"par-cmd-en"})");

    expect_body_parity(rest, cmd, "par-rest-en", "par-cmd-en", "enable");
}

// Type: unit (parity, disable).
// Scenario: POST /disable and link.disable disable an enabled link.
// Boundaries: disable flips enabled to false on both paths.
// On failure first check: did the disable descriptor bind the wrong flag?
BOOST_AUTO_TEST_CASE(disable_body_parity)
{
    ParityFixture f;
    seed(make_seed("par-rest-dis"));
    seed(make_seed("par-cmd-dis"));

    const auto rest = f.dispatch(
        make_request(bhttp::verb::post, "/api/v1/links/par-rest-dis/disable"));
    const auto cmd = f.run_cmd("link.disable", R"({"slug":"par-cmd-dis"})");

    expect_body_parity(rest, cmd, "par-rest-dis", "par-cmd-dis", "disable");
}

// Type: unit (parity, restore).
// Scenario: POST /restore and link.restore clear deleted_at on a soft-deleted
//   link.
// Boundaries: restore clears deletion on both paths.
// On failure first check: did RestoreLink change which field it clears?
BOOST_AUTO_TEST_CASE(restore_body_parity)
{
    ParityFixture f;
    Link rest_link = make_seed("par-rest-res");
    rest_link.deleted_at = "2020-06-01T00:00:00Z";
    seed(rest_link);
    Link cmd_link = make_seed("par-cmd-res");
    cmd_link.deleted_at = "2020-06-01T00:00:00Z";
    seed(cmd_link);

    const auto rest = f.dispatch(
        make_request(bhttp::verb::post, "/api/v1/links/par-rest-res/restore"));
    const auto cmd = f.run_cmd("link.restore", R"({"slug":"par-cmd-res"})");

    expect_body_parity(rest, cmd, "par-rest-res", "par-cmd-res", "restore");
}

// Type: unit (parity, preview across statuses).
// Scenario: GET /preview and link.preview return the same reduced preview body
//   for an active, disabled, deleted and expired link.
// Boundaries: both use serializeLinkPreviewJson; status is derived identically.
// On failure first check: did PreviewLink or the preview serializer diverge?
BOOST_AUTO_TEST_CASE(preview_body_parity_across_statuses)
{
    ParityFixture f;

    struct variant
    {
        std::string suffix;
        std::function<void(Link&)> mutate;
    };
    const std::vector<variant> variants = {
        {"active", [](Link&) {}},
        {"disabled", [](Link& l) { l.enabled = false; }},
        {"deleted", [](Link& l) { l.deleted_at = "2020-06-01T00:00:00Z"; }},
        {"expired", [](Link& l) { l.expires_at = "2000-01-01T00:00:00Z"; }},
    };

    for (const variant& v : variants) {
        const std::string rest_slug = "par-rest-prev-" + v.suffix;
        const std::string cmd_slug = "par-cmd-prev-" + v.suffix;
        Link rest_link = make_seed(rest_slug);
        v.mutate(rest_link);
        seed(rest_link);
        Link cmd_link = make_seed(cmd_slug);
        v.mutate(cmd_link);
        seed(cmd_link);

        const auto rest = f.dispatch(make_request(
            bhttp::verb::get, "/api/v1/links/" + rest_slug + "/preview"));
        const auto cmd =
            f.run_cmd("link.preview", R"({"slug":")" + cmd_slug + R"("})");

        expect_body_parity(
            rest, cmd, rest_slug, cmd_slug, "preview " + v.suffix);
    }
}

// Type: unit (parity, lifetime stats).
// Scenario: GET /stats without a window and link.stats without from/to/bucket
//   return the same six-field lifetime-counter body.
// Boundaries: the command path's serializeLinkLifetimeStatsJson reproduces the
//   REST handler's hand-rolled body (contract C3 special case).
// On failure first check: did the lifetime serializer drift from
// handleLinkStats?
BOOST_AUTO_TEST_CASE(stats_lifetime_body_parity)
{
    ParityFixture f;
    Link rest_link = make_seed("par-rest-statsl");
    rest_link.stats.total_redirects = 5;
    rest_link.stats.redirects_24h = 2;
    rest_link.stats.redirects_7d = 4;
    seed(rest_link);
    Link cmd_link = make_seed("par-cmd-statsl");
    cmd_link.stats.total_redirects = 5;
    cmd_link.stats.redirects_24h = 2;
    cmd_link.stats.redirects_7d = 4;
    seed(cmd_link);

    const auto rest = f.dispatch(
        make_request(bhttp::verb::get, "/api/v1/links/par-rest-statsl/stats"));
    const auto cmd = f.run_cmd("link.stats", R"({"slug":"par-cmd-statsl"})");

    expect_body_parity(
        rest, cmd, "par-rest-statsl", "par-cmd-statsl", "stats lifetime");
}

// Type: unit (parity, range stats).
// Scenario: GET /stats?from&to&bucket and link.stats {from,to,bucket} aggregate
//   the same seeded click events and return the same range report.
// Boundaries: both read the shared analyticsClickRepository() via the same
//   stats reader; from/to/bucket are echoed in the body.
// On failure first check: did GetLinkStats or serializeLinkStatsJson diverge,
// or
//   were the two slugs seeded with differing events?
BOOST_AUTO_TEST_CASE(stats_range_body_parity)
{
    ParityFixture f;
    seed(make_seed("par-rest-statsr"));
    seed(make_seed("par-cmd-statsr"));
    // Identical events for both slugs so the aggregates match.
    for (const std::string& slug : {"par-rest-statsr", "par-cmd-statsr"}) {
        seed_click(slug, 302, "example.com", 1500000000);
        seed_click(slug, 302, "example.com", 1500100000);
    }

    const std::string window = "?from=1000000000&to=2000000000&bucket=day";
    const auto rest = f.dispatch(make_request(
        bhttp::verb::get, "/api/v1/links/par-rest-statsr/stats" + window));
    const auto cmd =
        f.run_cmd("link.stats",
                  R"({"slug":"par-cmd-statsr","from":"1000000000",)"
                  R"("to":"2000000000","bucket":"day"})");

    expect_body_parity(
        rest, cmd, "par-rest-statsr", "par-cmd-statsr", "stats range");
}

// ---------------------------------------------------------------------------
// Failure-class parity (contract C3). For failures that originate in the
// service, the REST status/code equal the command code's legacy_rest_* columns.
// ---------------------------------------------------------------------------

// Type: unit (parity, get unknown).
// Scenario: GET on an unknown slug is 404 not_found on both paths.
// Boundaries: not_found arises from the service's findBySlug on both.
// On failure first check: did the error mapping or status table change?
BOOST_AUTO_TEST_CASE(get_unknown_failure_parity)
{
    ParityFixture f;
    const auto rest = f.dispatch(
        make_request(bhttp::verb::get, "/api/v1/links/par-rest-ghost"));
    const auto cmd = f.run_cmd("link.get", R"({"slug":"par-cmd-ghost"})");

    expect_failure_parity(
        rest, cmd, CommandErrorCode::not_found, "get unknown");
}

// Type: unit (parity, create invalid url).
// Scenario: a malformed target URL is 400 invalid_url on both paths.
// Boundaries: invalid_url originates in the service's URL validator.
// On failure first check: did the URL validator or its error code change?
BOOST_AUTO_TEST_CASE(create_invalid_url_failure_parity)
{
    ParityFixture f;
    const auto rest = f.dispatch(
        make_request(bhttp::verb::post,
                     "/api/v1/links",
                     R"({"url":"not a url","slug":"par-rest-badurl"})"));
    const auto cmd = f.run_cmd(
        "link.create", R"({"target_url":"not a url","slug":"par-cmd-badurl"})");

    expect_failure_parity(
        rest, cmd, CommandErrorCode::invalid_url, "create invalid url");
}

// Type: unit (parity, reserved slug).
// Scenario: creating a reserved slug is 409 reserved_slug on both paths.
// Boundaries: reserved_slug is rejected before any persistence.
// On failure first check: did the reserved-slug check move or change code?
BOOST_AUTO_TEST_CASE(create_reserved_slug_failure_parity)
{
    ParityFixture f;
    const auto rest = f.dispatch(
        make_request(bhttp::verb::post,
                     "/api/v1/links",
                     R"({"url":"https://example.com/parity","slug":"api"})"));
    const auto cmd = f.run_cmd(
        "link.create",
        R"({"target_url":"https://example.com/parity","slug":"api"})");

    expect_failure_parity(
        rest, cmd, CommandErrorCode::reserved_slug, "reserved slug");
}

// Type: unit (parity, duplicate slug).
// Scenario: creating a slug already in use is 409 slug_conflict on both paths.
// Boundaries: slug_conflict arises from the store create on both.
// On failure first check: did the conflict mapping change?
BOOST_AUTO_TEST_CASE(create_duplicate_slug_failure_parity)
{
    ParityFixture f;
    seed(make_seed("par-rest-dup"));
    seed(make_seed("par-cmd-dup"));

    const auto rest = f.dispatch(make_request(
        bhttp::verb::post,
        "/api/v1/links",
        R"({"url":"https://example.com/parity","slug":"par-rest-dup"})"));
    const auto cmd = f.run_cmd(
        "link.create",
        R"({"target_url":"https://example.com/parity","slug":"par-cmd-dup"})");

    expect_failure_parity(
        rest, cmd, CommandErrorCode::slug_conflict, "duplicate slug");
}

// Type: unit (parity, update unknown).
// Scenario: PATCH on an unknown slug is 404 not_found on both paths (REST
// checks
//   existence before body validation; the command surfaces not_found from
//   UpdateLink).
// Boundaries: not_found precedes body validation on REST; the command body here
//   is otherwise valid so both agree on the class.
// On failure first check: did the PATCH existence check or UpdateLink mapping
//   change?
BOOST_AUTO_TEST_CASE(update_unknown_failure_parity)
{
    ParityFixture f;
    const auto rest = f.dispatch(make_request(bhttp::verb::patch,
                                              "/api/v1/links/par-rest-updghost",
                                              R"({"enabled":false})"));
    const auto cmd = f.run_cmd(
        "link.update", R"({"slug":"par-cmd-updghost","enabled":false})");

    expect_failure_parity(
        rest, cmd, CommandErrorCode::not_found, "update unknown");
}

// Type: unit (parity, stats invalid bucket; C3 special case).
// Scenario: a stats range with an unrecognized bucket is 400 on both paths; the
//   REST `code` is the detail-derived "invalid_bucket" while the command
//   carries invalid_field with "invalid_bucket" as its message. This
//   detail->code promotion is a REST-adapter concern (spec out-of-scope note;
//   Task 04.0 reproduces it via commands::detail_invalid_bucket), so it is NOT
//   a C3 mismatch: the status matches legacy_rest_status and the REST code
//   equals the command's error detail.
// Boundaries: the status matches
// error_info_for(invalid_field).legacy_rest_status
//   (400); the REST code is compared to the command detail, not
//   legacy_rest_code
//   ("invalid_request"), which the reader intentionally overrides.
// On failure first check: did the reader stop emitting the "invalid_bucket"
//   detail, or did statusForAppError change?
BOOST_AUTO_TEST_CASE(stats_invalid_bucket_failure_parity)
{
    ParityFixture f;
    seed(make_seed("par-rest-statsbad"));
    seed(make_seed("par-cmd-statsbad"));

    const std::string window = "?from=1000000000&to=2000000000&bucket=nope";
    const auto rest = f.dispatch(make_request(
        bhttp::verb::get, "/api/v1/links/par-rest-statsbad/stats" + window));
    const auto cmd =
        f.run_cmd("link.stats",
                  R"({"slug":"par-cmd-statsbad","from":"1000000000",)"
                  R"("to":"2000000000","bucket":"nope"})");

    BOOST_TEST(!cmd.ok);
    BOOST_TEST((cmd.error.code == CommandErrorCode::invalid_field));
    BOOST_TEST(cmd.error.message == "invalid_bucket");
    // Status parity against the error table; code parity against the detail the
    // REST adapter promotes to a code (not legacy_rest_code).
    BOOST_TEST(
        rest.result_int()
        == error_info_for(CommandErrorCode::invalid_field).legacy_rest_status);
    BOOST_TEST(extract_code(rest.body()) == "invalid_bucket");
}

// ---------------------------------------------------------------------------
// Structural checks: the registry holds exactly the nine link commands with the
// catalog's mutating/permission metadata.
// ---------------------------------------------------------------------------

// Type: unit (registration completeness).
// Scenario: register_link_commands on an empty registry registers exactly the
//   nine link command names and nothing else.
// Boundaries: the set is compared by name, independent of insertion order.
// On failure first check: did a factory get dropped or an extra command creep
// in?
BOOST_AUTO_TEST_CASE(registers_exactly_the_nine_link_commands)
{
    ParityFixture f;
    std::set<std::string> names;
    for (const auto* descriptor : f.registry.list()) {
        names.insert(descriptor->name);
    }
    const std::set<std::string> expected = {"link.create",
                                            "link.get",
                                            "link.update",
                                            "link.delete",
                                            "link.enable",
                                            "link.disable",
                                            "link.restore",
                                            "link.preview",
                                            "link.stats"};
    BOOST_TEST(names == expected);
}

// Type: unit (descriptor metadata matches the catalog).
// Scenario: each registered descriptor carries the catalog's mutating flag and
//   permission (write for the five mutations, read for get/preview/stats).
// Boundaries: metadata is asserted on the live descriptors, not re-derived.
// On failure first check: did a descriptor's permission or mutating flag
// change?
BOOST_AUTO_TEST_CASE(descriptor_metadata_matches_catalog)
{
    using url_shortener::commands::Permission;
    ParityFixture f;

    struct expectation
    {
        bool mutating;
        Permission permission;
    };
    const std::vector<std::pair<std::string, expectation>> expected = {
        {"link.create", {true, Permission::write}},
        {"link.update", {true, Permission::write}},
        {"link.delete", {true, Permission::write}},
        {"link.enable", {true, Permission::write}},
        {"link.disable", {true, Permission::write}},
        {"link.restore", {true, Permission::write}},
        {"link.get", {false, Permission::read}},
        {"link.preview", {false, Permission::read}},
        {"link.stats", {false, Permission::read}},
    };

    for (const auto& [name, want] : expected) {
        const auto* descriptor = f.registry.find(name);
        BOOST_REQUIRE_MESSAGE(descriptor != nullptr, name + " not registered");
        BOOST_TEST(descriptor->mutating == want.mutating, name);
        BOOST_TEST((descriptor->permission == want.permission), name);
    }
}
