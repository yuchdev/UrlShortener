#define BOOST_TEST_MODULE CommandsLinkCommandCodecTest
#include <optional>
#include <string>
#include <vector>

#include <boost/test/unit_test.hpp>

#include "url_shortener/app/app_error.hpp"
#include "url_shortener/app/link_command_service.hpp"
#include "url_shortener/commands/arg_spec.hpp"
#include "url_shortener/commands/command_descriptor.hpp"
#include "url_shortener/commands/command_registry.hpp"
#include "url_shortener/commands/envelope.hpp"
#include "url_shortener/commands/error_codes.hpp"
#include "url_shortener/commands/link_command_codec.hpp"
#include "url_shortener/core/types.hpp"

using url_shortener::Link;
using url_shortener::LinkStatus;
using url_shortener::RedirectType;
using url_shortener::app::AppError;
using url_shortener::app::AppErrorCode;
using url_shortener::app::GetLinkBy;
using url_shortener::app::GetLinkQuery;
using url_shortener::app::LinkStatsView;
using url_shortener::app::LinkView;
using url_shortener::app::Result;
using url_shortener::app::serializeLinkPreviewJson;
using url_shortener::app::serializeLinkStatsJson;
using url_shortener::app::serializeLinkViewJson;
using url_shortener::commands::ArgsView;
using url_shortener::commands::ArgSpec;
using url_shortener::commands::ArgType;
using url_shortener::commands::CallerContext;
using url_shortener::commands::CommandContext;
using url_shortener::commands::CommandDescriptor;
using url_shortener::commands::CommandError;
using url_shortener::commands::CommandErrorCode;
using url_shortener::commands::CommandHandler;
using url_shortener::commands::CommandRegistry;
using url_shortener::commands::CommandRequest;
using url_shortener::commands::CommandResponse;
using url_shortener::commands::CommandResult;
using url_shortener::commands::decode_campaign;
using url_shortener::commands::decode_slug_or_id;
using url_shortener::commands::from_app_error;
using url_shortener::commands::patch_state;
using url_shortener::commands::patch_state_of;
using url_shortener::commands::Permission;
using url_shortener::commands::to_command_error;
using url_shortener::commands::validate_args;

namespace
{

// Builds a string-typed field spec for a nested object member.
ArgSpec string_field(std::string name)
{
    ArgSpec spec;
    spec.name = std::move(name);
    spec.type = ArgType::string;
    return spec;
}

// The `campaign` object spec: six optional string members, nullable.
ArgSpec campaign_spec()
{
    ArgSpec spec;
    spec.name = "campaign";
    spec.type = ArgType::object;
    spec.nullable = true;
    spec.fields = {string_field("name"),
                   string_field("source"),
                   string_field("medium"),
                   string_field("term"),
                   string_field("content"),
                   string_field("id")};
    return spec;
}

// A nullable timestamp spec for `expires_at`.
ArgSpec expires_at_spec()
{
    ArgSpec spec;
    spec.name = "expires_at";
    spec.type = ArgType::timestamp;
    spec.nullable = true;
    return spec;
}

// A `slug`/`id` spec sharing the `slug_or_id` mutual-exclusion group.
ArgSpec one_of_spec(std::string name)
{
    ArgSpec spec;
    spec.name = std::move(name);
    spec.type = ArgType::string;
    spec.one_of_group = "slug_or_id";
    return spec;
}

// Validates `json` against `specs` and returns the resulting view; requires
// success so the caller can decode from it.
ArgsView view_of(const std::vector<ArgSpec>& specs, const std::string& json)
{
    ArgsView view;
    CommandError error;
    BOOST_REQUIRE_MESSAGE(validate_args(specs, json, &view, &error),
                          "validate_args failed: " << error.message);
    return view;
}

// A fully populated LinkView for byte-for-byte serializer comparisons.
LinkView make_link_view()
{
    LinkView view;
    view.id = "id-1";
    view.slug = "my-slug";
    view.target_url = "https://example.com/path";
    view.short_url = "https://sho.rt/my-slug";
    view.created_at = "2025-01-01T00:00:00Z";
    view.updated_at = "2025-01-02T00:00:00Z";
    view.expires_at = "2025-12-31T23:59:59Z";
    view.enabled = true;
    view.tags = {"alpha", "beta"};
    view.metadata = {{"key", "value"}};
    view.redirect_type = RedirectType::permanent;
    view.status = LinkStatus::active;
    return view;
}

// A fully populated LinkStatsView for byte-for-byte serializer comparisons.
LinkStatsView make_stats_view()
{
    LinkStatsView view;
    view.slug = "my-slug";
    view.total_attempts = 10;
    view.successful_redirects = 8;
    view.status_code_counts = {{301, 8}, {404, 2}};
    view.domain_counts = {{"example.com", 5}};
    view.time_buckets = {{1700000000, 3}};
    view.from = "1699990000";
    view.to = "1700000000";
    view.bucket = "hour";
    return view;
}

// A probe handler that decodes `slug`/`id` and echoes the chosen discriminator,
// so the registry's one_of_group enforcement can be observed end to end.
CommandHandler slug_or_id_probe()
{
    return [](CommandContext&, const ArgsView& args) -> CommandResult
    {
        CommandResult result;
        GetLinkQuery query;
        CommandError error;
        if (!decode_slug_or_id(args, &query, &error)) {
            result.error = error;
            return result;
        }
        const std::string by = query.by == GetLinkBy::slug ? "slug" : "id";
        result.result_json =
            "{\"by\":\"" + by + "\",\"value\":\"" + query.value + "\"}";
        return result;
    };
}

// Registers a read probe descriptor with slug/id one_of_group args.
void add_slug_or_id_probe(CommandRegistry& registry)
{
    CommandDescriptor descriptor;
    descriptor.name = "link.get";
    descriptor.summary = "probe";
    descriptor.permission = Permission::read;
    descriptor.args = {one_of_spec("slug"), one_of_spec("id")};
    descriptor.handler = slug_or_id_probe();
    registry.add(std::move(descriptor));
}

// Builds a request for the probe command with the given args JSON.
CommandRequest probe_request(std::string args_json)
{
    CommandRequest request;
    request.id = "client-1";
    request.command = "link.get";
    request.args_json = std::move(args_json);
    return request;
}

// Suffix match (no std::string::ends_with in C++17).
bool ends_with(const std::string& text, const std::string& suffix)
{
    return text.size() >= suffix.size()
        && text.compare(text.size() - suffix.size(), suffix.size(), suffix)
            == 0;
}

}  // namespace

// Type: unit (error mapping).
// Scenario: to_command_error maps every app::AppErrorCode (including `none`,
//   handled defensively) through from_app_error and preserves the service
//   detail verbatim as the message.
// Boundaries: the message equals the fixed detail exactly and never gains an
//   argument value; `none` maps to `none` rather than a failure code.
// On failure first check: did from_app_error gain/lose an enumerator, or did
//   make_error start mutating the message?

BOOST_AUTO_TEST_CASE(to_command_error_maps_every_app_error_code)
{
    const std::vector<AppErrorCode> codes = {AppErrorCode::none,
                                             AppErrorCode::invalid_url,
                                             AppErrorCode::invalid_slug,
                                             AppErrorCode::reserved_slug,
                                             AppErrorCode::slug_conflict,
                                             AppErrorCode::invalid_field,
                                             AppErrorCode::not_found,
                                             AppErrorCode::storage_failure,
                                             AppErrorCode::internal};

    for (const AppErrorCode code : codes) {
        AppError app_error;
        app_error.code = code;
        app_error.detail = "fixed service detail";
        const CommandError mapped = to_command_error(app_error);
        BOOST_TEST((mapped.code == from_app_error(code)));
        BOOST_TEST(mapped.message == "fixed service detail");
        // The message must carry no argument value; it is exactly the detail.
        BOOST_TEST(mapped.message.find("https://") == std::string::npos);
    }
}

// Type: unit (serializer passthrough, contract C1/C3).
// Scenario: link_view_result and link_preview_result embed the shared
//   serializer output byte for byte and record the view's own slug as the audit
//   target on success.
// Boundaries: the wrapped text equals calling the serializer directly; the
//   audit_target is the view slug, not the addressed slug.
// On failure first check: did the wrapper reformat the JSON instead of calling
//   the serializer, or pick the wrong audit slug?

BOOST_AUTO_TEST_CASE(link_view_and_preview_results_match_serializers)
{
    const LinkView view = make_link_view();
    Result<LinkView> result;
    result.value = view;

    const CommandResult full =
        url_shortener::commands::link_view_result(result, "addressed-slug");
    BOOST_TEST(full.ok());
    BOOST_TEST(full.result_json == serializeLinkViewJson(view));
    BOOST_TEST(full.audit_target == "my-slug");

    const CommandResult preview =
        url_shortener::commands::link_preview_result(result, "addressed-slug");
    BOOST_TEST(preview.ok());
    BOOST_TEST(preview.result_json == serializeLinkPreviewJson(view));
    BOOST_TEST(preview.audit_target == "my-slug");
}

// Type: unit (serializer passthrough, contract C1/C3).
// Scenario: link_stats_result embeds serializeLinkStatsJson byte for byte and
//   records the stats view slug as the audit target on success.
// Boundaries: the wrapped text equals calling the serializer directly.
// On failure first check: did the wrapper reformat the stats JSON?

BOOST_AUTO_TEST_CASE(link_stats_result_matches_serializer)
{
    const LinkStatsView view = make_stats_view();
    Result<LinkStatsView> result;
    result.value = view;

    const CommandResult stats =
        url_shortener::commands::link_stats_result(result, "addressed-slug");
    BOOST_TEST(stats.ok());
    BOOST_TEST(stats.result_json == serializeLinkStatsJson(view));
    BOOST_TEST(stats.audit_target == "my-slug");
}

// Type: unit (error projection + audit target, threat model T-55).
// Scenario: on a service failure the wrapper maps the AppError to its command
//   code, leaves result_json empty, and records the caller-addressed slug (not
//   the view slug) as the audit target.
// Boundaries: audit_target is the addressed value on failure; the message is
//   the service detail.
// On failure first check: did the failure path use the wrong audit target or
//   fail to clear result_json?

BOOST_AUTO_TEST_CASE(link_view_result_failure_uses_addressed_target)
{
    Result<LinkView> result;
    result.error.code = AppErrorCode::not_found;
    result.error.detail = "link not found";

    const CommandResult mapped =
        url_shortener::commands::link_view_result(result, "addressed-slug");
    BOOST_TEST(!mapped.ok());
    BOOST_TEST((mapped.error.code == CommandErrorCode::not_found));
    BOOST_TEST(mapped.error.message == "link not found");
    BOOST_TEST(mapped.result_json.empty());
    BOOST_TEST(mapped.audit_target == "addressed-slug");
}

// Type: unit (defensive internal mapping).
// Scenario: an ok() result carrying no value is a service programming error and
//   maps to `internal` with a fixed message and the addressed audit target.
// Boundaries: ok()-without-value is distinct from a real failure; message is
//   fixed text.
// On failure first check: does Result expose value/ok() the way the wrapper
//   expects?

BOOST_AUTO_TEST_CASE(link_view_result_ok_without_value_is_internal)
{
    Result<LinkView> result;  // ok() true, value empty.

    const CommandResult mapped =
        url_shortener::commands::link_view_result(result, "addressed-slug");
    BOOST_TEST(!mapped.ok());
    BOOST_TEST((mapped.error.code == CommandErrorCode::internal));
    BOOST_TEST(mapped.error.message == "service returned no value");
    BOOST_TEST(mapped.audit_target == "addressed-slug");
}

// Type: unit (object decode).
// Scenario: decode_campaign reads all six members of a full campaign object.
// Boundaries: every field is populated from a present member.
// On failure first check: did a member name drift from the ArgSpec field names?

BOOST_AUTO_TEST_CASE(decode_campaign_full_object)
{
    const ArgsView view = view_of(
        {campaign_spec()},
        R"({"campaign":{"name":"n","source":"s","medium":"m","term":"t",)"
        R"("content":"c","id":"i"}})");

    std::optional<Link::Campaign> out;
    CommandError error;
    BOOST_TEST(decode_campaign(view, "campaign", &out, &error));
    BOOST_REQUIRE(out.has_value());
    BOOST_TEST((out->name == std::optional<std::string>("n")));
    BOOST_TEST((out->source == std::optional<std::string>("s")));
    BOOST_TEST((out->medium == std::optional<std::string>("m")));
    BOOST_TEST((out->term == std::optional<std::string>("t")));
    BOOST_TEST((out->content == std::optional<std::string>("c")));
    BOOST_TEST((out->id == std::optional<std::string>("i")));
}

// Type: unit (object decode, empty + partial).
// Scenario: an empty campaign object yields a present campaign with all fields
//   unset; a partial object leaves absent members nullopt.
// Boundaries: `{}` is a present value (not absent); absent members stay nullopt.
// On failure first check: did decode_campaign confuse an absent member with a
//   present-but-empty one?

BOOST_AUTO_TEST_CASE(decode_campaign_empty_and_partial)
{
    const ArgsView empty_view = view_of({campaign_spec()}, R"({"campaign":{}})");
    std::optional<Link::Campaign> empty_out;
    CommandError error;
    BOOST_TEST(decode_campaign(empty_view, "campaign", &empty_out, &error));
    BOOST_REQUIRE(empty_out.has_value());
    BOOST_TEST(!empty_out->name.has_value());
    BOOST_TEST(!empty_out->id.has_value());

    const ArgsView partial_view = view_of(
        {campaign_spec()}, R"({"campaign":{"name":"n","id":"i"}})");
    std::optional<Link::Campaign> partial_out;
    BOOST_TEST(decode_campaign(partial_view, "campaign", &partial_out, &error));
    BOOST_REQUIRE(partial_out.has_value());
    BOOST_TEST((partial_out->name == std::optional<std::string>("n")));
    BOOST_TEST((partial_out->id == std::optional<std::string>("i")));
    BOOST_TEST(!partial_out->source.has_value());
    BOOST_TEST(!partial_out->medium.has_value());
    BOOST_TEST(!partial_out->term.has_value());
    BOOST_TEST(!partial_out->content.has_value());
}

// Type: unit (PATCH tri-state).
// Scenario: patch_state_of distinguishes absent, explicit null, and a concrete
//   value for both expires_at and campaign.
// Boundaries: null is handled here, not by decode_campaign; absent and null are
//   distinct from a value.
// On failure first check: do ArgsView::has/is_null report presence/null the way
//   this helper expects?

BOOST_AUTO_TEST_CASE(patch_state_of_absent_null_value)
{
    const std::vector<ArgSpec> specs = {expires_at_spec(), campaign_spec()};

    const ArgsView absent = view_of(specs, R"({})");
    BOOST_TEST((patch_state_of(absent, "expires_at") == patch_state::absent));
    BOOST_TEST((patch_state_of(absent, "campaign") == patch_state::absent));

    const ArgsView nulls =
        view_of(specs, R"({"expires_at":null,"campaign":null})");
    BOOST_TEST(
        (patch_state_of(nulls, "expires_at") == patch_state::null_value));
    BOOST_TEST((patch_state_of(nulls, "campaign") == patch_state::null_value));

    const ArgsView values = view_of(
        specs,
        R"({"expires_at":"2025-12-31T23:59:59Z","campaign":{"name":"n"}})");
    BOOST_TEST((patch_state_of(values, "expires_at") == patch_state::value));
    BOOST_TEST((patch_state_of(values, "campaign") == patch_state::value));
}

// Type: unit (selector).
// Scenario: decode_slug_or_id picks the single present identifier into a
//   GetLinkQuery with the matching discriminator.
// Boundaries: slug sets by=slug, id sets by=id.
// On failure first check: did GetLinkBy or the arg names drift?

BOOST_AUTO_TEST_CASE(decode_slug_or_id_picks_present_identifier)
{
    const std::vector<ArgSpec> specs = {one_of_spec("slug"), one_of_spec("id")};

    const ArgsView slug_view = view_of(specs, R"({"slug":"abc"})");
    GetLinkQuery slug_query;
    CommandError error;
    BOOST_TEST(decode_slug_or_id(slug_view, &slug_query, &error));
    BOOST_TEST((slug_query.by == GetLinkBy::slug));
    BOOST_TEST(slug_query.value == "abc");

    const ArgsView id_view = view_of(specs, R"({"id":"xyz"})");
    GetLinkQuery id_query;
    BOOST_TEST(decode_slug_or_id(id_view, &id_query, &error));
    BOOST_TEST((id_query.by == GetLinkBy::id));
    BOOST_TEST(id_query.value == "xyz");
}

// Type: unit (defensive selector branch).
// Scenario: with neither identifier present (a state the registry normally
//   prevents) decode_slug_or_id returns false with an invalid_field error whose
//   message ends in one_of_violation.
// Boundaries: the defensive branch mirrors the registry's error shape.
// On failure first check: did the fixed message change?

BOOST_AUTO_TEST_CASE(decode_slug_or_id_neither_present_is_invalid_field)
{
    const ArgsView empty = view_of({}, R"({})");
    GetLinkQuery query;
    CommandError error;
    BOOST_TEST(!decode_slug_or_id(empty, &query, &error));
    BOOST_TEST((error.code == CommandErrorCode::invalid_field));
    BOOST_TEST(ends_with(error.message, "one_of_violation"));
}

// Type: unit (registry integration, one_of_group).
// Scenario: through a real registry a slug-only request decodes and succeeds,
//   while both-present and neither-present requests are rejected by the
//   registry's one_of_group check before the handler, with invalid_field and a
//   message ending in one_of_violation.
// Boundaries: the one_of enforcement is the registry's, not the handler's; the
//   handler is reached only when exactly one identifier is present.
// On failure first check: did ArgSpec::one_of_group handling or the registry
//   pipeline order change?

BOOST_AUTO_TEST_CASE(slug_or_id_through_registry_one_of_group)
{
    CommandRegistry registry;
    add_slug_or_id_probe(registry);
    const CallerContext caller;

    const CommandResponse ok =
        registry.execute(caller, probe_request(R"({"slug":"abc"})"));
    BOOST_TEST(ok.ok);
    BOOST_TEST(ok.result_json == R"({"by":"slug","value":"abc"})");

    const CommandResponse both = registry.execute(
        caller, probe_request(R"({"slug":"abc","id":"xyz"})"));
    BOOST_TEST(!both.ok);
    BOOST_TEST((both.error.code == CommandErrorCode::invalid_field));
    BOOST_TEST(ends_with(both.error.message, "one_of_violation"));

    const CommandResponse neither =
        registry.execute(caller, probe_request(R"({})"));
    BOOST_TEST(!neither.ok);
    BOOST_TEST((neither.error.code == CommandErrorCode::invalid_field));
    BOOST_TEST(ends_with(neither.error.message, "one_of_violation"));
}
