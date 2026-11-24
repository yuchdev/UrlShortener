#include <url_shortener/commands/link_commands.hpp>

#include <cstddef>
#include <map>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include <url_shortener/app/link_command_service.hpp>
#include <url_shortener/commands/arg_spec.hpp>
#include <url_shortener/commands/command_descriptor.hpp>
#include <url_shortener/commands/error_codes.hpp>
#include <url_shortener/commands/link_command_codec.hpp>
#include <url_shortener/core/types.hpp>
#include <url_shortener/core/utils.hpp>

namespace url_shortener::commands
{

namespace
{

/// Per-arg free-text byte cap (contract C3): the registry's generic caps are a
/// hard outer bound only, loose enough never to pre-empt the service's own
/// length validators (which keep their existing REST messages). Mirrors
/// `ServerConfig::max_request_body_bytes`.
constexpr std::size_t k_text_max_length = 65536;

/// Per-arg item cap for the list/map arguments (contract C3): large enough that
/// the registry never rejects input the service's tag and metadata
/// validators would otherwise classify with their own messages.
constexpr std::size_t k_collection_max_items = 4096;

/**
 * @brief Builds a free-text string @ref ArgSpec with the contract-C3 cap.
 *
 * @param name The argument key as it appears in the `args` object.
 * @param required `true` when the argument must be present (the registry then
 * fails a missing value with `"<name>: missing"`).
 * @return A string @ref ArgSpec named @p name with `max_length` raised to
 * @ref k_text_max_length so length validation stays in the service.
 */
ArgSpec make_string_arg(std::string name, bool required)
{
    ArgSpec spec;
    spec.name = std::move(name);
    spec.type = ArgType::string;
    spec.required = required;
    spec.max_length = k_text_max_length;
    return spec;
}

/**
 * @brief Builds one optional string member spec of the `campaign` object.
 *
 * @param name The nested member key (`name`, `source`, `medium`, `term`,
 * `content`, or `id`).
 * @return A string @ref ArgSpec with the contract-C3 free-text cap, used as a
 * `fields` entry of the `campaign` object spec.
 */
ArgSpec make_campaign_field(std::string name)
{
    return make_string_arg(std::move(name), false);
}

/**
 * @brief Builds the nullable `campaign` object @ref ArgSpec.
 *
 * Declares the six optional UTM string members; `nullable = true` so an explicit
 * JSON `null` is accepted and distinguished from an absent argument (JSON `null`
 * means "no campaign" for a create).
 *
 * @return The `campaign` object @ref ArgSpec.
 */
ArgSpec make_campaign_arg()
{
    ArgSpec spec;
    spec.name = "campaign";
    spec.type = ArgType::object;
    spec.nullable = true;
    spec.max_length = k_text_max_length;
    spec.max_items = k_collection_max_items;
    spec.fields = {make_campaign_field("name"),
                   make_campaign_field("source"),
                   make_campaign_field("medium"),
                   make_campaign_field("term"),
                   make_campaign_field("content"),
                   make_campaign_field("id")};
    return spec;
}

/**
 * @brief Builds the argument schema for `link.create` (catalog 3.1 table).
 *
 * Exactly the catalog rows: `target_url` (required), `slug`, `redirect_type`
 * (enum `temporary`/`permanent`), `expires_at` (timestamp), `enabled` (bool),
 * `tags` (string list), `metadata` (string map), `campaign` (nullable object).
 * Every free-text arg and the list/map args carry the raised contract-C3 caps
 * so the single validator remains `LinkCommandService` (see
 * @ref k_text_max_length / @ref k_collection_max_items).
 *
 * @return The ordered @ref ArgSpec list for the descriptor.
 */
std::vector<ArgSpec> make_link_create_args_impl()
{
    ArgSpec redirect_type;
    redirect_type.name = "redirect_type";
    redirect_type.type = ArgType::enumeration;
    redirect_type.enum_values = {"temporary", "permanent"};
    redirect_type.max_length = k_text_max_length;

    ArgSpec expires_at;
    expires_at.name = "expires_at";
    expires_at.type = ArgType::timestamp;
    expires_at.max_length = k_text_max_length;

    ArgSpec enabled;
    enabled.name = "enabled";
    enabled.type = ArgType::boolean;

    ArgSpec tags;
    tags.name = "tags";
    tags.type = ArgType::string_list;
    tags.max_length = k_text_max_length;
    tags.max_items = k_collection_max_items;

    ArgSpec metadata;
    metadata.name = "metadata";
    metadata.type = ArgType::string_map;
    metadata.max_length = k_text_max_length;
    metadata.max_items = k_collection_max_items;

    return {make_string_arg("target_url", true),
            make_string_arg("slug", false),
            std::move(redirect_type),
            std::move(expires_at),
            std::move(enabled),
            std::move(tags),
            std::move(metadata),
            make_campaign_arg()};
}

/**
 * @brief Decodes the validated arguments and runs the create (or its dry-run).
 *
 * Mirrors the REST `createLinkFromRequest` decode (decision-log Q18): only the
 * canonical `target_url` is read; optional fields are copied only when present,
 * so an absent field falls back to the service's config default exactly as
 * today. The `campaign` tri-state follows the PATCH convention: absent or
 * explicit `null` means "no campaign", a present object is decoded via
 * @ref decode_campaign. Under `context.dry_run` the call routes to
 * `ValidateCreateLink` (validates, never writes); otherwise to `CreateLink`.
 * The outcome is projected through @ref link_view_result, which supplies the
 * audit target (the created view's slug on success, the caller-addressed slug on
 * failure). No URL/slug check is performed here: all domain validation stays in
 * the service (contract C6, SSRF indistinguishability).
 *
 * @param service The service the create is delegated to.
 * @param context The per-invocation context; only `dry_run` is read here.
 * @param args The validated argument view (every arg already satisfied its
 * @ref ArgSpec, so required/unknown/type/enum/timestamp failures were rejected
 * by the registry before this runs).
 * @return On success a @ref CommandResult carrying the serialized link view and
 * the view's slug as audit target; on a service failure the translated
 * @ref CommandError and the caller-addressed slug; on a campaign decode
 * inconsistency an `internal` error.
 */
CommandResult handle_link_create(app::LinkCommandService& service,
                                 CommandContext& context,
                                 const ArgsView& args)
{
    app::CreateLinkCommand command;
    CommandError decode_error;
    if (!decode_create_link_command(args, &command, &decode_error)) {
        CommandResult result;
        result.error = std::move(decode_error);
        return result;
    }

    const app::Result<app::LinkView> result =
        context.dry_run ? service.ValidateCreateLink(command)
                        : service.CreateLink(command);

    return link_view_result(result, command.slug.value_or(""));
}

}  // namespace

std::vector<ArgSpec> link_create_arg_specs()
{
    return make_link_create_args_impl();
}

bool decode_create_link_command(const ArgsView& args,
                                app::CreateLinkCommand* out,
                                CommandError* error)
{
    if (out == nullptr) {
        return false;
    }

    app::CreateLinkCommand command;

    // target_url is required; the registry's ArgSpec presence check has already
    // guaranteed it is present as a string.
    command.target_url = args.get_string("target_url").value_or("");

    command.slug = args.get_string("slug");

    if (const std::optional<std::string> redirect_type =
            args.get_string("redirect_type")) {
        // The enum ArgSpec already restricted the value to temporary/permanent,
        // so this parse always yields a value.
        command.redirect_type = parseRedirectType(*redirect_type);
    }

    if (const std::optional<std::string> expires_at =
            args.get_string("expires_at")) {
        // Passed through verbatim; the service re-validates and may return
        // invalid_field (the timestamp ArgSpec has already checked the shape).
        command.expires_at = *expires_at;
    }

    if (const std::optional<bool> enabled = args.get_bool("enabled")) {
        command.enabled = *enabled;
    }

    if (std::optional<std::vector<std::string>> tags =
            args.get_string_list("tags")) {
        command.tags = std::move(*tags);
    }

    if (const std::optional<std::map<std::string, std::string>> metadata =
            args.get_string_map("metadata")) {
        command.metadata.insert(metadata->begin(), metadata->end());
    }

    // campaign: absent or explicit null -> no campaign (leave nullopt); a
    // present object is decoded. decode_campaign is only valid on a present
    // object, so it is guarded by the PATCH state.
    if (patch_state_of(args, "campaign") == patch_state::value) {
        if (!decode_campaign(args, "campaign", &command.campaign, error)) {
            return false;
        }
    }

    *out = std::move(command);
    return true;
}

CommandDescriptor make_link_create_descriptor(app::LinkCommandService& service)
{
    CommandDescriptor descriptor;
    descriptor.name = "link.create";
    descriptor.summary = "Create a short link.";
    descriptor.args = link_create_arg_specs();
    descriptor.permission = Permission::write;
    descriptor.kind = ExecutionKind::sync;
    descriptor.mutating = true;
    descriptor.supports_dry_run = true;
    descriptor.maps_to = {"POST /api/v1/links", "link create"};
    descriptor.handler =
        [&service](CommandContext& context, const ArgsView& args)
    {
        return handle_link_create(service, context, args);
    };
    return descriptor;
}

}  // namespace url_shortener::commands
