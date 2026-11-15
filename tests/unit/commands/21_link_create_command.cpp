#define BOOST_TEST_MODULE CommandsLinkCreateCommandTest
#include <memory>
#include <string>
#include <vector>

#include <boost/test/unit_test.hpp>

#include "../app/app_test_fakes.hpp"
#include "url_shortener/app/link_command_service.hpp"
#include "url_shortener/commands/audit.hpp"
#include "url_shortener/commands/command_registry.hpp"
#include "url_shortener/commands/envelope.hpp"
#include "url_shortener/commands/error_codes.hpp"
#include "url_shortener/commands/link_commands.hpp"
#include "url_shortener/core/types.hpp"

using url_shortener::app::GetLinkBy;
using url_shortener::app::GetLinkQuery;
using url_shortener::app::LinkCommandService;
using url_shortener::app::LinkView;
using url_shortener::app::Result;
using url_shortener::app::serializeLinkViewJson;
using url_shortener::commands::AuditRecord;
using url_shortener::commands::CallerContext;
using url_shortener::commands::CommandErrorCode;
using url_shortener::commands::CommandRegistry;
using url_shortener::commands::CommandRequest;
using url_shortener::commands::CommandResponse;
using url_shortener::commands::IAuditSink;
using url_shortener::commands::make_link_create_descriptor;
using test_fakes::FakeLinkStatsReader;
using test_fakes::FakeLinkStore;
using test_fakes::makeTestConfig;

namespace
{

// A capturing audit sink for asserting the registry's audit behavior (item 2).
// record() is noexcept per the IAuditSink contract.
class CapturingAuditSink final : public IAuditSink
{
public:
    void record(const AuditRecord& record) noexcept override
    {
        records.push_back(record);
    }

    std::vector<AuditRecord> records;
};

// Bundles the service and its fakes so each test gets an isolated store, and
// registers link.create on a fresh registry.
struct Harness
{
    FakeLinkStore store;
    FakeLinkStatsReader stats;
    ServerConfig config = makeTestConfig();
    LinkCommandService service {store, stats, config};
    CommandRegistry registry;

    Harness() { registry.add(make_link_create_descriptor(service)); }

    CommandResponse run(const CallerContext& caller, CommandRequest request)
    {
        return registry.execute(caller, request);
    }
};

// An admin caller (write permission); the default CallerContext is a User.
CallerContext admin_caller()
{
    CallerContext caller;
    caller.principal = "test-admin";
    caller.transport = "test";
    caller.control_set = ControlSet::Admin;
    return caller;
}

CallerContext user_caller()
{
    CallerContext caller;
    caller.principal = "test-user";
    caller.transport = "test";
    caller.control_set = ControlSet::User;
    return caller;
}

// Builds a link.create request with the given args JSON (and optional dry_run).
CommandRequest create_request(std::string args_json, bool dry_run = false)
{
    CommandRequest request;
    request.id = "client-1";
    request.command = "link.create";
    request.args_json = std::move(args_json);
    request.options.dry_run = dry_run;
    return request;
}

// Re-reads the stored link by slug through the service and serializes it, so a
// create result can be compared byte-for-byte against the persisted projection.
std::string stored_view_json(LinkCommandService& service, const std::string& slug)
{
    GetLinkQuery query;
    query.by = GetLinkBy::slug;
    query.value = slug;
    const Result<LinkView> got = service.GetLink(query);
    BOOST_REQUIRE(got.ok());
    BOOST_REQUIRE(got.value.has_value());
    return serializeLinkViewJson(*got.value);
}

}  // namespace

// Type: unit (happy path, contract C1/C3 parity).
// Scenario: a minimal create (target_url + slug) succeeds and the result text is
//   byte-equal to serializeLinkViewJson of the link the store now holds.
// Boundaries: result_json comes from the shared serializer, not a reformat;
//   the link is actually persisted (re-readable through the service).
// On failure first check: did the handler stop calling serializeLinkViewJson,
//   or fail to pass target_url/slug into CreateLink?
BOOST_AUTO_TEST_CASE(minimal_create_result_matches_stored_view)
{
    Harness h;
    const CommandResponse response = h.run(
        admin_caller(),
        create_request(
            R"({"target_url":"https://example.com/a","slug":"my-slug"})"));

    BOOST_TEST(response.ok);
    BOOST_TEST((response.error.code == CommandErrorCode::none));
    BOOST_TEST(response.result_json == stored_view_json(h.service, "my-slug"));
    BOOST_TEST(h.store.findBySlug("my-slug").has_value());
}

// Type: unit (optional args round-trip).
// Scenario: a create carrying every optional arg persists them and the result
//   matches the stored projection, with redirect_type/enabled/campaign applied.
// Boundaries: enabled=false, redirect_type=permanent, campaign object, tags,
//   metadata, expires_at all flow through the handler into the service.
// On failure first check: did an optional field fail to decode (wrong
//   ArgsView accessor) or get dropped when present?
BOOST_AUTO_TEST_CASE(all_optional_args_round_trip)
{
    Harness h;
    const CommandResponse response = h.run(
        admin_caller(),
        create_request(
            R"({"target_url":"https://example.com/b","slug":"full-slug",)"
            R"("redirect_type":"permanent","expires_at":"2030-12-31T23:59:59Z",)"
            R"("enabled":false,"tags":["alpha","beta"],)"
            R"("metadata":{"team":"growth"},)"
            R"("campaign":{"name":"spring","source":"mail"}})"));

    BOOST_TEST(response.ok);
    BOOST_TEST(response.result_json == stored_view_json(h.service, "full-slug"));

    const auto stored = h.store.findBySlug("full-slug");
    BOOST_REQUIRE(stored.has_value());
    BOOST_TEST((stored->redirect_type
                == url_shortener::RedirectType::permanent));
    BOOST_TEST(stored->enabled == false);
    BOOST_REQUIRE_EQUAL(stored->tags.size(), 2u);
    BOOST_TEST(stored->metadata.at("team") == "growth");
    BOOST_REQUIRE(stored->campaign.has_value());
    BOOST_TEST((stored->campaign->name
                == std::optional<std::string>("spring")));
}

// Type: unit (campaign tri-state).
// Scenario: an explicit JSON null campaign means "no campaign"; the link is
//   created with campaign unset.
// Boundaries: null is distinct from a present object; decode_campaign is not
//   invoked for null.
// On failure first check: did the handler call decode_campaign on a null value
//   instead of treating it as absent?
BOOST_AUTO_TEST_CASE(campaign_null_stores_no_campaign)
{
    Harness h;
    const CommandResponse response = h.run(
        admin_caller(),
        create_request(
            R"({"target_url":"https://example.com/c","slug":"null-camp",)"
            R"("campaign":null})"));

    BOOST_TEST(response.ok);
    const auto stored = h.store.findBySlug("null-camp");
    BOOST_REQUIRE(stored.has_value());
    BOOST_TEST(!stored->campaign.has_value());
}

// Type: unit (required arg, ArgSpec gate).
// Scenario: omitting target_url is rejected by the registry before the handler
//   with invalid_field "target_url: missing".
// Boundaries: the message names the arg and a fixed reason, never a value.
// On failure first check: did target_url lose its required flag in the ArgSpec?
BOOST_AUTO_TEST_CASE(missing_target_url_is_invalid_field)
{
    Harness h;
    const CommandResponse response =
        h.run(admin_caller(), create_request(R"({})"));

    BOOST_TEST(!response.ok);
    BOOST_TEST((response.error.code == CommandErrorCode::invalid_field));
    BOOST_TEST(response.error.message == "target_url: missing");
}

// Type: unit (canonical name only, decision-log Q18).
// Scenario: the REST-only legacy key `url` is not a declared arg, so the
//   registry rejects it as unknown even when target_url is also present.
// Boundaries: only target_url is accepted; url is unknown.
// On failure first check: did someone add a url alias to the ArgSpec?
BOOST_AUTO_TEST_CASE(legacy_url_key_is_unknown)
{
    Harness h;
    const CommandResponse response = h.run(
        admin_caller(),
        create_request(
            R"({"target_url":"https://example.com/d","url":"https://evil"})"));

    BOOST_TEST(!response.ok);
    BOOST_TEST((response.error.code == CommandErrorCode::invalid_field));
    BOOST_TEST(response.error.message == "url: unknown");
}

// Type: unit (server-managed field rejected).
// Scenario: deleted_at is server-managed and not a declared arg, so it is
//   rejected as unknown.
// Boundaries: deleted_at can never be supplied at create time.
// On failure first check: did deleted_at creep into the ArgSpec?
BOOST_AUTO_TEST_CASE(deleted_at_key_is_unknown)
{
    Harness h;
    const CommandResponse response = h.run(
        admin_caller(),
        create_request(
            R"({"target_url":"https://example.com/e",)"
            R"("deleted_at":"2020-01-01T00:00:00Z"})"));

    BOOST_TEST(!response.ok);
    BOOST_TEST((response.error.code == CommandErrorCode::invalid_field));
    BOOST_TEST(response.error.message == "deleted_at: unknown");
}

// Type: unit (nested object validation).
// Scenario: an undeclared member inside campaign is rejected by the registry's
//   recursive object check.
// Boundaries: campaign members are limited to the six declared fields.
// On failure first check: did the campaign field list drift?
BOOST_AUTO_TEST_CASE(unknown_campaign_member_is_invalid_field)
{
    Harness h;
    const CommandResponse response = h.run(
        admin_caller(),
        create_request(
            R"({"target_url":"https://example.com/f",)"
            R"("campaign":{"bogus":"x"}})"));

    BOOST_TEST(!response.ok);
    BOOST_TEST((response.error.code == CommandErrorCode::invalid_field));
}

// Type: unit (SSRF/URL validation stays in the service).
// Scenario: a malformed (non-http/https) URL yields invalid_url from the
//   service; the handler performs no URL check itself.
// Boundaries: the error is value-free and indistinguishable from an SSRF block.
// On failure first check: did URL validation leak into the handler/ArgSpec?
BOOST_AUTO_TEST_CASE(malformed_url_is_invalid_url)
{
    Harness h;
    const CommandResponse response = h.run(
        admin_caller(),
        create_request(R"({"target_url":"ftp://example.com/x"})"));

    BOOST_TEST(!response.ok);
    BOOST_TEST((response.error.code == CommandErrorCode::invalid_url));
}

// Type: unit (SSRF indistinguishability, threat model T-53 invariant).
// Scenario: a private-host target with shortener_allow_private_targets off maps
//   to invalid_url, the same code a malformed URL produces.
// Boundaries: a private target is indistinguishable from a bad URL (no probe).
// On failure first check: did private-host handling change, or allow_private
//   default flip in makeTestConfig?
BOOST_AUTO_TEST_CASE(private_target_is_invalid_url)
{
    Harness h;
    const CommandResponse response = h.run(
        admin_caller(),
        create_request(R"({"target_url":"http://127.0.0.1/admin"})"));

    BOOST_TEST(!response.ok);
    BOOST_TEST((response.error.code == CommandErrorCode::invalid_url));
}

// Type: unit (reserved slug).
// Scenario: a system-reserved slug is rejected with reserved_slug.
// Boundaries: reserved names are refused before persistence.
// On failure first check: did the reserved set or its mapping change?
BOOST_AUTO_TEST_CASE(reserved_slug_is_rejected)
{
    Harness h;
    const CommandResponse response = h.run(
        admin_caller(),
        create_request(
            R"({"target_url":"https://example.com/g","slug":"admin"})"));

    BOOST_TEST(!response.ok);
    BOOST_TEST((response.error.code == CommandErrorCode::reserved_slug));
}

// Type: unit (invalid slug).
// Scenario: a slug failing format validation is rejected with invalid_slug.
// Boundaries: slug syntax is enforced by the service, not the ArgSpec.
// On failure first check: did isValidSlug's pattern change?
BOOST_AUTO_TEST_CASE(invalid_slug_is_rejected)
{
    Harness h;
    const CommandResponse response = h.run(
        admin_caller(),
        create_request(
            R"({"target_url":"https://example.com/h","slug":"ab"})"));

    BOOST_TEST(!response.ok);
    BOOST_TEST((response.error.code == CommandErrorCode::invalid_slug));
}

// Type: unit (duplicate slug).
// Scenario: creating with a slug that already exists yields slug_conflict and
//   nothing new is persisted.
// Boundaries: the conflict is detected via the store's existence check.
// On failure first check: did chooseSlug stop consulting slugExists?
BOOST_AUTO_TEST_CASE(duplicate_slug_is_conflict)
{
    Harness h;
    h.store.preseedSlugExists("taken");
    const CommandResponse response = h.run(
        admin_caller(),
        create_request(
            R"({"target_url":"https://example.com/i","slug":"taken"})"));

    BOOST_TEST(!response.ok);
    BOOST_TEST((response.error.code == CommandErrorCode::slug_conflict));
}

// Type: unit (tag limit, invalid-field category).
// Scenario: exceeding the service tag-count limit yields invalid_field; the
//   registry's generous caps do not pre-empt the service validator (C3).
// Boundaries: the service, not the ArgSpec, enforces the 20-tag limit.
// On failure first check: did the ArgSpec max_items drop below the service
//   limit and start rejecting first with a different message?
BOOST_AUTO_TEST_CASE(too_many_tags_is_invalid_field)
{
    Harness h;
    std::string tags = "[";
    for (int i = 0; i < 21; ++i) {
        if (i != 0) {
            tags += ",";
        }
        tags += "\"t" + std::to_string(i) + "\"";
    }
    tags += "]";

    const CommandResponse response = h.run(
        admin_caller(),
        create_request(R"({"target_url":"https://example.com/j","tags":)"
                       + tags + "}"));

    BOOST_TEST(!response.ok);
    BOOST_TEST((response.error.code == CommandErrorCode::invalid_field));
}

// Type: unit (bad timestamp).
// Scenario: a non-RFC3339 expires_at is rejected with invalid_field (the
//   timestamp ArgSpec gates it before the service even runs).
// Boundaries: the result is invalid_field regardless of which layer rejects.
// On failure first check: did expires_at lose its timestamp ArgType?
BOOST_AUTO_TEST_CASE(bad_expires_at_is_invalid_field)
{
    Harness h;
    const CommandResponse response = h.run(
        admin_caller(),
        create_request(
            R"({"target_url":"https://example.com/k",)"
            R"("expires_at":"not-a-date"})"));

    BOOST_TEST(!response.ok);
    BOOST_TEST((response.error.code == CommandErrorCode::invalid_field));
}

// Type: unit (forced storage failure).
// Scenario: a repository create failure surfaces as storage_failure on the real
//   (non-dry-run) path.
// Boundaries: storage_failure only arises from a real persistence attempt.
// On failure first check: did the handler swallow the store error or route to
//   the dry-run variant?
BOOST_AUTO_TEST_CASE(forced_store_failure_is_storage_failure)
{
    Harness h;
    h.store.forceCreateStorageFailure();
    const CommandResponse response = h.run(
        admin_caller(),
        create_request(
            R"({"target_url":"https://example.com/l","slug":"store-fail"})"));

    BOOST_TEST(!response.ok);
    BOOST_TEST((response.error.code == CommandErrorCode::storage_failure));
}

// Type: unit (dry_run non-mutation, R-A/T-57).
// Scenario: a dry_run create validates and returns an ok result but writes
//   nothing: the store stays empty afterwards.
// Boundaries: ValidateCreateLink is exercised; no record is persisted.
// On failure first check: did the handler route dry_run to CreateLink, or did
//   context.dry_run stop reaching the handler?
BOOST_AUTO_TEST_CASE(dry_run_validates_without_persisting)
{
    Harness h;
    const CommandResponse response = h.run(
        admin_caller(),
        create_request(
            R"({"target_url":"https://example.com/m","slug":"dry-slug"})",
            /*dry_run=*/true));

    BOOST_TEST(response.ok);
    BOOST_TEST(!h.store.findBySlug("dry-slug").has_value());
}

// Type: unit (dry_run slug-conflict probe, item 2).
// Scenario: a dry_run create with an explicit slug that already exists reveals
//   slug_conflict (the slug-existence probe runs under dry_run too).
// Boundaries: dry_run still runs the full validator including chooseSlug.
// On failure first check: did ValidateCreateLink stop calling slugExists?
BOOST_AUTO_TEST_CASE(dry_run_reveals_slug_conflict)
{
    Harness h;
    h.store.preseedSlugExists("taken");
    const CommandResponse response = h.run(
        admin_caller(),
        create_request(
            R"({"target_url":"https://example.com/n","slug":"taken"})",
            /*dry_run=*/true));

    BOOST_TEST(!response.ok);
    BOOST_TEST((response.error.code == CommandErrorCode::slug_conflict));
}

// Type: unit (authorization, item 1).
// Scenario: a read-only User is denied link.create with not_authorized, both
//   with and without dry_run, and nothing is persisted. Denial precedes the
//   handler (write permission is required for both modes, never read).
// Boundaries: dry_run does not relax the permission; the store is untouched.
// On failure first check: did the descriptor permission drop to read, or did
//   the registry start checking permission after dispatch?
BOOST_AUTO_TEST_CASE(user_denied_with_and_without_dry_run)
{
    Harness h;

    const CommandResponse plain = h.run(
        user_caller(),
        create_request(
            R"({"target_url":"https://example.com/o","slug":"user-plain"})"));
    BOOST_TEST(!plain.ok);
    BOOST_TEST((plain.error.code == CommandErrorCode::not_authorized));
    BOOST_TEST(!h.store.findBySlug("user-plain").has_value());

    const CommandResponse dry = h.run(
        user_caller(),
        create_request(
            R"({"target_url":"https://example.com/p","slug":"user-dry"})",
            /*dry_run=*/true));
    BOOST_TEST(!dry.ok);
    BOOST_TEST((dry.error.code == CommandErrorCode::not_authorized));
    BOOST_TEST(!h.store.findBySlug("user-dry").has_value());
}

// Type: unit (audit emission for a real create, threat model T-55).
// Scenario: a successful create emits exactly one audit record whose target is
//   the created slug and whose outcome is "ok"; the audit line carries no URL.
// Boundaries: audit_target is the validated slug, never target_url.
// On failure first check: did the registry stop auditing mutating successes, or
//   did link_view_result pick the wrong audit target?
BOOST_AUTO_TEST_CASE(successful_create_is_audited_with_slug_target)
{
    Harness h;
    auto sink = std::make_shared<CapturingAuditSink>();
    h.registry.set_audit_sink(sink);

    const CommandResponse response = h.run(
        admin_caller(),
        create_request(
            R"({"target_url":"https://example.com/q","slug":"audited"})"));

    BOOST_TEST(response.ok);
    BOOST_REQUIRE_EQUAL(sink->records.size(), 1u);
    BOOST_TEST(sink->records[0].command == "link.create");
    BOOST_TEST(sink->records[0].target == "audited");
    BOOST_TEST(sink->records[0].outcome == "ok");
    BOOST_TEST(sink->records[0].target.find("example.com")
               == std::string::npos);
}

// Type: unit (dry_run is audited, item 2).
// Scenario: a dry_run create of this mutating command emits exactly one audit
//   record with outcome "dry_run"; dry-run attempts are audited like real ones.
// Boundaries: the registry already audits dry_run executions of mutating
//   commands (the 01.0 pipeline needed no change).
// On failure first check: did resolve_dispatch stop flagging audit_dry_run, or
//   did the mutating audit emission become conditional on a real write?
BOOST_AUTO_TEST_CASE(dry_run_create_is_audited)
{
    Harness h;
    auto sink = std::make_shared<CapturingAuditSink>();
    h.registry.set_audit_sink(sink);

    const CommandResponse response = h.run(
        admin_caller(),
        create_request(
            R"({"target_url":"https://example.com/r","slug":"dry-audit"})",
            /*dry_run=*/true));

    BOOST_TEST(response.ok);
    BOOST_REQUIRE_EQUAL(sink->records.size(), 1u);
    BOOST_TEST(sink->records[0].command == "link.create");
    BOOST_TEST(sink->records[0].outcome == "dry_run");
    BOOST_TEST(!h.store.findBySlug("dry-audit").has_value());
}

// Type: unit (denial is audited, threat model T-55).
// Scenario: a User-denied create still emits one audit record (outcome
//   not_authorized) with an empty target, proving denials of a mutating command
//   are audited without echoing any argument value.
// Boundaries: the denial record carries no slug or URL.
// On failure first check: did the registry stop emitting an audit line on
//   denial of a mutating command?
BOOST_AUTO_TEST_CASE(denied_create_is_audited_without_values)
{
    Harness h;
    auto sink = std::make_shared<CapturingAuditSink>();
    h.registry.set_audit_sink(sink);

    const CommandResponse response = h.run(
        user_caller(),
        create_request(
            R"({"target_url":"https://example.com/s","slug":"denied"})"));

    BOOST_TEST(!response.ok);
    BOOST_REQUIRE_EQUAL(sink->records.size(), 1u);
    BOOST_TEST(sink->records[0].outcome == "not_authorized");
    BOOST_TEST(sink->records[0].target.empty());
}
