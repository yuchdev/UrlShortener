#include <cstddef>
#include <functional>
#include <string>
#include <utility>
#include <vector>

#include <url_shortener/app/link_command_service.hpp>
#include <url_shortener/commands/arg_spec.hpp>
#include <url_shortener/commands/command_descriptor.hpp>
#include <url_shortener/commands/link_command_codec.hpp>
#include <url_shortener/commands/link_commands.hpp>

namespace url_shortener::commands
{

namespace
{

/// Per-arg free-text byte cap (contract C3): the registry's generic cap is a
/// hard outer bound only, loose enough never to pre-empt the service's own slug
/// validation (which keeps its existing REST messages). Mirrors
/// `ServerConfig::max_request_body_bytes` and the create/update commands.
constexpr std::size_t k_text_max_length = 65536;

/**
 * @brief The one lifecycle operation each of the four descriptors binds to.
 *
 * Erases the specific service call (`DeleteLink`, `SetLinkEnabled(true|false)`,
 * `RestoreLink`) so a single shared handler and slug decoder serve all four
 * commands (milestone contract C1): the factories differ only in `name`,
 * `summary`, `maps_to`, and this bound call. Each invocation receives the
 * addressed slug and returns the service's `Result<LinkView>` verbatim.
 */
using lifecycle_call =
    std::function<app::Result<app::LinkView>(const std::string& slug)>;

/**
 * @brief Builds the single required `slug` string @ref ArgSpec shared by all
 * four lifecycle commands.
 *
 * `max_length` is raised to @ref k_text_max_length so the registry's generic
 * cap never pre-empts the service's slug handling (contract C3); length and
 * format validation stay in the service. The argument is required, so the
 * registry fails a missing value with `"slug: missing"` before the handler
 * runs.
 *
 * @return The one-element argument schema `{slug}` for a lifecycle descriptor.
 */
std::vector<ArgSpec> make_slug_args()
{
    ArgSpec slug;
    slug.name = "slug";
    slug.type = ArgType::string;
    slug.required = true;
    slug.max_length = k_text_max_length;
    return {std::move(slug)};
}

/**
 * @brief The one shared lifecycle handler: decodes the addressed slug and runs
 * the bound service call.
 *
 * The single slug decoder for every lifecycle command (requirement: no
 * duplicated per-command mapping). The registry's @ref ArgSpec presence check
 * has already guaranteed `slug` is present as a string, so it is read directly
 * and forwarded to @p call. The outcome is projected through
 * @ref link_view_result, whose success body is the shared
 * `app::serializeLinkViewJson` (contract C1/C3) and whose audit target is the
 * returned view's slug on success or the caller-addressed slug on failure
 * (never any other argument value, contract C6). No existence or field check
 * happens here: `not_found` for an unknown or empty slug and `storage_failure`
 * for a failed write both arise in the service.
 *
 * @param call The bound lifecycle operation (soft-delete, enable, disable, or
 * restore) to run for the addressed slug.
 * @param args The validated argument view; `slug` is guaranteed present.
 * @return On success a @ref CommandResult carrying the serialized post-mutation
 * link view and the view's slug as audit target; on a service failure the
 * translated @ref CommandError and the caller-addressed slug.
 */
CommandResult handle_lifecycle(const lifecycle_call& call, const ArgsView& args)
{
    const std::string slug = args.get_string("slug").value_or("");
    return link_view_result(call(slug), slug);
}

/**
 * @brief Builds one slug-only mutating lifecycle descriptor (shared factory).
 *
 * The single place the four lifecycle descriptors are constructed (milestone
 * contract C1): every one is `write`, `sync`, `mutating`, and
 * `supports_dry_run = false`. Setting `supports_dry_run = false` makes the
 * registry reject an `options.dry_run = true` request as `unsupported` before
 * the handler, so a dry-run can never trigger a real mutation (threat model
 * R-A, T-57). The handler is the one shared @ref handle_lifecycle bound to @p
 * call, so all four commands share one slug decoder.
 *
 * @param name The dotted command name (`link.delete`/`link.enable`/
 * `link.disable`/`link.restore`); must be unique in its registry.
 * @param summary The one-line human summary for the generated catalog and help.
 * @param maps_to The documentation-only REST/CLI mapping rows for the catalog
 * renderer (never used for routing).
 * @param call The bound service operation the handler delegates to.
 * @return A fully populated @ref CommandDescriptor ready for
 * @ref CommandRegistry::add.
 */
CommandDescriptor make_lifecycle_descriptor(std::string name,
                                            std::string summary,
                                            std::vector<std::string> maps_to,
                                            lifecycle_call call)
{
    CommandDescriptor descriptor;
    descriptor.name = std::move(name);
    descriptor.summary = std::move(summary);
    descriptor.args = make_slug_args();
    descriptor.permission = Permission::write;
    descriptor.kind = ExecutionKind::sync;
    descriptor.mutating = true;
    descriptor.supports_dry_run = false;
    descriptor.maps_to = std::move(maps_to);
    descriptor.handler = [call = std::move(call)](CommandContext& /*context*/,
                                                  const ArgsView& args)
    { return handle_lifecycle(call, args); };
    return descriptor;
}

}  // namespace

CommandDescriptor make_link_delete_descriptor(app::LinkCommandService& service)
{
    return make_lifecycle_descriptor(
        "link.delete",
        "Soft-delete a short link.",
        {"DELETE /api/v1/links/{slug}", "link delete"},
        [&service](const std::string& slug)
        { return service.DeleteLink({slug}); });
}

CommandDescriptor make_link_enable_descriptor(app::LinkCommandService& service)
{
    return make_lifecycle_descriptor(
        "link.enable",
        "Enable a short link.",
        {"POST /api/v1/links/{slug}/enable", "link enable"},
        [&service](const std::string& slug)
        { return service.SetLinkEnabled({slug, true}); });
}

CommandDescriptor make_link_disable_descriptor(app::LinkCommandService& service)
{
    return make_lifecycle_descriptor(
        "link.disable",
        "Disable a short link.",
        {"POST /api/v1/links/{slug}/disable", "link disable"},
        [&service](const std::string& slug)
        { return service.SetLinkEnabled({slug, false}); });
}

CommandDescriptor make_link_restore_descriptor(app::LinkCommandService& service)
{
    return make_lifecycle_descriptor(
        "link.restore",
        "Restore a soft-deleted short link.",
        {"POST /api/v1/links/{slug}/restore", "link restore"},
        [&service](const std::string& slug)
        { return service.RestoreLink({slug}); });
}

}  // namespace url_shortener::commands
