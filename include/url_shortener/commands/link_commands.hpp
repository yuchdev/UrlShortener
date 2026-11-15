#pragma once

#include <url_shortener/commands/command_descriptor.hpp>

namespace url_shortener
{
namespace app
{

// Forward declaration: the descriptor factories bind their handlers to a
// reference to this service. The full definition lives in
// `app/link_command_service.hpp`, which transitively pulls in Beast via
// `core/utils.hpp`; that header is therefore included only by the command
// `.cpp` files, never here (keeps this header free of storage/http includes).
class LinkCommandService;

}  // namespace app
}  // namespace url_shortener

namespace url_shortener::commands
{

/**
 * @brief Builds the `link.create` command descriptor (catalog 3.1).
 *
 * The one definition of the create operation (milestone contract C1): the
 * returned descriptor is `write`, `sync`, `mutating`, `supports_dry_run`, and
 * its handler decodes the validated arguments into an `app::CreateLinkCommand`,
 * calls `app::LinkCommandService::CreateLink` (or `ValidateCreateLink` under
 * `options.dry_run`), and projects the outcome with `app::serializeLinkViewJson`
 * via @ref link_view_result. All domain validation (URL normalization, SSRF
 * checks, slug/tag/metadata/campaign rules) stays in the service; the handler
 * only decodes and forwards.
 *
 * Lifetime: the handler captures `&service`, so @p service must outlive the
 * @ref CommandRegistry the descriptor is added to. Because the registry is
 * non-copyable and non-movable and stores the handler, the handler can never
 * outlive the registry, but the registry itself must not outlive @p service.
 *
 * @param service The application service the handler delegates to; must outlive
 * the registry the descriptor is registered on.
 * @return A fully populated @ref CommandDescriptor ready for
 * @ref CommandRegistry::add.
 */
CommandDescriptor make_link_create_descriptor(app::LinkCommandService& service);

}  // namespace url_shortener::commands
