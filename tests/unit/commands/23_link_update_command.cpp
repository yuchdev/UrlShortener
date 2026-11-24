#define BOOST_TEST_MODULE CommandsLinkUpdateCommandTest
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
#include "url_shortener/core/utils.hpp"

using url_shortener::Link;
using url_shortener::RedirectType;
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
using url_shortener::commands::make_link_update_descriptor;
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
// registers link.update on a fresh registry.
struct Harness
{
    FakeLinkStore store;
    FakeLinkStatsReader stats;
    ServerConfig config = makeTestConfig();
    LinkCommandService service {store, stats, config};
    CommandRegistry registry;

    Harness() { registry.add(make_link_update_descriptor(service)); }

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

// Builds a link.update request with the given args JSON (and optional dry_run).
CommandRequest update_request(std::string args_json, bool dry_run = false)
{
    CommandRequest request;
    request.id = "client-1";
    request.command = "link.update";
    request.args_json = std::move(args_json);
    request.options.dry_run = dry_run;
    return request;
}

// Seeds a fully populated link so every PATCH field has a non-default prior
// value to observe. updated_at is stamped in the distant past so a real update
// (which refreshes updated_at to the wall clock) is always detectable.
Link seed_link(FakeLinkStore& store, const std::string& slug)
{
    Link link;
    link.id = "id-" + slug;
    link.slug = slug;
    link.target_url = "https://target.example.com/path";
    link.created_at = "2000-01-01T00:00:00Z";
    link.updated_at = "2000-01-01T00:00:00Z";
    link.enabled = true;
    link.expires_at = "2999-01-01T00:00:00Z";
    link.tags = {"old-tag"};
    link.metadata = {{"old-key", "old-value"}};
    Link::Campaign campaign;
    campaign.name = "old-campaign";
    link.campaign = campaign;
    link.redirect_type = RedirectType::temporary;
    store.seed(link);
    return link;
}

// Re-reads the stored link by slug through the service and serializes it, so an
// update result can be compared byte-for-byte against the persisted projection.
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

// Type: unit (happy path, no-op body except updated_at).
// Scenario: an update addressing only slug persists no field change; every
//   stored field except updated_at is byte-identical, and the result text is
//   byte-equal to serializeLinkViewJson of the stored record afterwards.
// Boundaries: updated_at is refreshed off the seeded distant-past value; all
//   other fields survive untouched; result_json comes from the shared
//   serializer, not a reformat.
// On failure first check: did the handler set a field it should have left
//   absent, or stop calling serializeLinkViewJson via link_view_result?
BOOST_AUTO_TEST_CASE(slug_only_update_changes_nothing_but_updated_at)
{
    Harness h;
    const Link before = seed_link(h.store, "only-slug");

    const CommandResponse response = h.run(
        admin_caller(), update_request(R"({"slug":"only-slug"})"));

    BOOST_TEST(response.ok);
    BOOST_TEST((response.error.code == CommandErrorCode::none));
    BOOST_TEST(response.result_json == stored_view_json(h.service, "only-slug"));

    const auto after = h.store.findBySlug("only-slug");
    BOOST_REQUIRE(after.has_value());
    BOOST_TEST(after->enabled == before.enabled);
    BOOST_TEST((after->expires_at == before.expires_at));
    BOOST_TEST(after->tags == before.tags);
    BOOST_TEST((after->metadata == before.metadata));
    BOOST_REQUIRE(after->campaign.has_value());
    BOOST_TEST((after->campaign->name == before.campaign->name));
    BOOST_TEST(after->updated_at != before.updated_at);
}

// Type: unit (enabled toggle, present bool).
// Scenario: enabled:false then enabled:true each persist, proving the present
//   bool is applied verbatim in both directions.
// Boundaries: a present false is not mistaken for absent; a later true flips it
//   back.
// On failure first check: did the handler guard on truthiness instead of has()?
BOOST_AUTO_TEST_CASE(enabled_false_then_true_round_trips)
{
    Harness h;
    seed_link(h.store, "toggle");

    const CommandResponse off = h.run(
        admin_caller(), update_request(R"({"slug":"toggle","enabled":false})"));
    BOOST_TEST(off.ok);
    BOOST_TEST(h.store.findBySlug("toggle")->enabled == false);

    const CommandResponse on = h.run(
        admin_caller(), update_request(R"({"slug":"toggle","enabled":true})"));
    BOOST_TEST(on.ok);
    BOOST_TEST(h.store.findBySlug("toggle")->enabled == true);
}

// Type: unit (expires_at tri-state: value replaces).
// Scenario: a present expires_at value replaces the stored expiry.
// Boundaries: the value path sets both optional layers of the command.
// On failure first check: did patch_state::value stop reading get_string?
BOOST_AUTO_TEST_CASE(expires_at_value_replaces)
{
    Harness h;
    seed_link(h.store, "exp-value");

    const CommandResponse response = h.run(
        admin_caller(),
        update_request(
            R"({"slug":"exp-value","expires_at":"2030-06-01T12:00:00Z"})"));

    BOOST_TEST(response.ok);
    const auto stored = h.store.findBySlug("exp-value");
    BOOST_REQUIRE(stored.has_value());
    BOOST_TEST((stored->expires_at
                == std::optional<std::string>("2030-06-01T12:00:00Z")));
}

// Type: unit (expires_at tri-state: explicit null clears).
// Scenario: an explicit JSON null expires_at clears the stored expiry.
// Boundaries: null is distinct from absent; the inner optional is emptied.
// On failure first check: did patch_state::null_value collapse into absent?
BOOST_AUTO_TEST_CASE(expires_at_null_clears)
{
    Harness h;
    seed_link(h.store, "exp-null");

    const CommandResponse response = h.run(
        admin_caller(),
        update_request(R"({"slug":"exp-null","expires_at":null})"));

    BOOST_TEST(response.ok);
    const auto stored = h.store.findBySlug("exp-null");
    BOOST_REQUIRE(stored.has_value());
    BOOST_TEST(!stored->expires_at.has_value());
}

// Type: unit (expires_at tri-state: absent keeps).
// Scenario: an update that omits expires_at leaves the stored expiry intact.
// Boundaries: an absent nullable field must leave the double-optional nullopt.
// On failure first check: did the handler default-construct the field when
//   absent instead of skipping it?
BOOST_AUTO_TEST_CASE(expires_at_absent_keeps)
{
    Harness h;
    const Link before = seed_link(h.store, "exp-keep");

    const CommandResponse response = h.run(
        admin_caller(), update_request(R"({"slug":"exp-keep","enabled":false})"));

    BOOST_TEST(response.ok);
    const auto stored = h.store.findBySlug("exp-keep");
    BOOST_REQUIRE(stored.has_value());
    BOOST_TEST((stored->expires_at == before.expires_at));
}

// Type: unit (campaign tri-state: value replaces).
// Scenario: a present campaign object replaces the stored campaign via
//   decode_campaign.
// Boundaries: the value path decodes the nested object.
// On failure first check: did patch_state::value stop calling decode_campaign?
BOOST_AUTO_TEST_CASE(campaign_value_replaces)
{
    Harness h;
    seed_link(h.store, "camp-value");

    const CommandResponse response = h.run(
        admin_caller(),
        update_request(
            R"({"slug":"camp-value","campaign":{"name":"spring"}})"));

    BOOST_TEST(response.ok);
    const auto stored = h.store.findBySlug("camp-value");
    BOOST_REQUIRE(stored.has_value());
    BOOST_REQUIRE(stored->campaign.has_value());
    BOOST_TEST((stored->campaign->name
                == std::optional<std::string>("spring")));
}

// Type: unit (campaign tri-state: explicit null clears).
// Scenario: an explicit JSON null campaign clears the stored campaign.
// Boundaries: null is distinct from absent and from a present object.
// On failure first check: did the handler call decode_campaign on a null value?
BOOST_AUTO_TEST_CASE(campaign_null_clears)
{
    Harness h;
    seed_link(h.store, "camp-null");

    const CommandResponse response = h.run(
        admin_caller(),
        update_request(R"({"slug":"camp-null","campaign":null})"));

    BOOST_TEST(response.ok);
    const auto stored = h.store.findBySlug("camp-null");
    BOOST_REQUIRE(stored.has_value());
    BOOST_TEST(!stored->campaign.has_value());
}

// Type: unit (campaign tri-state: absent keeps).
// Scenario: an update that omits campaign leaves the stored campaign intact.
// Boundaries: an absent nullable object leaves the double-optional nullopt.
// On failure first check: did the handler clear campaign when it was absent?
BOOST_AUTO_TEST_CASE(campaign_absent_keeps)
{
    Harness h;
    const Link before = seed_link(h.store, "camp-keep");

    const CommandResponse response = h.run(
        admin_caller(),
        update_request(R"({"slug":"camp-keep","enabled":false})"));

    BOOST_TEST(response.ok);
    const auto stored = h.store.findBySlug("camp-keep");
    BOOST_REQUIRE(stored.has_value());
    BOOST_REQUIRE(stored->campaign.has_value());
    BOOST_TEST((stored->campaign->name == before.campaign->name));
}

// Type: unit (tags replacement).
// Scenario: a present tags list replaces the whole stored tag list.
// Boundaries: the prior single tag is fully replaced, not merged.
// On failure first check: did the handler append instead of assign?
BOOST_AUTO_TEST_CASE(tags_replacement_overwrites)
{
    Harness h;
    seed_link(h.store, "tags-set");

    const CommandResponse response = h.run(
        admin_caller(),
        update_request(R"({"slug":"tags-set","tags":["alpha","beta"]})"));

    BOOST_TEST(response.ok);
    const auto stored = h.store.findBySlug("tags-set");
    BOOST_REQUIRE(stored.has_value());
    BOOST_REQUIRE_EQUAL(stored->tags.size(), 2u);
    BOOST_TEST(stored->tags[0] == "alpha");
    BOOST_TEST(stored->tags[1] == "beta");
}

// Type: unit (empty tags clears).
// Scenario: a present empty tags list removes every stored tag.
// Boundaries: [] is a present replacement, not an absent field.
// On failure first check: did has("tags") stop firing for an empty array?
BOOST_AUTO_TEST_CASE(empty_tags_clears)
{
    Harness h;
    seed_link(h.store, "tags-clear");

    const CommandResponse response = h.run(
        admin_caller(), update_request(R"({"slug":"tags-clear","tags":[]})"));

    BOOST_TEST(response.ok);
    const auto stored = h.store.findBySlug("tags-clear");
    BOOST_REQUIRE(stored.has_value());
    BOOST_TEST(stored->tags.empty());
}

// Type: unit (empty metadata clears).
// Scenario: a present empty metadata object removes every stored entry.
// Boundaries: {} is a present replacement, not an absent field.
// On failure first check: did has("metadata") stop firing for an empty object?
BOOST_AUTO_TEST_CASE(empty_metadata_clears)
{
    Harness h;
    seed_link(h.store, "meta-clear");

    const CommandResponse response = h.run(
        admin_caller(), update_request(R"({"slug":"meta-clear","metadata":{}})"));

    BOOST_TEST(response.ok);
    const auto stored = h.store.findBySlug("meta-clear");
    BOOST_REQUIRE(stored.has_value());
    BOOST_TEST(stored->metadata.empty());
}

// Type: unit (unknown slug).
// Scenario: updating a slug that does not exist returns not_found; the service,
//   not the handler, performs the existence check.
// Boundaries: not_found arises from the service's findBySlug.
// On failure first check: did the handler short-circuit existence itself?
BOOST_AUTO_TEST_CASE(unknown_slug_is_not_found)
{
    Harness h;

    const CommandResponse response = h.run(
        admin_caller(), update_request(R"({"slug":"ghost","enabled":false})"));

    BOOST_TEST(!response.ok);
    BOOST_TEST((response.error.code == CommandErrorCode::not_found));
}

// Type: unit (bad timestamp).
// Scenario: a non-RFC3339 expires_at is rejected with invalid_field (the
//   timestamp ArgSpec gates it before the service even runs).
// Boundaries: the result is invalid_field regardless of which layer rejects.
// On failure first check: did expires_at lose its timestamp ArgType?
BOOST_AUTO_TEST_CASE(bad_expires_at_is_invalid_field)
{
    Harness h;
    seed_link(h.store, "bad-exp");

    const CommandResponse response = h.run(
        admin_caller(),
        update_request(R"({"slug":"bad-exp","expires_at":"not-a-date"})"));

    BOOST_TEST(!response.ok);
    BOOST_TEST((response.error.code == CommandErrorCode::invalid_field));
}

// Type: unit (tag limit, invalid-field category, C3 outer-cap proof).
// Scenario: exceeding the service tag-count limit (20) yields invalid_field with
//   the service's own fixed message; the 21 tags sit far below the registry's
//   generous outer cap (k_collection_max_items=4096), proving the outer cap does
//   not pre-empt the service validator.
// Boundaries: the message is validateTags' fixed text, never the ArgSpec
//   "<name>: ..." form; the stored record is unchanged.
// On failure first check: did the ArgSpec max_items drop below the service
//   limit and reject first with a different message?
BOOST_AUTO_TEST_CASE(too_many_tags_is_invalid_field)
{
    Harness h;
    const Link before = seed_link(h.store, "tag-limit");

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
        update_request(R"({"slug":"tag-limit","tags":)" + tags + "}"));

    BOOST_TEST(!response.ok);
    BOOST_TEST((response.error.code == CommandErrorCode::invalid_field));
    BOOST_TEST(response.error.message == "tags violate constraints");
    BOOST_TEST(h.store.findBySlug("tag-limit")->tags == before.tags);
}

// Type: unit (metadata limit, invalid-field category, C3 outer-cap proof).
// Scenario: exceeding the service metadata-entry limit (50) yields invalid_field
//   with the service's own fixed message; the 51 entries sit far below the outer
//   cap, proving it does not pre-empt the service validator.
// Boundaries: the message is validateMetadata's fixed text (the UpdateLink
//   wording), never the ArgSpec "<name>: ..." form; nothing is persisted.
// On failure first check: did the ArgSpec max_items drop below 50, or did
//   validateMetadata's limit or message change?
BOOST_AUTO_TEST_CASE(too_many_metadata_entries_is_invalid_field)
{
    Harness h;
    const Link before = seed_link(h.store, "meta-limit");

    std::string metadata = "{";
    for (int i = 0; i < 51; ++i) {
        if (i != 0) {
            metadata += ",";
        }
        metadata += "\"k" + std::to_string(i) + "\":\"v\"";
    }
    metadata += "}";

    const CommandResponse response = h.run(
        admin_caller(),
        update_request(R"({"slug":"meta-limit","metadata":)" + metadata + "}"));

    BOOST_TEST(!response.ok);
    BOOST_TEST((response.error.code == CommandErrorCode::invalid_field));
    BOOST_TEST(response.error.message
               == "metadata must be flat object with string values");
    BOOST_TEST((h.store.findBySlug("meta-limit")->metadata == before.metadata));
}

// Type: unit (campaign field limit, invalid-field category, C3 outer-cap proof).
// Scenario: a campaign member value over validateCampaign's 128-char per-field
//   limit yields invalid_field with the service's own fixed message; the 129-char
//   value sits far below the registry's free-text cap (k_text_max_length=65536),
//   proving the outer cap does not pre-empt the service validator.
// Boundaries: the message is validateCampaign's fixed text, never the ArgSpec
//   "<name>: ..." form; nothing is persisted.
// On failure first check: did the campaign field ArgSpec max_length drop below
//   128, or did validateCampaign's limit or message change?
BOOST_AUTO_TEST_CASE(oversized_campaign_field_is_invalid_field)
{
    Harness h;
    const Link before = seed_link(h.store, "camp-limit");
    const std::string long_name(129, 'x');

    const CommandResponse response = h.run(
        admin_caller(),
        update_request(R"({"slug":"camp-limit","campaign":{"name":")" + long_name
                       + R"("}})"));

    BOOST_TEST(!response.ok);
    BOOST_TEST((response.error.code == CommandErrorCode::invalid_field));
    BOOST_TEST(response.error.message == "campaign fields exceed limits");
    BOOST_REQUIRE(h.store.findBySlug("camp-limit")->campaign.has_value());
    BOOST_TEST((h.store.findBySlug("camp-limit")->campaign->name
                == before.campaign->name));
}

// Type: unit (server-managed field rejected).
// Scenario: deleted_at is server-managed and not a declared arg, so the registry
//   rejects it as unknown.
// Boundaries: deleted_at can never be supplied through an update.
// On failure first check: did deleted_at creep into the ArgSpec?
BOOST_AUTO_TEST_CASE(deleted_at_key_is_unknown)
{
    Harness h;
    seed_link(h.store, "del-key");

    const CommandResponse response = h.run(
        admin_caller(),
        update_request(
            R"({"slug":"del-key","deleted_at":"2020-01-01T00:00:00Z"})"));

    BOOST_TEST(!response.ok);
    BOOST_TEST((response.error.code == CommandErrorCode::invalid_field));
    BOOST_TEST(response.error.message == "deleted_at: unknown");
}

// Type: unit (immutable field rejected).
// Scenario: target_url is immutable through this command and is not a declared
//   arg, so the registry rejects it as unknown.
// Boundaries: the destination URL cannot be changed via link.update (same as
//   REST PATCH today).
// On failure first check: did target_url get added to the update ArgSpec?
BOOST_AUTO_TEST_CASE(target_url_key_is_unknown)
{
    Harness h;
    seed_link(h.store, "url-key");

    const CommandResponse response = h.run(
        admin_caller(),
        update_request(
            R"({"slug":"url-key","target_url":"https://example.com/new"})"));

    BOOST_TEST(!response.ok);
    BOOST_TEST((response.error.code == CommandErrorCode::invalid_field));
    BOOST_TEST(response.error.message == "target_url: unknown");
}

// Type: unit (wrong type).
// Scenario: a string where enabled expects a bool is rejected by the registry
//   with invalid_field, and the message names the arg and a fixed reason.
// Boundaries: the message is "enabled: wrong_type", never the supplied value.
// On failure first check: did enabled lose its boolean ArgType?
BOOST_AUTO_TEST_CASE(wrong_type_enabled_is_invalid_field)
{
    Harness h;
    seed_link(h.store, "wrong-type");

    const CommandResponse response = h.run(
        admin_caller(),
        update_request(R"({"slug":"wrong-type","enabled":"yes"})"));

    BOOST_TEST(!response.ok);
    BOOST_TEST((response.error.code == CommandErrorCode::invalid_field));
    BOOST_TEST(response.error.message == "enabled: wrong_type");
}

// Type: unit (forced storage failure).
// Scenario: a repository update failure surfaces as storage_failure on the real
//   (non-dry-run) path.
// Boundaries: storage_failure only arises from a real persistence attempt.
// On failure first check: did the handler swallow the store error or route to
//   the dry-run variant?
BOOST_AUTO_TEST_CASE(forced_store_failure_is_storage_failure)
{
    Harness h;
    seed_link(h.store, "store-fail");
    h.store.forceUpdateStorageFailure();

    const CommandResponse response = h.run(
        admin_caller(),
        update_request(R"({"slug":"store-fail","enabled":false})"));

    BOOST_TEST(!response.ok);
    BOOST_TEST((response.error.code == CommandErrorCode::storage_failure));
}

// Type: unit (dry_run non-mutation, R-A/T-57; requirement 4).
// Scenario: a dry_run update returns an ok view reflecting the requested changes
//   but writes nothing: the stored record stays byte-identical, including
//   updated_at.
// Boundaries: ValidateUpdateLink is exercised; the view shows enabled=false and
//   the new expiry while the store keeps the seeded values.
// On failure first check: did the handler route dry_run to UpdateLink, or did
//   context.dry_run stop reaching the handler?
BOOST_AUTO_TEST_CASE(dry_run_validates_without_persisting)
{
    Harness h;
    const Link before = seed_link(h.store, "dry-slug");

    const CommandResponse response = h.run(
        admin_caller(),
        update_request(
            R"({"slug":"dry-slug","enabled":false,)"
            R"("expires_at":"2030-06-01T12:00:00Z"})",
            /*dry_run=*/true));

    BOOST_TEST(response.ok);

    // The stored record is byte-identical to the pre-call snapshot.
    const auto after = h.store.findBySlug("dry-slug");
    BOOST_REQUIRE(after.has_value());
    BOOST_TEST(after->enabled == before.enabled);
    BOOST_TEST((after->expires_at == before.expires_at));
    BOOST_TEST(after->tags == before.tags);
    BOOST_TEST((after->metadata == before.metadata));
    BOOST_TEST(after->updated_at == before.updated_at);
}

// Type: unit (authorization, requirement 1).
// Scenario: a read-only User is denied link.update with not_authorized, both
//   with and without dry_run, and the stored record is untouched. Denial
//   precedes the handler (write permission is required for both modes).
// Boundaries: dry_run does not relax the permission; the store is untouched.
// On failure first check: did the descriptor permission drop to read, or did
//   the registry start checking permission after dispatch?
BOOST_AUTO_TEST_CASE(user_denied_with_and_without_dry_run)
{
    Harness h;
    const Link before = seed_link(h.store, "user-deny");

    const CommandResponse plain = h.run(
        user_caller(),
        update_request(R"({"slug":"user-deny","enabled":false})"));
    BOOST_TEST(!plain.ok);
    BOOST_TEST((plain.error.code == CommandErrorCode::not_authorized));
    BOOST_TEST(h.store.findBySlug("user-deny")->enabled == before.enabled);

    const CommandResponse dry = h.run(
        user_caller(),
        update_request(R"({"slug":"user-deny","enabled":false})",
                       /*dry_run=*/true));
    BOOST_TEST(!dry.ok);
    BOOST_TEST((dry.error.code == CommandErrorCode::not_authorized));
    BOOST_TEST(h.store.findBySlug("user-deny")->enabled == before.enabled);
}

// Type: unit (audit emission for a real update, requirement 3; threat T-57).
// Scenario: a successful update emits exactly one audit record whose target is
//   the addressed slug and whose outcome is "ok"; the audit line carries no URL
//   or body value.
// Boundaries: audit_target is the addressed slug, never target_url/body.
// On failure first check: did link_view_result pick the wrong audit target?
BOOST_AUTO_TEST_CASE(successful_update_is_audited_with_slug_target)
{
    Harness h;
    seed_link(h.store, "audited");
    auto sink = std::make_shared<CapturingAuditSink>();
    h.registry.set_audit_sink(sink);

    const CommandResponse response = h.run(
        admin_caller(), update_request(R"({"slug":"audited","enabled":false})"));

    BOOST_TEST(response.ok);
    BOOST_REQUIRE_EQUAL(sink->records.size(), 1u);
    BOOST_TEST(sink->records[0].command == "link.update");
    BOOST_TEST(sink->records[0].target == "audited");
    BOOST_TEST(sink->records[0].outcome == "ok");
}

// Type: unit (dry_run is audited, requirement 2).
// Scenario: a dry_run update of this mutating command emits exactly one audit
//   record with outcome "dry_run" and the addressed slug as target; dry-run
//   attempts are audited exactly like real ones and the store stays unchanged.
// Boundaries: the 01.0 pipeline already audits dry_run executions of mutating
//   commands (no pipeline change was needed).
// On failure first check: did resolve_dispatch stop flagging audit_dry_run, or
//   did the mutating audit emission become conditional on a real write?
BOOST_AUTO_TEST_CASE(dry_run_update_is_audited)
{
    Harness h;
    const Link before = seed_link(h.store, "dry-audit");
    auto sink = std::make_shared<CapturingAuditSink>();
    h.registry.set_audit_sink(sink);

    const CommandResponse response = h.run(
        admin_caller(),
        update_request(R"({"slug":"dry-audit","enabled":false})",
                       /*dry_run=*/true));

    BOOST_TEST(response.ok);
    BOOST_REQUIRE_EQUAL(sink->records.size(), 1u);
    BOOST_TEST(sink->records[0].command == "link.update");
    BOOST_TEST(sink->records[0].target == "dry-audit");
    BOOST_TEST(sink->records[0].outcome == "dry_run");
    BOOST_TEST(h.store.findBySlug("dry-audit")->enabled == before.enabled);
}
