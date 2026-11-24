#define BOOST_TEST_MODULE CommandsLinkReadCommandsTest
#include <string>

#include <boost/test/unit_test.hpp>

#include "../app/app_test_fakes.hpp"
#include "url_shortener/app/app_error.hpp"
#include "url_shortener/app/link_command_service.hpp"
#include "url_shortener/commands/command_registry.hpp"
#include "url_shortener/commands/envelope.hpp"
#include "url_shortener/commands/error_codes.hpp"
#include "url_shortener/commands/link_commands.hpp"
#include "url_shortener/core/types.hpp"

using url_shortener::Link;
using url_shortener::app::AppError;
using url_shortener::app::AppErrorCode;
using url_shortener::app::GetLinkBy;
using url_shortener::app::GetLinkQuery;
using url_shortener::app::GetLinkStatsQuery;
using url_shortener::app::LinkCommandService;
using url_shortener::app::serializeLinkLifetimeStatsJson;
using url_shortener::app::serializeLinkPreviewJson;
using url_shortener::app::serializeLinkStatsJson;
using url_shortener::app::serializeLinkViewJson;
using url_shortener::commands::CallerContext;
using url_shortener::commands::CommandErrorCode;
using url_shortener::commands::CommandRegistry;
using url_shortener::commands::CommandRequest;
using url_shortener::commands::CommandResponse;
using url_shortener::commands::make_link_get_descriptor;
using url_shortener::commands::make_link_preview_descriptor;
using url_shortener::commands::make_link_stats_descriptor;
using test_fakes::FakeLinkStatsReader;
using test_fakes::FakeLinkStore;
using test_fakes::makeTestConfig;

namespace
{

// Bundles the service and its fakes and registers the three read commands on a
// fresh registry, so each test gets an isolated store.
struct Harness
{
    FakeLinkStore store;
    FakeLinkStatsReader stats;
    ServerConfig config = makeTestConfig();
    LinkCommandService service {store, stats, config};
    CommandRegistry registry;

    Harness()
    {
        registry.add(make_link_get_descriptor(service));
        registry.add(make_link_preview_descriptor(service));
        registry.add(make_link_stats_descriptor(service));
    }

    CommandResponse run(const CallerContext& caller, CommandRequest request)
    {
        return registry.execute(caller, request);
    }
};

// A read-only User caller: the three descriptors require only Permission::read,
// so every one must be allowed for this role.
CallerContext user_caller()
{
    CallerContext caller;
    caller.principal = "test-user";
    caller.transport = "test";
    caller.control_set = ControlSet::User;
    return caller;
}

// Seeds a link with the given slug/id into the store (bypassing validation) and
// returns it, so the caller can build the expected serializer output.
Link seed_link(FakeLinkStore& store, const std::string& slug,
               const std::string& id)
{
    Link link;
    link.id = id;
    link.slug = slug;
    link.target_url = "https://example.com/" + slug;
    link.created_at = "2025-01-01T00:00:00Z";
    link.updated_at = "2025-01-02T00:00:00Z";
    link.enabled = true;
    link.stats.total_redirects = 11;
    link.stats.redirects_24h = 2;
    link.stats.redirects_7d = 5;
    link.stats.last_accessed_at = "2025-06-01T12:00:00Z";
    store.seed(link);
    return link;
}

CommandRequest request(std::string command, std::string args_json)
{
    CommandRequest req;
    req.id = "client-1";
    req.command = std::move(command);
    req.args_json = std::move(args_json);
    return req;
}

// Expected full-view JSON for a seeded slug, as the shared serializer produces.
std::string view_json(LinkCommandService& service, const std::string& slug)
{
    GetLinkQuery query;
    query.by = GetLinkBy::slug;
    query.value = slug;
    return serializeLinkViewJson(*service.GetLink(query).value);
}

// Expected reduced-preview JSON for a seeded slug.
std::string preview_json(LinkCommandService& service, const std::string& slug)
{
    GetLinkQuery query;
    query.by = GetLinkBy::slug;
    query.value = slug;
    return serializeLinkPreviewJson(*service.PreviewLink(query).value);
}

}  // namespace

// Type: unit (link.get by slug, parity C1/C3).
// Scenario: link.get by slug returns byte-equal serializeLinkViewJson of the
//   stored link.
// Boundaries: result_json comes from the shared serializer, not a reformat.
// On failure first check: did the handler stop calling link_view_result or pass
//   the wrong GetLinkQuery?
BOOST_AUTO_TEST_CASE(get_by_slug_matches_view_serializer)
{
    Harness h;
    seed_link(h.store, "get-slug", "id-get-1");

    const CommandResponse response =
        h.run(user_caller(), request("link.get", R"({"slug":"get-slug"})"));

    BOOST_TEST(response.ok);
    BOOST_TEST(response.result_json == view_json(h.service, "get-slug"));
}

// Type: unit (link.get by id, indexed path).
// Scenario: link.get by id resolves through the indexed findById lookup and
//   returns the same serializer output as the by-slug path.
// Boundaries: the id selector drives GetLinkBy::id; no scan.
// On failure first check: did decode_slug_or_id set by=id, or did GetLink fall
//   back to a slug lookup?
BOOST_AUTO_TEST_CASE(get_by_id_matches_view_serializer)
{
    Harness h;
    seed_link(h.store, "get-slug", "id-get-1");

    const CommandResponse response =
        h.run(user_caller(), request("link.get", R"({"id":"id-get-1"})"));

    BOOST_TEST(response.ok);
    BOOST_TEST(response.result_json == view_json(h.service, "get-slug"));
}

// Type: unit (one-of group, both present).
// Scenario: supplying both slug and id fails the registry's slug_or_id group
//   with invalid_field and a one_of_violation message, before the handler.
// Boundaries: the message carries the fixed one_of_violation reason, never a
//   value.
// On failure first check: did the two selectors lose their shared one_of_group?
BOOST_AUTO_TEST_CASE(get_both_selectors_is_one_of_violation)
{
    Harness h;
    const CommandResponse response = h.run(
        user_caller(),
        request("link.get", R"({"slug":"a","id":"b"})"));

    BOOST_TEST(!response.ok);
    BOOST_TEST((response.error.code == CommandErrorCode::invalid_field));
    BOOST_TEST(response.error.message.find("one_of_violation")
               != std::string::npos);
}

// Type: unit (one-of group, neither present).
// Scenario: supplying neither slug nor id also fails the group with
//   invalid_field / one_of_violation.
// Boundaries: exactly one selector is required; zero is rejected.
// On failure first check: did the group stop requiring a member?
BOOST_AUTO_TEST_CASE(get_no_selector_is_one_of_violation)
{
    Harness h;
    const CommandResponse response =
        h.run(user_caller(), request("link.get", R"({})"));

    BOOST_TEST(!response.ok);
    BOOST_TEST((response.error.code == CommandErrorCode::invalid_field));
    BOOST_TEST(response.error.message.find("one_of_violation")
               != std::string::npos);
}

// Type: unit (unknown slug).
// Scenario: link.get for an unseeded slug maps the service not_found to the
//   command not_found code.
// Boundaries: the error category is not_found, never leaking the slug value.
// On failure first check: did to_command_error stop mapping not_found?
BOOST_AUTO_TEST_CASE(get_unknown_slug_is_not_found)
{
    Harness h;
    const CommandResponse response =
        h.run(user_caller(), request("link.get", R"({"slug":"missing"})"));

    BOOST_TEST(!response.ok);
    BOOST_TEST((response.error.code == CommandErrorCode::not_found));
}

// Type: unit (unknown id).
// Scenario: link.get by an unseeded id maps to not_found.
// Boundaries: the indexed id lookup miss is not_found, not internal.
// On failure first check: did findById miss surface a different code?
BOOST_AUTO_TEST_CASE(get_unknown_id_is_not_found)
{
    Harness h;
    const CommandResponse response =
        h.run(user_caller(), request("link.get", R"({"id":"nope"})"));

    BOOST_TEST(!response.ok);
    BOOST_TEST((response.error.code == CommandErrorCode::not_found));
}

// Type: unit (preview parity, enabled link).
// Scenario: link.preview for an enabled link returns byte-equal
//   serializeLinkPreviewJson.
// Boundaries: result_json is the reduced projection from the shared serializer.
// On failure first check: did the handler stop calling link_preview_result?
BOOST_AUTO_TEST_CASE(preview_enabled_matches_preview_serializer)
{
    Harness h;
    seed_link(h.store, "prev-on", "id-prev-1");

    const CommandResponse response = h.run(
        user_caller(), request("link.preview", R"({"slug":"prev-on"})"));

    BOOST_TEST(response.ok);
    BOOST_TEST(response.result_json == preview_json(h.service, "prev-on"));
}

// Type: unit (preview by id).
// Scenario: link.preview by id resolves the same link and returns the reduced
//   projection.
// Boundaries: the id selector works for preview as for get.
// On failure first check: did PreviewLink receive the wrong discriminator?
BOOST_AUTO_TEST_CASE(preview_by_id_matches_preview_serializer)
{
    Harness h;
    seed_link(h.store, "prev-on", "id-prev-1");

    const CommandResponse response = h.run(
        user_caller(), request("link.preview", R"({"id":"id-prev-1"})"));

    BOOST_TEST(response.ok);
    BOOST_TEST(response.result_json == preview_json(h.service, "prev-on"));
}

// Type: unit (preview status, disabled link, success criterion 2).
// Scenario: a disabled link previews with "status":"disabled" and carries no
//   full-view-only keys (short_url, tags), byte-equal to the preview serializer.
// Boundaries: the reduced projection omits short_url/tags; status reflects the
//   disabled state.
// On failure first check: did resolveLinkStatus order change, or did the preview
//   serializer start leaking full-view keys?
BOOST_AUTO_TEST_CASE(preview_disabled_has_disabled_status_and_no_full_keys)
{
    Harness h;
    Link link = seed_link(h.store, "prev-off", "id-prev-2");
    link.enabled = false;
    h.store.seed(link);

    const CommandResponse response = h.run(
        user_caller(), request("link.preview", R"({"slug":"prev-off"})"));

    BOOST_TEST(response.ok);
    BOOST_TEST(response.result_json == preview_json(h.service, "prev-off"));
    BOOST_TEST(response.result_json.find("\"status\":\"disabled\"")
               != std::string::npos);
    BOOST_TEST(response.result_json.find("short_url") == std::string::npos);
    BOOST_TEST(response.result_json.find("\"tags\"") == std::string::npos);
}

// Type: unit (preview status, expired link).
// Scenario: a link whose expires_at is in the past previews with
//   "status":"expired", byte-equal to the preview serializer.
// Boundaries: an expired (but enabled, non-deleted) link resolves to expired.
// On failure first check: did isExpired or the status precedence change?
BOOST_AUTO_TEST_CASE(preview_expired_has_expired_status)
{
    Harness h;
    Link link = seed_link(h.store, "prev-exp", "id-prev-3");
    link.expires_at = "2000-01-01T00:00:00Z";
    h.store.seed(link);

    const CommandResponse response = h.run(
        user_caller(), request("link.preview", R"({"slug":"prev-exp"})"));

    BOOST_TEST(response.ok);
    BOOST_TEST(response.result_json == preview_json(h.service, "prev-exp"));
    BOOST_TEST(response.result_json.find("\"status\":\"expired\"")
               != std::string::npos);
}

// Type: unit (preview status, deleted link).
// Scenario: a soft-deleted link previews with "status":"deleted", byte-equal to
//   the preview serializer.
// Boundaries: a deleted link is still previewable (parity with REST) and
//   deleted takes precedence over disabled/expired.
// On failure first check: did PreviewLink begin filtering deleted links?
BOOST_AUTO_TEST_CASE(preview_deleted_has_deleted_status)
{
    Harness h;
    Link link = seed_link(h.store, "prev-del", "id-prev-4");
    link.deleted_at = "2024-01-01T00:00:00Z";
    h.store.seed(link);

    const CommandResponse response = h.run(
        user_caller(), request("link.preview", R"({"slug":"prev-del"})"));

    BOOST_TEST(response.ok);
    BOOST_TEST(response.result_json == preview_json(h.service, "prev-del"));
    BOOST_TEST(response.result_json.find("\"status\":\"deleted\"")
               != std::string::npos);
}

// Type: unit (stats range mode, parity).
// Scenario: with from/to/bucket all present, link.stats returns the range report
//   byte-equal to serializeLinkStatsJson of the reader's view.
// Boundaries: all three range args present selects GetLinkStats; the body is the
//   stats serializer output.
// On failure first check: did the range detection or link_stats_result change?
BOOST_AUTO_TEST_CASE(stats_range_mode_matches_stats_serializer)
{
    Harness h;
    seed_link(h.store, "stat-slug", "id-stat-1");

    GetLinkStatsQuery expected_query;
    expected_query.slug = "stat-slug";
    expected_query.from = "1700000000";
    expected_query.to = "1700003600";
    expected_query.bucket = "hour";
    const std::string expected =
        serializeLinkStatsJson(*h.service.GetLinkStats(expected_query).value);

    const CommandResponse response = h.run(
        user_caller(),
        request("link.stats",
                R"({"slug":"stat-slug","from":"1700000000",)"
                R"("to":"1700003600","bucket":"hour"})"));

    BOOST_TEST(response.ok);
    BOOST_TEST(response.result_json == expected);
}

// Type: unit (stats reader error, invalid_timestamp).
// Scenario: in range mode a reader invalid_timestamp maps to invalid_field with
//   the reader's exact "invalid_timestamp" message.
// Boundaries: timestamp validation lives in the reader; the handler never parses
//   from/to; the message is the reader detail verbatim.
// On failure first check: did the handler start parsing timestamps, or did
//   to_command_error rewrite the detail?
BOOST_AUTO_TEST_CASE(stats_reader_invalid_timestamp_is_invalid_field)
{
    Harness h;
    h.stats.forceError(AppError {AppErrorCode::invalid_field,
                                 "invalid_timestamp"});

    const CommandResponse response = h.run(
        user_caller(),
        request("link.stats",
                R"({"slug":"s","from":"bad","to":"1700003600","bucket":"hour"})"));

    BOOST_TEST(!response.ok);
    BOOST_TEST((response.error.code == CommandErrorCode::invalid_field));
    BOOST_TEST(response.error.message == "invalid_timestamp");
}

// Type: unit (free-string bucket, invalid_bucket, contract C3).
// Scenario: a bucket outside hour|day|week reaches the reader (not an enum
//   ArgSpec) and returns invalid_field with the reader's "invalid_bucket"
//   message, never the registry's "bucket: not_in_enum".
// Boundaries: bucket is a free string so the reader stays the sole validator and
//   the legacy invalid_bucket code is preserved.
// On failure first check: did bucket become an enumeration ArgSpec (pre-empting
//   the reader), or did the message change?
BOOST_AUTO_TEST_CASE(stats_invalid_bucket_comes_from_reader)
{
    Harness h;
    h.stats.forceError(AppError {AppErrorCode::invalid_field, "invalid_bucket"});

    const CommandResponse response = h.run(
        user_caller(),
        request("link.stats",
                R"({"slug":"s","from":"1700000000","to":"1700003600",)"
                R"("bucket":"fortnight"})"));

    BOOST_TEST(!response.ok);
    BOOST_TEST((response.error.code == CommandErrorCode::invalid_field));
    BOOST_TEST(response.error.message == "invalid_bucket");
    BOOST_TEST(response.error.message != "bucket: not_in_enum");
}

// Type: unit (stats lifetime mode, zero range args).
// Scenario: with only slug, link.stats returns the six-key lifetime body
//   byte-equal to serializeLinkLifetimeStatsJson of the stored link.
// Boundaries: no range args selects the GetLink/lifetime path, never the reader.
// On failure first check: did the range detection misfire, or the lifetime body
//   diverge from the shared serializer?
BOOST_AUTO_TEST_CASE(stats_lifetime_no_range_args)
{
    Harness h;
    seed_link(h.store, "life-slug", "id-life-1");
    const std::string expected = serializeLinkLifetimeStatsJson(
        *h.service.GetLink({GetLinkBy::slug, "life-slug"}).value);

    const CommandResponse response = h.run(
        user_caller(), request("link.stats", R"({"slug":"life-slug"})"));

    BOOST_TEST(response.ok);
    BOOST_TEST(response.result_json == expected);
}

// Type: unit (stats lifetime mode, one range arg ignored).
// Scenario: supplying slug + from (only) still yields the lifetime body; the
//   partial range arg is ignored exactly as REST ignores it.
// Boundaries: one of three range args present is not enough for range mode.
// On failure first check: did range detection trigger on a partial range?
BOOST_AUTO_TEST_CASE(stats_lifetime_one_range_arg_ignored)
{
    Harness h;
    seed_link(h.store, "life-slug", "id-life-1");
    const std::string expected = serializeLinkLifetimeStatsJson(
        *h.service.GetLink({GetLinkBy::slug, "life-slug"}).value);

    const CommandResponse response = h.run(
        user_caller(),
        request("link.stats", R"({"slug":"life-slug","from":"1700000000"})"));

    BOOST_TEST(response.ok);
    BOOST_TEST(response.result_json == expected);
}

// Type: unit (stats lifetime mode, two range args ignored).
// Scenario: supplying slug + from + to (no bucket) still yields the lifetime
//   body; the two partial range args are ignored.
// Boundaries: two of three range args present is still lifetime mode.
// On failure first check: did range detection trigger without bucket?
BOOST_AUTO_TEST_CASE(stats_lifetime_two_range_args_ignored)
{
    Harness h;
    seed_link(h.store, "life-slug", "id-life-1");
    const std::string expected = serializeLinkLifetimeStatsJson(
        *h.service.GetLink({GetLinkBy::slug, "life-slug"}).value);

    const CommandResponse response = h.run(
        user_caller(),
        request("link.stats",
                R"({"slug":"life-slug","from":"1700000000","to":"1700003600"})"));

    BOOST_TEST(response.ok);
    BOOST_TEST(response.result_json == expected);
}

// Type: unit (stats lifetime mode, unknown slug).
// Scenario: lifetime mode for an unseeded slug maps the GetLink not_found to the
//   command not_found code.
// Boundaries: the lifetime path reports not_found, never an empty body.
// On failure first check: did lifetime_stats_result mishandle the error result?
BOOST_AUTO_TEST_CASE(stats_lifetime_unknown_slug_is_not_found)
{
    Harness h;
    const CommandResponse response = h.run(
        user_caller(), request("link.stats", R"({"slug":"missing"})"));

    BOOST_TEST(!response.ok);
    BOOST_TEST((response.error.code == CommandErrorCode::not_found));
}

// Type: unit (read-only caller allowed for all three).
// Scenario: a ControlSet::User caller is permitted to run link.get,
//   link.preview and link.stats (all require only Permission::read).
// Boundaries: none of the three is denied for a read-only role.
// On failure first check: did a descriptor permission rise above read?
BOOST_AUTO_TEST_CASE(user_role_allowed_for_all_three)
{
    Harness h;
    seed_link(h.store, "ro-slug", "id-ro-1");

    BOOST_TEST(
        h.run(user_caller(), request("link.get", R"({"slug":"ro-slug"})")).ok);
    BOOST_TEST(h.run(user_caller(),
                     request("link.preview", R"({"slug":"ro-slug"})"))
                   .ok);
    BOOST_TEST(
        h.run(user_caller(), request("link.stats", R"({"slug":"ro-slug"})")).ok);
}
