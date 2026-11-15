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
/// the registry never rejects input the service's `validateTags`/
/// `validateMetadata` would otherwise classify with their own messages.
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
 * @brief Builds the nullable `campaign` object @ref ArgSpec for `link.update`.
 *
 * Declares the six optional UTM string members; `nullable = true` so an explicit
 * JSON `null` is accepted and distinguished from an absent argument. For a PATCH
 * update the three states carry distinct meaning (contract C3): absent leaves
 * the campaign untouched, `null` clears it, a present object replaces it.
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
 * @brief Builds the argument schema for `link.update` (catalog 3.1 table).
 *
 * The addressed `slug` is the only required argument; `target_url` is absent
 * because it is immutable through this command (same as REST PATCH today). Every
 * mutable field is optional with PATCH semantics: `enabled` (bool), `expires_at`
 * (nullable timestamp), `tags` (string list), `metadata` (string map), and
 * `campaign` (nullable object). Both `expires_at` and `campaign` carry
 * `nullable = true` so an explicit JSON `null` is kept distinct from an absent
 * argument (three states, contract C3). Every free-text arg and the list/map
 * args carry the raised contract-C3 caps so the single validator remains
 * `LinkCommandService` (see @ref k_text_max_length / @ref k_collection_max_items).
 *
 * @return The ordered @ref ArgSpec list for the descriptor.
 */
std::vector<ArgSpec> make_link_update_args()
{
    ArgSpec expires_at;
    expires_at.name = "expires_at";
    expires_at.type = ArgType::timestamp;
    expires_at.nullable = true;
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

    return {make_string_arg("slug", true),
            std::move(enabled),
            std::move(expires_at),
            std::move(tags),
            std::move(metadata),
            make_campaign_arg()};
}

/**
 * @brief Decodes the validated arguments and runs the update (or its dry-run).
 *
 * Mirrors the REST `PATCH /api/v1/links/{slug}` decode: the addressed `slug`
 * selects the record, and only the present mutable fields are copied onto the
 * `app::UpdateLinkCommand`. The PATCH tri-state (contract C3) is preserved
 * verbatim for both nullable fields through @ref patch_state_of: an absent
 * argument leaves the double-optional field `nullopt` (untouched), an explicit
 * JSON `null` sets the outer optional with an empty inner (clear), and a value
 * sets both layers (replace). `tags`/`metadata` are plain replacements: a
 * present empty list/map removes every entry, an absent one leaves the field
 * untouched. Under `context.dry_run` the call routes to `ValidateUpdateLink`
 * (validates against the stored record, never writes); otherwise to
 * `UpdateLink`. The outcome is projected through @ref link_view_result, whose
 * audit target is the view's slug on success or the caller-addressed slug on
 * failure (never a URL or body value, contract C6). No field validation happens
 * here: expiry parsing and the tag/metadata/campaign limit checks all stay in
 * the service.
 *
 * @param service The service the update is delegated to.
 * @param context The per-invocation context; only `dry_run` is read here.
 * @param args The validated argument view (every arg already satisfied its
 * @ref ArgSpec, so required/unknown/type/timestamp failures were rejected by the
 * registry before this runs).
 * @return On success a @ref CommandResult carrying the serialized updated link
 * view and the view's slug as audit target; on a service failure the translated
 * @ref CommandError and the caller-addressed slug; on a campaign decode
 * inconsistency an `internal` error.
 */
CommandResult handle_link_update(app::LinkCommandService& service,
                                 CommandContext& context,
                                 const ArgsView& args)
{
    app::UpdateLinkCommand command;

    // slug is required; the registry's ArgSpec presence check has already
    // guaranteed it is present as a string.
    command.slug = args.get_string("slug").value_or("");

    if (args.has("enabled")) {
        command.enabled = args.get_bool("enabled");
    }

    // expires_at tri-state: absent -> leave nullopt; explicit null -> outer
    // present, inner empty (clear); value -> outer and inner present (replace).
    switch (patch_state_of(args, "expires_at")) {
        case patch_state::absent:
            break;
        case patch_state::null_value:
            command.expires_at = std::optional<std::string>{};
            break;
        case patch_state::value:
            command.expires_at = args.get_string("expires_at");
            break;
    }

    if (args.has("tags")) {
        // A present list (including []) replaces the whole tag set.
        command.tags = args.get_string_list("tags").value_or(
            std::vector<std::string>{});
    }

    if (args.has("metadata")) {
        // A present object (including {}) replaces the whole metadata map.
        const std::optional<std::map<std::string, std::string>> metadata =
            args.get_string_map("metadata");
        std::unordered_map<std::string, std::string> replacement;
        if (metadata.has_value()) {
            replacement.insert(metadata->begin(), metadata->end());
        }
        command.metadata = std::move(replacement);
    }

    // campaign tri-state: absent -> leave nullopt; explicit null -> outer
    // present, inner empty (clear); value -> decode and set both layers.
    switch (patch_state_of(args, "campaign")) {
        case patch_state::absent:
            break;
        case patch_state::null_value:
            command.campaign = std::optional<Link::Campaign>{};
            break;
        case patch_state::value: {
            std::optional<Link::Campaign> decoded;
            CommandError error;
            if (!decode_campaign(args, "campaign", &decoded, &error)) {
                CommandResult result;
                result.error = std::move(error);
                return result;
            }
            command.campaign = std::move(decoded);
            break;
        }
    }

    const app::Result<app::LinkView> result =
        context.dry_run ? service.ValidateUpdateLink(command)
                        : service.UpdateLink(command);

    return link_view_result(result, command.slug);
}

}  // namespace

CommandDescriptor make_link_update_descriptor(app::LinkCommandService& service)
{
    CommandDescriptor descriptor;
    descriptor.name = "link.update";
    descriptor.summary = "Apply a partial update to a short link.";
    descriptor.args = make_link_update_args();
    descriptor.permission = Permission::write;
    descriptor.kind = ExecutionKind::sync;
    descriptor.mutating = true;
    descriptor.supports_dry_run = true;
    descriptor.maps_to = {"PATCH /api/v1/links/{slug}", "link update"};
    descriptor.handler =
        [&service](CommandContext& context, const ArgsView& args)
    {
        return handle_link_update(service, context, args);
    };
    return descriptor;
}

}  // namespace url_shortener::commands
