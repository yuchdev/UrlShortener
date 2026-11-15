#define BOOST_TEST_MODULE CommandsLinkLifecycleCommandsTest
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

using test_fakes::FakeLinkStatsReader;
using test_fakes::FakeLinkStore;
using test_fakes::makeTestConfig;
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
using url_shortener::commands::make_link_delete_descriptor;
using url_shortener::commands::make_link_disable_descriptor;
using url_shortener::commands::make_link_enable_descriptor;
using url_shortener::commands::make_link_restore_descriptor;

namespace
{

// A capturing audit sink for asserting the registry's audit behavior
// (requirement 3). record() is noexcept per the IAuditSink contract.
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
// registers all four lifecycle commands on one fresh registry (they form a
// single handler family).
struct Harness
{
    FakeLinkStore store;
    FakeLinkStatsReader stats;
    ServerConfig config = makeTestConfig();
    LinkCommandService service {store, stats, config};
    CommandRegistry registry;

    Harness()
    {
        registry.add(make_link_delete_descriptor(service));
        registry.add(make_link_enable_descriptor(service));
        registry.add(make_link_disable_descriptor(service));
        registry.add(make_link_restore_descriptor(service));
    }

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

// Builds a lifecycle request for the given command and args JSON (optional
// dry_run). All four lifecycle commands take the same `{slug}` argument shape.
CommandRequest lifecycle_request(std::string command,
                                 std::string args_json,
                                 bool dry_run = false)
{
    CommandRequest request;
    request.id = "client-1";
    request.command = std::move(command);
    request.args_json = std::move(args_json);
    request.options.dry_run = dry_run;
    return request;
}

// Seeds an enabled, non-deleted link with a distant-past updated_at so any real
// mutation (which refreshes updated_at) is always detectable.
Link seed_link(FakeLinkStore& store, const std::string& slug)
{
    Link link;
    link.id = "id-" + slug;
    link.slug = slug;
    link.target_url = "https://target.example.com/path";
    link.created_at = "2000-01-01T00:00:00Z";
    link.updated_at = "2000-01-01T00:00:00Z";
    link.enabled = true;
    link.redirect_type = RedirectType::temporary;
    store.seed(link);
    return link;
}

// Re-reads the stored link by slug through the service and serializes it, so a
// command result can be compared byte-for-byte against the persisted projection
// and before/after snapshots can prove the store was (un)changed.
std::string stored_view_json(LinkCommandService& service,
                             const std::string& slug)
{
    GetLinkQuery query;
    query.by = GetLinkBy::slug;
    query.value = slug;
    const Result<LinkView> got = service.GetLink(query);
    BOOST_REQUIRE(got.ok());
    BOOST_REQUIRE(got.value.has_value());
    return serializeLinkViewJson(*got.value);
}

// The four lifecycle command names, for the parametrized cross-command cases.
const std::vector<std::string>& all_commands()
{
    static const std::vector<std::string> names = {
        "link.delete", "link.enable", "link.disable", "link.restore"};
    return names;
}

}  // namespace

// Type: unit (happy path, delete; requirement 4/5).
// Scenario: link.delete sets deleted_at and updated_at, leaves enabled
//   untouched, reports status "deleted", and returns text byte-equal to
//   serializeLinkViewJson of the stored record afterwards.
// Boundaries: delete does not disable the link; the result comes from the
// shared
//   serializer, not a reformat.
// On failure first check: did the handler stop routing through
// link_view_result,
//   or did DeleteLink begin toggling enabled?
BOOST_AUTO_TEST_CASE(delete_soft_deletes_without_disabling)
{
    Harness h;
    const Link before = seed_link(h.store, "del-ok");

    const CommandResponse response =
        h.run(admin_caller(),
              lifecycle_request("link.delete", R"({"slug":"del-ok"})"));

    BOOST_TEST(response.ok);
    BOOST_TEST((response.error.code == CommandErrorCode::none));
    BOOST_TEST(response.result_json == stored_view_json(h.service, "del-ok"));
    BOOST_TEST(response.result_json.find("\"status\":\"deleted\"")
               != std::string::npos);

    const auto after = h.store.findBySlug("del-ok");
    BOOST_REQUIRE(after.has_value());
    BOOST_REQUIRE(after->deleted_at.has_value());
    BOOST_TEST(after->enabled == before.enabled);  // delete does not disable
    BOOST_TEST(after->enabled == true);
    BOOST_TEST(after->updated_at != before.updated_at);
}

// Type: unit (happy path, disable; requirement 5).
// Scenario: link.disable flips enabled to false, persists it, and returns text
//   byte-equal to serializeLinkViewJson of the stored record.
// Boundaries: disable changes only enabled; deleted_at stays unset.
// On failure first check: did SetLinkEnabled receive the wrong flag?
BOOST_AUTO_TEST_CASE(disable_sets_enabled_false)
{
    Harness h;
    seed_link(h.store, "dis-ok");

    const CommandResponse response =
        h.run(admin_caller(),
              lifecycle_request("link.disable", R"({"slug":"dis-ok"})"));

    BOOST_TEST(response.ok);
    BOOST_TEST(response.result_json == stored_view_json(h.service, "dis-ok"));
    const auto after = h.store.findBySlug("dis-ok");
    BOOST_REQUIRE(after.has_value());
    BOOST_TEST(after->enabled == false);
    BOOST_TEST(!after->deleted_at.has_value());
}

// Type: unit (happy path, enable; requirement 5).
// Scenario: link.enable flips a disabled link back to enabled and returns text
//   byte-equal to serializeLinkViewJson of the stored record.
// Boundaries: enable sets the true flag verbatim.
// On failure first check: did the enable descriptor bind the false flag?
BOOST_AUTO_TEST_CASE(enable_sets_enabled_true)
{
    Harness h;
    Link link = seed_link(h.store, "en-ok");
    link.enabled = false;
    h.store.seed(link);

    const CommandResponse response =
        h.run(admin_caller(),
              lifecycle_request("link.enable", R"({"slug":"en-ok"})"));

    BOOST_TEST(response.ok);
    BOOST_TEST(response.result_json == stored_view_json(h.service, "en-ok"));
    BOOST_TEST(h.store.findBySlug("en-ok")->enabled == true);
}

// Type: unit (happy path, restore; requirement 4/5).
// Scenario: link.restore on a soft-deleted link clears deleted_at only and
//   returns text byte-equal to serializeLinkViewJson of the stored record.
// Boundaries: restore clears deleted_at and touches nothing else but
// updated_at. On failure first check: did RestoreLink reset a field other than
// deleted_at?
BOOST_AUTO_TEST_CASE(restore_clears_deleted_at_only)
{
    Harness h;
    Link link = seed_link(h.store, "res-ok");
    link.deleted_at = "2001-01-01T00:00:00Z";
    link.enabled = false;  // prove restore does not re-enable
    h.store.seed(link);

    const CommandResponse response =
        h.run(admin_caller(),
              lifecycle_request("link.restore", R"({"slug":"res-ok"})"));

    BOOST_TEST(response.ok);
    BOOST_TEST(response.result_json == stored_view_json(h.service, "res-ok"));
    const auto after = h.store.findBySlug("res-ok");
    BOOST_REQUIRE(after.has_value());
    BOOST_TEST(!after->deleted_at.has_value());
    BOOST_TEST(after->enabled == false);  // restore does not re-enable
}

// Type: unit (idempotent re-delete; requirement 4).
// Scenario: deleting an already-deleted link succeeds and overwrites
// deleted_at;
//   a delete -> restore -> delete sequence stays well-shaped (each step ok and
//   the final state has deleted_at set again).
// Boundaries: re-delete is not rejected; restore between deletes clears then
// the
//   second delete re-sets deleted_at.
// On failure first check: did DeleteLink start rejecting an already-deleted
//   link, or did restore fail to clear deleted_at?
BOOST_AUTO_TEST_CASE(delete_restore_delete_is_idempotent_in_shape)
{
    Harness h;
    seed_link(h.store, "cycle");

    const CommandResponse d1 =
        h.run(admin_caller(),
              lifecycle_request("link.delete", R"({"slug":"cycle"})"));
    BOOST_TEST(d1.ok);
    const auto first_deleted_at = h.store.findBySlug("cycle")->deleted_at;
    BOOST_REQUIRE(first_deleted_at.has_value());

    const CommandResponse r =
        h.run(admin_caller(),
              lifecycle_request("link.restore", R"({"slug":"cycle"})"));
    BOOST_TEST(r.ok);
    BOOST_TEST(!h.store.findBySlug("cycle")->deleted_at.has_value());

    const CommandResponse d2 =
        h.run(admin_caller(),
              lifecycle_request("link.delete", R"({"slug":"cycle"})"));
    BOOST_TEST(d2.ok);
    BOOST_REQUIRE(h.store.findBySlug("cycle")->deleted_at.has_value());
}

// Type: unit (enable on deleted keeps status deleted; requirement 4).
// Scenario: enabling a soft-deleted link succeeds, leaves deleted_at set, and
//   the computed status in the result stays "deleted".
// Boundaries: enable does not clear deletion; status derivation still reports
//   deleted while deleted_at is present.
// On failure first check: did SetLinkEnabled start clearing deleted_at, or did
//   status derivation stop giving deletion precedence?
BOOST_AUTO_TEST_CASE(enable_on_deleted_link_keeps_status_deleted)
{
    Harness h;
    Link link = seed_link(h.store, "en-del");
    link.enabled = false;
    link.deleted_at = "2001-01-01T00:00:00Z";
    h.store.seed(link);

    const CommandResponse response =
        h.run(admin_caller(),
              lifecycle_request("link.enable", R"({"slug":"en-del"})"));

    BOOST_TEST(response.ok);
    BOOST_TEST(h.store.findBySlug("en-del")->enabled == true);
    BOOST_REQUIRE(h.store.findBySlug("en-del")->deleted_at.has_value());
    BOOST_TEST(response.result_json.find("\"status\":\"deleted\"")
               != std::string::npos);
}

// Type: unit (disable then enable round trip).
// Scenario: disable then enable on the same link lands back at enabled == true,
//   proving the two descriptors bind opposite flags.
// Boundaries: a present-false then present-true both apply.
// On failure first check: did one of the two descriptors bind the wrong flag?
BOOST_AUTO_TEST_CASE(disable_then_enable_round_trips)
{
    Harness h;
    seed_link(h.store, "round");

    const CommandResponse off =
        h.run(admin_caller(),
              lifecycle_request("link.disable", R"({"slug":"round"})"));
    BOOST_TEST(off.ok);
    BOOST_TEST(h.store.findBySlug("round")->enabled == false);

    const CommandResponse on =
        h.run(admin_caller(),
              lifecycle_request("link.enable", R"({"slug":"round"})"));
    BOOST_TEST(on.ok);
    BOOST_TEST(h.store.findBySlug("round")->enabled == true);
}

// Type: unit (unknown slug; requirement 5).
// Scenario: each of the four commands against a slug that does not exist
// returns
//   not_found; the service, not the handler, performs the existence check.
// Boundaries: not_found arises from the service's findBySlug.
// On failure first check: did a handler short-circuit existence itself?
BOOST_AUTO_TEST_CASE(unknown_slug_is_not_found_for_each)
{
    for (const std::string& command : all_commands()) {
        Harness h;  // empty store
        const CommandResponse response = h.run(
            admin_caller(), lifecycle_request(command, R"({"slug":"ghost"})"));
        BOOST_TEST(!response.ok, command);
        BOOST_TEST((response.error.code == CommandErrorCode::not_found),
                   command);
    }
}

// Type: unit (empty-string slug; requirement 5; documented rule).
// Scenario: an empty-string slug is accepted by the registry (the required-arg
//   rule checks presence, not emptiness) and reaches the service, which reports
//   not_found. Asserted and documented per the spec's either/or choice.
// Boundaries: "" is present-but-missing-in-store, so not_found (service
//   semantics), not invalid_field.
// On failure first check: did the slug ArgSpec gain a min_length, flipping this
//   to invalid_field?
BOOST_AUTO_TEST_CASE(empty_slug_is_not_found_for_each)
{
    for (const std::string& command : all_commands()) {
        Harness h;
        const CommandResponse response =
            h.run(admin_caller(), lifecycle_request(command, R"({"slug":""})"));
        BOOST_TEST(!response.ok, command);
        BOOST_TEST((response.error.code == CommandErrorCode::not_found),
                   command);
    }
}

// Type: unit (missing slug; requirement 5).
// Scenario: omitting the required slug is rejected by the registry with
//   invalid_field and the fixed "slug: missing" message for each command.
// Boundaries: the message names the arg and a fixed reason, never a value.
// On failure first check: did slug lose its required flag?
BOOST_AUTO_TEST_CASE(missing_slug_is_invalid_field_for_each)
{
    for (const std::string& command : all_commands()) {
        Harness h;
        const CommandResponse response =
            h.run(admin_caller(), lifecycle_request(command, R"({})"));
        BOOST_TEST(!response.ok, command);
        BOOST_TEST((response.error.code == CommandErrorCode::invalid_field),
                   command);
        BOOST_TEST(response.error.message == "slug: missing", command);
    }
}

// Type: unit (unknown key; requirement 5).
// Scenario: an extra, undeclared argument is rejected by the registry with
//   invalid_field and the fixed "<key>: unknown" message for each command.
// Boundaries: lifecycle commands declare only slug; nothing else is accepted.
// On failure first check: did an extra arg creep into the lifecycle ArgSpec?
BOOST_AUTO_TEST_CASE(unknown_key_is_invalid_field_for_each)
{
    for (const std::string& command : all_commands()) {
        Harness h;
        seed_link(h.store, "extra");
        const CommandResponse response = h.run(
            admin_caller(),
            lifecycle_request(command, R"({"slug":"extra","enabled":true})"));
        BOOST_TEST(!response.ok, command);
        BOOST_TEST((response.error.code == CommandErrorCode::invalid_field),
                   command);
        BOOST_TEST(response.error.message == "enabled: unknown", command);
    }
}

// Type: unit (forced storage failure; requirement 5).
// Scenario: a repository write failure surfaces as storage_failure for each of
//   the four commands on the real path.
// Boundaries: storage_failure only arises from a real persistence attempt.
// On failure first check: did a handler swallow the store error?
BOOST_AUTO_TEST_CASE(forced_store_failure_is_storage_failure_for_each)
{
    for (const std::string& command : all_commands()) {
        Harness h;
        seed_link(h.store, "store-fail");
        h.store.forceUpdateStorageFailure();
        const CommandResponse response =
            h.run(admin_caller(),
                  lifecycle_request(command, R"({"slug":"store-fail"})"));
        BOOST_TEST(!response.ok, command);
        BOOST_TEST((response.error.code == CommandErrorCode::storage_failure),
                   command);
    }
}

// Type: unit (authorization; requirement 2).
// Scenario: a read-only User is denied each of the four commands with
//   not_authorized, and the stored record is byte-identical before and after
//   (no silent mutation). Denial precedes the handler (write permission).
// Boundaries: the store snapshot via serializeLinkViewJson is unchanged.
// On failure first check: did a descriptor permission drop below write?
BOOST_AUTO_TEST_CASE(user_denied_and_store_unchanged_for_each)
{
    for (const std::string& command : all_commands()) {
        Harness h;
        seed_link(h.store, "user-deny");
        const std::string before = stored_view_json(h.service, "user-deny");

        const CommandResponse response =
            h.run(user_caller(),
                  lifecycle_request(command, R"({"slug":"user-deny"})"));

        BOOST_TEST(!response.ok, command);
        BOOST_TEST((response.error.code == CommandErrorCode::not_authorized),
                   command);
        BOOST_TEST(stored_view_json(h.service, "user-deny") == before, command);
    }
}

// Type: unit (dry_run rejected before handler; requirement 1; threat R-A/T-57).
// Scenario: options.dry_run = true on each of the four commands is rejected by
//   the registry as unsupported (these commands set supports_dry_run = false),
//   and the stored record is byte-identical before and after -- no silent real
//   mutation slips through on the dry-run path.
// Boundaries: the rejection is unsupported (not not_found/invalid_field) and
// the
//   store snapshot via serializeLinkViewJson is unchanged.
// On failure first check: did a descriptor set supports_dry_run = true, or did
//   resolve_dispatch stop failing closed for a non-dry-run command?
BOOST_AUTO_TEST_CASE(dry_run_is_unsupported_and_store_unchanged_for_each)
{
    for (const std::string& command : all_commands()) {
        Harness h;
        seed_link(h.store, "dry");
        const std::string before = stored_view_json(h.service, "dry");

        const CommandResponse response =
            h.run(admin_caller(),
                  lifecycle_request(command,
                                    R"({"slug":"dry"})",
                                    /*dry_run=*/true));

        BOOST_TEST(!response.ok, command);
        BOOST_TEST((response.error.code == CommandErrorCode::unsupported),
                   command);
        BOOST_TEST(stored_view_json(h.service, "dry") == before, command);
    }
}

// Type: unit (audit on success; requirement 3).
// Scenario: a successful call to each mutating lifecycle command emits exactly
//   one audit record whose command is the invoked name, whose target is the
//   addressed slug, and whose outcome is "ok".
// Boundaries: audit_target is the addressed slug only, never any other value.
// On failure first check: did link_view_result pick the wrong audit target, or
//   did the descriptor stop being mutating?
BOOST_AUTO_TEST_CASE(successful_call_is_audited_with_slug_target_for_each)
{
    for (const std::string& command : all_commands()) {
        Harness h;
        // Seed deleted so link.restore also has a sensible target to act on.
        Link link = seed_link(h.store, "audited");
        link.deleted_at = "2001-01-01T00:00:00Z";
        h.store.seed(link);
        auto sink = std::make_shared<CapturingAuditSink>();
        h.registry.set_audit_sink(sink);

        const CommandResponse response =
            h.run(admin_caller(),
                  lifecycle_request(command, R"({"slug":"audited"})"));

        BOOST_TEST(response.ok, command);
        BOOST_REQUIRE_EQUAL(sink->records.size(), 1u);
        BOOST_TEST(sink->records[0].command == command, command);
        BOOST_TEST(sink->records[0].target == "audited", command);
        BOOST_TEST(sink->records[0].outcome == "ok", command);
    }
}

// Type: unit (denial is audited with empty target; requirement 3).
// Scenario: a denied (User) call to each mutating lifecycle command still emits
//   exactly one audit record, with an empty target (the handler never ran to
//   produce a validated slug) and a not_authorized outcome.
// Boundaries: the denial audit carries an empty target, never the addressed
//   slug, because permission is checked before the handler sets a target.
// On failure first check: did the registry start recording the addressed slug
// on
//   denial, or stop auditing denied mutating attempts?
BOOST_AUTO_TEST_CASE(denied_call_is_audited_with_empty_target_for_each)
{
    for (const std::string& command : all_commands()) {
        Harness h;
        seed_link(h.store, "deny-audit");
        auto sink = std::make_shared<CapturingAuditSink>();
        h.registry.set_audit_sink(sink);

        const CommandResponse response =
            h.run(user_caller(),
                  lifecycle_request(command, R"({"slug":"deny-audit"})"));

        BOOST_TEST(!response.ok, command);
        BOOST_REQUIRE_EQUAL(sink->records.size(), 1u);
        BOOST_TEST(sink->records[0].command == command, command);
        BOOST_TEST(sink->records[0].target.empty(), command);
        BOOST_TEST(sink->records[0].outcome == "not_authorized", command);
    }
}
