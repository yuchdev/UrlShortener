#define BOOST_TEST_MODULE CommandsLinkListCommandTest
#include <string>
#include <vector>

#include <boost/test/unit_test.hpp>

#include "url_shortener/app/app_error.hpp"
#include "url_shortener/app/link_command_service.hpp"
#include "url_shortener/app/link_lister.hpp"
#include "url_shortener/commands/browse_commands.hpp"
#include "url_shortener/commands/command_registry.hpp"
#include "url_shortener/commands/envelope.hpp"
#include "url_shortener/commands/error_codes.hpp"

using url_shortener::app::AppError;
using url_shortener::app::AppErrorCode;
using url_shortener::app::ILinkLister;
using url_shortener::app::LinkListPage;
using url_shortener::app::LinkListQuery;
using url_shortener::app::LinkView;
using url_shortener::app::list_sort;
using url_shortener::app::list_status;
using url_shortener::app::Result;
using url_shortener::app::serializeLinkListJson;
using url_shortener::commands::CallerContext;
using url_shortener::commands::CommandErrorCode;
using url_shortener::commands::CommandRegistry;
using url_shortener::commands::CommandRequest;
using url_shortener::commands::CommandResponse;
using url_shortener::commands::make_link_list_descriptor;

namespace
{

// A fake ILinkLister that records how often it was called and the exact query
// it received, and returns either a configured page or a forced error. The
// recording members are mutable because `list` is const on the port; each test
// owns a fresh instance, so the counter starts at zero.
class FakeLinkLister : public ILinkLister
{
public:
    mutable int call_count = 0;
    mutable LinkListQuery last_query;
    LinkListPage page;
    AppError forced_error;

    Result<LinkListPage> list(const LinkListQuery& query) const override
    {
        ++call_count;
        last_query = query;

        Result<LinkListPage> result;
        if (forced_error.code != AppErrorCode::none) {
            result.error = forced_error;
            return result;
        }
        result.value = page;
        return result;
    }
};

// Bundles the fake lister and a registry carrying only link.list, so each test
// is isolated. The lister is declared before the registry so it outlives the
// handler that captures it.
struct Harness
{
    FakeLinkLister lister;
    CommandRegistry registry;

    Harness() { registry.add(make_link_list_descriptor(lister)); }

    CommandResponse run(const CallerContext& caller, CommandRequest request)
    {
        return registry.execute(caller, request);
    }
};

// A read-only User caller: link.list's base permission is read, so this role
// may list any non-deleted status (active/disabled/expired).
CallerContext user_caller()
{
    CallerContext caller;
    caller.principal = "test-user";
    caller.transport = "test";
    caller.control_set = ControlSet::User;
    return caller;
}

// An Admin caller: required for status=all|deleted, which the permission hook
// raises to write (threat model T-71).
CallerContext admin_caller()
{
    CallerContext caller;
    caller.principal = "test-admin";
    caller.transport = "test";
    caller.control_set = ControlSet::Admin;
    return caller;
}

CommandRequest request(std::string args_json)
{
    CommandRequest req;
    req.id = "client-1";
    req.command = "link.list";
    req.args_json = std::move(args_json);
    return req;
}

// Builds a minimal LinkView for a page; only the fields the serializer reads
// need to be meaningful for the round-trip comparison.
LinkView make_view(const std::string& slug)
{
    LinkView view;
    view.id = "id-" + slug;
    view.slug = slug;
    view.target_url = "https://example.com/" + slug;
    view.short_url = "https://sho.rt/" + slug;
    view.created_at = "2025-01-01T00:00:00Z";
    view.updated_at = "2025-01-02T00:00:00Z";
    view.enabled = true;
    return view;
}

}  // namespace

// Type: unit (defaults reach the lister).
// Scenario: link.list with no args decodes to the struct defaults (active,
//   created_at, descending, offset 0, limit 50) and calls the lister exactly
//   once.
// Boundaries: an absent arg keeps the LinkListQuery default, never a handler
//   literal.
// On failure first check: did decode_query start overwriting a field for an
//   absent arg, or did a default drift from the subtask-12 struct?
BOOST_AUTO_TEST_CASE(no_args_decode_to_defaults)
{
    Harness h;

    const CommandResponse response = h.run(user_caller(), request("{}"));

    BOOST_TEST(response.ok);
    BOOST_TEST(h.lister.call_count == 1);
    BOOST_TEST((h.lister.last_query.status == list_status::active));
    BOOST_TEST((h.lister.last_query.sort == list_sort::created_at));
    BOOST_TEST(h.lister.last_query.descending);
    BOOST_TEST(h.lister.last_query.offset == 0u);
    BOOST_TEST(h.lister.last_query.limit == 50u);
    BOOST_TEST(!h.lister.last_query.q.has_value());
    BOOST_TEST(!h.lister.last_query.tag.has_value());
    BOOST_TEST(!h.lister.last_query.campaign.has_value());
}

// Type: unit (every arg decoded to the right field).
// Scenario: link.list with all eight args (order:"asc") maps each to its query
//   field: status, q, tag, campaign, sort, descending=false, offset, limit.
// Boundaries: order "asc" flips descending to false; each field lands on the
//   correct target.
// On failure first check: did a status/sort token map to the wrong enumerator,
//   or did order invert?
BOOST_AUTO_TEST_CASE(every_arg_decoded_to_its_field)
{
    Harness h;

    const CommandResponse response = h.run(
        admin_caller(),
        request(R"({"status":"all","q":"foo","tag":"red","campaign":"spring",)"
                R"("sort":"slug","order":"asc","offset":3,"limit":7})"));

    BOOST_TEST(response.ok);
    BOOST_TEST(h.lister.call_count == 1);
    BOOST_TEST((h.lister.last_query.status == list_status::all));
    BOOST_TEST((h.lister.last_query.q.value_or("") == "foo"));
    BOOST_TEST((h.lister.last_query.tag.value_or("") == "red"));
    BOOST_TEST((h.lister.last_query.campaign.value_or("") == "spring"));
    BOOST_TEST((h.lister.last_query.sort == list_sort::slug));
    BOOST_TEST(!h.lister.last_query.descending);
    BOOST_TEST(h.lister.last_query.offset == 3u);
    BOOST_TEST(h.lister.last_query.limit == 7u);
}

// Type: unit (status enumerators including expired).
// Scenario: each of active/disabled/expired/deleted/all decodes to its matching
//   list_status enumerator.
// Boundaries: expired is a first-class status (added in subtask 12), not folded
//   into another value.
// On failure first check: did status_of drop or misorder a case?
BOOST_AUTO_TEST_CASE(status_enumerators_decode_including_expired)
{
    {
        Harness h;
        h.run(user_caller(), request(R"({"status":"disabled"})"));
        BOOST_TEST((h.lister.last_query.status == list_status::disabled));
    }
    {
        Harness h;
        h.run(user_caller(), request(R"({"status":"expired"})"));
        BOOST_TEST((h.lister.last_query.status == list_status::expired));
    }
    {
        // status=deleted requires write/admin (T-71), so use an Admin caller
        // here; the User-denied path is covered by its own case below.
        Harness h;
        h.run(admin_caller(), request(R"({"status":"deleted"})"));
        BOOST_TEST((h.lister.last_query.status == list_status::deleted));
    }
}

// Type: unit (limit upper bound accepted).
// Scenario: limit:500 (the inclusive max) is accepted and reaches the lister.
// Boundaries: 500 is the last accepted value of the 1..500 range.
// On failure first check: did the ArgSpec max drift below link_list_max_limit?
BOOST_AUTO_TEST_CASE(limit_500_accepted)
{
    Harness h;

    const CommandResponse response =
        h.run(user_caller(), request(R"({"limit":500})"));

    BOOST_TEST(response.ok);
    BOOST_TEST(h.lister.call_count == 1);
    BOOST_TEST(h.lister.last_query.limit == 500u);
}

// Type: unit (limit over max rejected before the lister, T-56, success crit 2).
// Scenario: limit:501 fails with invalid_field / out_of_range and the lister is
//   never called.
// Boundaries: 501 is the first rejected value above the range.
// On failure first check: did the limit max cap move, or did the registry stop
//   running before the handler?
BOOST_AUTO_TEST_CASE(limit_501_rejected_without_calling_lister)
{
    Harness h;

    const CommandResponse response =
        h.run(user_caller(), request(R"({"limit":501})"));

    BOOST_TEST(!response.ok);
    BOOST_TEST((response.error.code == CommandErrorCode::invalid_field));
    BOOST_TEST(response.error.message.find("out_of_range")
               != std::string::npos);
    BOOST_TEST(h.lister.call_count == 0);
}

// Type: unit (limit below min rejected before the lister, T-56).
// Scenario: limit:0 fails with invalid_field / out_of_range and the lister is
//   never called.
// Boundaries: 0 is below the inclusive min of 1.
// On failure first check: did the limit min cap drop to 0?
BOOST_AUTO_TEST_CASE(limit_0_rejected_without_calling_lister)
{
    Harness h;

    const CommandResponse response =
        h.run(user_caller(), request(R"({"limit":0})"));

    BOOST_TEST(!response.ok);
    BOOST_TEST((response.error.code == CommandErrorCode::invalid_field));
    BOOST_TEST(response.error.message.find("out_of_range")
               != std::string::npos);
    BOOST_TEST(h.lister.call_count == 0);
}

// Type: unit (negative offset rejected before the lister, T-56).
// Scenario: offset:-1 fails with invalid_field / out_of_range and the lister is
//   never called.
// Boundaries: -1 is below the inclusive min of 0; a negative offset never
//   reaches the size_t cast in the handler.
// On failure first check: did the offset min cap disappear?
BOOST_AUTO_TEST_CASE(negative_offset_rejected_without_calling_lister)
{
    Harness h;

    const CommandResponse response =
        h.run(user_caller(), request(R"({"offset":-1})"));

    BOOST_TEST(!response.ok);
    BOOST_TEST((response.error.code == CommandErrorCode::invalid_field));
    BOOST_TEST(response.error.message.find("out_of_range")
               != std::string::npos);
    BOOST_TEST(h.lister.call_count == 0);
}

// Type: unit (offset upper bound accepted).
// Scenario: offset at link_list_max_offset (1000000, the inclusive max) is
//   accepted and reaches the lister with that value.
// Boundaries: 1000000 is the last accepted offset of the 0..1000000 range; it
//   bounds the deep-skip index walk a hostile offset would otherwise force
//   (threat model T-56).
// On failure first check: did the offset ArgSpec max drift below
//   link_list_max_offset, or vanish?
BOOST_AUTO_TEST_CASE(offset_max_accepted)
{
    Harness h;
    const std::string args =
        R"({"offset":)"
        + std::to_string(url_shortener::app::link_list_max_offset) + "}";

    const CommandResponse response = h.run(user_caller(), request(args));

    BOOST_TEST(response.ok);
    BOOST_TEST(h.lister.call_count == 1);
    BOOST_TEST(h.lister.last_query.offset
               == url_shortener::app::link_list_max_offset);
}

// Type: unit (offset over max rejected before the lister, T-56).
// Scenario: offset at link_list_max_offset + 1 (1000001) fails with the same
//   invalid_field / out_of_range shape as limit:501 and the lister is never
//   called.
// Boundaries: max+1 is the first rejected value above the range; the cap
//   stops a huge skip/index walk from an 18-digit offset.
// On failure first check: did the offset max cap move, or did the registry
//   stop running before the handler?
BOOST_AUTO_TEST_CASE(offset_over_max_rejected_without_calling_lister)
{
    Harness h;
    const std::string args =
        R"({"offset":)"
        + std::to_string(url_shortener::app::link_list_max_offset + 1) + "}";

    const CommandResponse response = h.run(user_caller(), request(args));

    BOOST_TEST(!response.ok);
    BOOST_TEST((response.error.code == CommandErrorCode::invalid_field));
    BOOST_TEST(response.error.message.find("out_of_range")
               != std::string::npos);
    BOOST_TEST(h.lister.call_count == 0);
}

// Type: unit (bad status enum rejected before the lister).
// Scenario: status:"bogus" fails with invalid_field / not_in_enum and the
//   lister is never called.
// Boundaries: the enum reason names only the fixed reason, never the value.
// On failure first check: did status stop being an enumeration ArgSpec?
BOOST_AUTO_TEST_CASE(bad_status_enum_rejected_without_calling_lister)
{
    Harness h;

    const CommandResponse response =
        h.run(user_caller(), request(R"({"status":"bogus"})"));

    BOOST_TEST(!response.ok);
    BOOST_TEST((response.error.code == CommandErrorCode::invalid_field));
    BOOST_TEST(response.error.message.find("not_in_enum")
               != std::string::npos);
    BOOST_TEST(response.error.message.find("bogus") == std::string::npos);
    BOOST_TEST(h.lister.call_count == 0);
}

// Type: unit (bad sort enum rejected before the lister).
// Scenario: sort:"nope" fails with invalid_field / not_in_enum; lister not
//   called.
// Boundaries: sort is a closed enumeration.
// On failure first check: did sort lose its enum_values?
BOOST_AUTO_TEST_CASE(bad_sort_enum_rejected_without_calling_lister)
{
    Harness h;

    const CommandResponse response =
        h.run(user_caller(), request(R"({"sort":"nope"})"));

    BOOST_TEST(!response.ok);
    BOOST_TEST((response.error.code == CommandErrorCode::invalid_field));
    BOOST_TEST(response.error.message.find("not_in_enum")
               != std::string::npos);
    BOOST_TEST(h.lister.call_count == 0);
}

// Type: unit (bad order enum rejected before the lister).
// Scenario: order:"up" fails with invalid_field / not_in_enum; lister not
//   called.
// Boundaries: order accepts only asc/desc.
// On failure first check: did order lose its enum_values?
BOOST_AUTO_TEST_CASE(bad_order_enum_rejected_without_calling_lister)
{
    Harness h;

    const CommandResponse response =
        h.run(user_caller(), request(R"({"order":"up"})"));

    BOOST_TEST(!response.ok);
    BOOST_TEST((response.error.code == CommandErrorCode::invalid_field));
    BOOST_TEST(response.error.message.find("not_in_enum")
               != std::string::npos);
    BOOST_TEST(h.lister.call_count == 0);
}

// Type: unit (q at the length cap accepted).
// Scenario: a q of exactly link_list_max_query_length (256) bytes is accepted
//   and reaches the lister.
// Boundaries: 256 is the last accepted length.
// On failure first check: did the q cap drift from link_list_max_query_length?
BOOST_AUTO_TEST_CASE(q_at_cap_accepted)
{
    Harness h;
    const std::string q(256, 'a');

    const CommandResponse response =
        h.run(user_caller(), request(R"({"q":")" + q + R"("})"));

    BOOST_TEST(response.ok);
    BOOST_TEST(h.lister.call_count == 1);
    BOOST_TEST((h.lister.last_query.q.value_or("") == q));
}

// Type: unit (q over the length cap rejected before the lister, T-56).
// Scenario: a q of 257 bytes fails with invalid_field / too_long; lister not
//   called.
// Boundaries: 257 is the first rejected length.
// On failure first check: did the q cap rise above 256?
BOOST_AUTO_TEST_CASE(q_over_cap_rejected_without_calling_lister)
{
    Harness h;
    const std::string q(257, 'a');

    const CommandResponse response =
        h.run(user_caller(), request(R"({"q":")" + q + R"("})"));

    BOOST_TEST(!response.ok);
    BOOST_TEST((response.error.code == CommandErrorCode::invalid_field));
    BOOST_TEST(response.error.message.find("too_long") != std::string::npos);
    BOOST_TEST(h.lister.call_count == 0);
}

// Type: unit (oversized tag rejected before the lister, T-56).
// Scenario: a tag of 33 bytes (one over the service's 32-byte per-tag limit)
//   fails with invalid_field / too_long; lister not called.
// Boundaries: a value longer than any storable tag can never match, so bounding
//   it here only removes pointless scan work.
// On failure first check: did the tag cap rise above 32?
BOOST_AUTO_TEST_CASE(oversized_tag_rejected_without_calling_lister)
{
    Harness h;
    const std::string tag(33, 'a');

    const CommandResponse response =
        h.run(user_caller(), request(R"({"tag":")" + tag + R"("})"));

    BOOST_TEST(!response.ok);
    BOOST_TEST((response.error.code == CommandErrorCode::invalid_field));
    BOOST_TEST(response.error.message.find("too_long") != std::string::npos);
    BOOST_TEST(h.lister.call_count == 0);
}

// Type: unit (oversized campaign rejected before the lister, T-56).
// Scenario: a campaign of 129 bytes (one over the service's 128-byte campaign
//   field limit) fails with invalid_field / too_long; lister not called.
// Boundaries: a value longer than any storable campaign name can never match.
// On failure first check: did the campaign cap rise above 128?
BOOST_AUTO_TEST_CASE(oversized_campaign_rejected_without_calling_lister)
{
    Harness h;
    const std::string campaign(129, 'a');

    const CommandResponse response = h.run(
        user_caller(), request(R"({"campaign":")" + campaign + R"("})"));

    BOOST_TEST(!response.ok);
    BOOST_TEST((response.error.code == CommandErrorCode::invalid_field));
    BOOST_TEST(response.error.message.find("too_long") != std::string::npos);
    BOOST_TEST(h.lister.call_count == 0);
}

// Type: unit (unknown arg rejected before the lister).
// Scenario: an undeclared argument fails with invalid_field; lister not called.
// Boundaries: the schema is closed; an unknown key never reaches the handler.
// On failure first check: did the arg list gain an unexpected member or the
//   registry stop rejecting unknown keys?
BOOST_AUTO_TEST_CASE(unknown_arg_rejected_without_calling_lister)
{
    Harness h;

    const CommandResponse response =
        h.run(user_caller(), request(R"({"bogus":1})"));

    BOOST_TEST(!response.ok);
    BOOST_TEST((response.error.code == CommandErrorCode::invalid_field));
    BOOST_TEST(h.lister.call_count == 0);
}

// Type: unit (wrong-typed limit rejected before the lister).
// Scenario: limit as a JSON string ("5") fails with invalid_field; lister not
//   called.
// Boundaries: an integer arg declared as integer rejects a string token.
// On failure first check: did limit stop being an integer ArgSpec?
BOOST_AUTO_TEST_CASE(wrong_typed_limit_rejected_without_calling_lister)
{
    Harness h;

    const CommandResponse response =
        h.run(user_caller(), request(R"({"limit":"5"})"));

    BOOST_TEST(!response.ok);
    BOOST_TEST((response.error.code == CommandErrorCode::invalid_field));
    BOOST_TEST(h.lister.call_count == 0);
}

// Type: unit (lister error propagated with its code).
// Scenario: the lister returns storage_failure; the response carries the mapped
//   storage_failure command code.
// Boundaries: a lister failure maps through to_command_error, not a fixed code.
// On failure first check: did the handler stop routing result.error through
//   to_command_error?
BOOST_AUTO_TEST_CASE(lister_error_propagated_with_its_code)
{
    Harness h;
    h.lister.forced_error = AppError {AppErrorCode::storage_failure, "backend"};

    const CommandResponse response = h.run(user_caller(), request("{}"));

    BOOST_TEST(!response.ok);
    BOOST_TEST((response.error.code == CommandErrorCode::storage_failure));
}

// Type: unit (result equals the serializer of the lister's page).
// Scenario: the lister returns a two-item page; result_json is byte-equal to
//   serializeLinkListJson of that page.
// Boundaries: the handler serializes the page verbatim, never a reformat.
// On failure first check: did the handler stop calling serializeLinkListJson,
//   or pass a different page?
BOOST_AUTO_TEST_CASE(result_equals_serialize_link_list_json)
{
    Harness h;
    h.lister.page.items = {make_view("alpha"), make_view("bravo")};
    h.lister.page.offset = 0;
    h.lister.page.limit = 50;
    h.lister.page.total = 2;

    const CommandResponse response = h.run(user_caller(), request("{}"));

    BOOST_TEST(response.ok);
    BOOST_TEST(response.result_json == serializeLinkListJson(h.lister.page));
}

// Type: unit (empty page is valid JSON).
// Scenario: the lister returns an empty page; result_json equals the serializer
//   output for an empty page.
// Boundaries: an empty items array still produces the full envelope shape.
// On failure first check: did serializeLinkListJson change its empty-page form?
BOOST_AUTO_TEST_CASE(empty_page_is_valid_json)
{
    Harness h;
    h.lister.page.offset = 0;
    h.lister.page.limit = 50;
    h.lister.page.total = 0;

    const CommandResponse response = h.run(user_caller(), request("{}"));

    BOOST_TEST(response.ok);
    BOOST_TEST(response.result_json == serializeLinkListJson(h.lister.page));
    BOOST_TEST(response.result_json
               == R"({"items":[],"offset":0,"limit":50,"total":0})");
}

// Type: unit (read-only caller allowed for non-deleted statuses).
// Scenario: a ControlSet::User caller may run link.list with the default and
//   every non-deleted status (active/disabled/expired).
// Boundaries: the base permission is read; the hook raises only all/deleted.
// On failure first check: did the hook start raising a non-deleted status, or
//   did the base permission rise above read?
BOOST_AUTO_TEST_CASE(user_role_allowed_for_non_deleted_statuses)
{
    const std::vector<std::string> statuses = {"{}",
                                               R"({"status":"active"})",
                                               R"({"status":"disabled"})",
                                               R"({"status":"expired"})"};
    for (const std::string& status : statuses) {
        Harness h;
        const CommandResponse response = h.run(user_caller(), request(status));
        BOOST_TEST(response.ok, status);
        BOOST_TEST(h.lister.call_count == 1, status);
    }
}

// Type: unit (status=all|deleted require write/admin, T-71).
// Scenario: a ControlSet::User caller requesting status=all or status=deleted is
//   denied with not_authorized and the lister is never called (no enumeration of
//   deleted/disabled rows).
// Boundaries: the permission hook raises read to write for exactly these two
//   statuses, enforced in execute before the handler runs.
// On failure first check: did the hook stop raising for all/deleted, or did the
//   denial happen after the lister?
BOOST_AUTO_TEST_CASE(user_denied_status_all_and_deleted_without_calling_lister)
{
    for (const std::string& status :
         {R"({"status":"all"})", R"({"status":"deleted"})"}) {
        Harness h;
        const CommandResponse response = h.run(user_caller(), request(status));
        BOOST_TEST(!response.ok, status);
        BOOST_TEST((response.error.code == CommandErrorCode::not_authorized),
                   status);
        BOOST_TEST(h.lister.call_count == 0, status);
    }
}

// Type: unit (admin may use every status, T-71).
// Scenario: an Admin caller runs status=all and status=deleted successfully and
//   the lister receives the matching query.
// Boundaries: write/admin satisfies the raised permission.
// On failure first check: did the hook raise above write, or did Admin lose
//   write capability?
BOOST_AUTO_TEST_CASE(admin_allowed_status_all_and_deleted)
{
    {
        Harness h;
        const CommandResponse response =
            h.run(admin_caller(), request(R"({"status":"all"})"));
        BOOST_TEST(response.ok);
        BOOST_TEST((h.lister.last_query.status == list_status::all));
    }
    {
        Harness h;
        const CommandResponse response =
            h.run(admin_caller(), request(R"({"status":"deleted"})"));
        BOOST_TEST(response.ok);
        BOOST_TEST((h.lister.last_query.status == list_status::deleted));
    }
}

// Type: unit (read-only caller allowed).
// Scenario: a ControlSet::User caller is permitted to run link.list with the
//   default (active) status.
// Boundaries: the base descriptor permission is read; a User is never denied for
//   the default status.
// On failure first check: did the base descriptor permission rise above read?
BOOST_AUTO_TEST_CASE(user_role_allowed)
{
    Harness h;

    const CommandResponse response = h.run(user_caller(), request("{}"));

    BOOST_TEST(response.ok);
    BOOST_TEST(h.lister.call_count == 1);
}

// Type: unit (handler never calls the lister on any validation failure, T-56).
// Scenario: a batch of invalid inputs (limit 0/501, offset -1, offset over max,
//   q 257, oversized tag/campaign, bad status/sort/order, unknown arg, wrong
//   type) each leave the lister uncalled, proving validation precedes every
//   lister call.
// Boundaries: the aggregate call count across all invalid requests stays 0.
// On failure first check: which invalid input slipped past the registry into
//   the handler?
BOOST_AUTO_TEST_CASE(handler_never_calls_lister_on_validation_failure)
{
    Harness h;
    const std::vector<std::string> invalid = {
        R"({"limit":0})",
        R"({"limit":501})",
        R"({"offset":-1})",
        R"({"offset":)"
            + std::to_string(url_shortener::app::link_list_max_offset + 1)
            + "}",
        R"({"q":")" + std::string(257, 'a') + R"("})",
        R"({"tag":")" + std::string(33, 'a') + R"("})",
        R"({"campaign":")" + std::string(129, 'a') + R"("})",
        R"({"status":"bogus"})",
        R"({"sort":"nope"})",
        R"({"order":"up"})",
        R"({"bogus":1})",
        R"({"limit":"5"})"};

    for (const std::string& args : invalid) {
        const CommandResponse response = h.run(user_caller(), request(args));
        BOOST_TEST(!response.ok);
        BOOST_TEST((response.error.code == CommandErrorCode::invalid_field));
    }

    BOOST_TEST(h.lister.call_count == 0);
}
