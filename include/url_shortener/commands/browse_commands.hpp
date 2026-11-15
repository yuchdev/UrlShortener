#pragma once

#include <url_shortener/commands/command_descriptor.hpp>

namespace url_shortener
{
namespace app
{

// Forward declaration: the descriptor factory binds its handler to a reference
// to this browse port. The full definition lives in `app/link_lister.hpp`,
// which transitively pulls in `app/link_command_service.hpp` (and thus Beast via
// `core/utils.hpp`); that header is therefore included only by the command
// `.cpp` files, never here (keeps this header free of storage/http includes,
// 01.0/13 include-hygiene test).
class ILinkLister;

}  // namespace app
}  // namespace url_shortener

namespace url_shortener::commands
{

/**
 * @brief Builds the `link.list` command descriptor (catalog 3.2).
 *
 * The one definition of the browse operation (milestone contract C1): the
 * returned descriptor is `read`, `sync`, non-`mutating` and does not support
 * `dry_run`. Its handler decodes the validated arguments into an
 * `app::LinkListQuery`, calls `app::ILinkLister::list`, and serializes the
 * resulting page with `app::serializeLinkListJson`, whose item shape is the
 * shared `serializeLinkViewJson` body a single-link read returns (contract
 * C1/C3). Every bounded-work constraint is declared on the descriptor's
 * @ref ArgSpec rows - `limit` in `1..500`, `offset >= 0`, and length caps on
 * `q`/`tag`/`campaign` - so an out-of-bounds query is rejected by the registry
 * before the lister performs any scan (threat model T-56); the lister
 * re-validates the query as defense in depth.
 *
 * Lifetime: the handler captures `&lister`, so @p lister must outlive the
 * @ref CommandRegistry the descriptor is added to. Because the registry is
 * non-copyable and non-movable and stores the handler, the handler can never
 * outlive the registry, but the registry itself must not outlive @p lister.
 *
 * @param lister The browse port the handler delegates to; must outlive the
 * registry the descriptor is registered on.
 * @return A fully populated @ref CommandDescriptor ready for
 * @ref CommandRegistry::add.
 */
CommandDescriptor make_link_list_descriptor(app::ILinkLister& lister);

}  // namespace url_shortener::commands
