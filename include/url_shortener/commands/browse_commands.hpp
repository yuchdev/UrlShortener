#pragma once

#include <url_shortener/commands/command_descriptor.hpp>

namespace url_shortener
{
namespace app
{

// Forward declaration: the descriptor factory binds its handler to a reference
// to this browse port. The full definition lives in `app/link_lister.hpp`,
// which transitively pulls in `app/link_command_service.hpp` (and thus Beast
// via `core/utils.hpp`); that header is therefore included only by the command
// `.cpp` files, never here (keeps this header free of storage/http includes,
// 01.0/13 include-hygiene test).
class ILinkLister;

// Forward declaration: the `db.*` descriptor factories bind their handlers to a
// reference to this allowlist. Its full definition lives in
// `app/table_source.hpp`, included only by the command `.cpp` so this header
// stays free of any heavier dependency (01.0/13 include-hygiene test).
class TableCatalog;

// Forward declaration: `register_browse_commands` takes an immutable
// @ref SystemInfo snapshot by value, which a declaration needs only forward-
// declared (the complete type is required at the function's definition and call
// sites). Its full definition lives in `app/system_info.hpp`, included only by
// the registering `.cpp`.
struct SystemInfo;

}  // namespace app
}  // namespace url_shortener

namespace url_shortener::commands
{

// Forward declaration: `register_browse_commands` takes the registry by
// reference and only calls `add`. The full definition lives in
// `commands/command_registry.hpp`, included by the registering `.cpp`, so this
// header stays free of the registry's `std::map`/envelope includes.
class CommandRegistry;

/**
 * @brief Registers the four browse commands on @p registry (milestone contract
 * C1): `link.list`, `db.tables`, `db.rows`, and `system.info`.
 *
 * The single registration entry point for the read/browse surface: it calls
 * @ref make_link_list_descriptor with @p lister, @ref make_db_tables_descriptor
 * and @ref make_db_rows_descriptor with @p catalog, and
 * @ref make_system_info_descriptor with a copy of @p info, then hands each
 * descriptor to @ref CommandRegistry::add. All four are `read`, `sync`,
 * non-`mutating` commands, so none is audited and both `ControlSet` roles may
 * call them. The link commands and the `command.list` built-in are registered
 * elsewhere.
 *
 * This function only builds descriptors and binds them to @p lister /
 * @p catalog / @p info; it never invokes a handler, opens a listener, or
 * touches `uri.txt`/`UriMapSingleton` (threat model T-58), so it is safe to
 * call from a catalog/registry builder.
 *
 * Lifetime: the `link.list`, `db.tables` and `db.rows` handlers capture
 * `&lister` / `&catalog`, so both @p lister and @p catalog must outlive
 * @p registry. The `system.info` descriptor copies @p info by value and so has
 * no lifetime dependency. Because the registry stores the handlers and is
 * non-copyable/non-movable, the handlers never outlive the registry, but the
 * registry must not outlive @p lister or @p catalog.
 *
 * @param registry The registry the four descriptors are added to; must be empty
 * of these four names (a duplicate name makes @ref CommandRegistry::add throw).
 * @param lister The browse port `link.list` and the `links` table source
 * delegate to; must outlive @p registry.
 * @param catalog The `db.*` table allowlist the `db.tables`/`db.rows` handlers
 * read; must outlive @p registry.
 * @param info The immutable system snapshot the `system.info` handler
 * serializes; copied into the descriptor so it needs no external owner.
 */
void register_browse_commands(CommandRegistry& registry,
                              app::ILinkLister& lister,
                              app::TableCatalog& catalog,
                              app::SystemInfo info);

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

/**
 * @brief Builds the `db.tables` command descriptor (catalog 3.2).
 *
 * The one definition of the "list browsable tables" operation. The returned
 * descriptor is `read`, `sync`, non-`mutating` and takes no arguments. Its
 * handler walks @p catalog in registration order, obtains each source's row
 * count via `app::ITableSource::row_count`, and serializes the result with
 * `app::serializeTablesJson`; a single row-count failure fails the whole call
 * with `storage_failure`. Because the catalog is the allowlist (milestone
 * contract C6), the handler lists exactly the registered sources and never
 * names a physical table or builds SQL.
 *
 * Lifetime: the handler captures `&catalog`, so @p catalog must outlive the
 * @ref CommandRegistry the descriptor is added to.
 *
 * @param catalog The table allowlist the handler reads; must outlive the
 * registry the descriptor is registered on.
 * @return A fully populated @ref CommandDescriptor ready for
 * @ref CommandRegistry::add.
 */
CommandDescriptor make_db_tables_descriptor(app::TableCatalog& catalog);

/**
 * @brief Builds the `db.rows` command descriptor (catalog 3.2).
 *
 * The one definition of the generic table-read operation. The returned
 * descriptor is `read`, `sync` and non-`mutating`. Its arguments are declared
 * so every bound is enforced by the registry before the handler runs: `table`
 * (required string), `q` (string, <= 256 bytes), `sort` (string), `order`
 * (enum `asc`/`desc`, default `asc`), `offset` (integer `>= 0`, default 0) and
 * `limit` (integer `1..500`, default 50). The handler resolves `table` through
 * `app::TableCatalog::find`; an unregistered name yields `not_found` with the
 * fixed message `table not found` that never echoes the requested name (threat
 * model T-51). A `sort` that does not name one of the source's `sortable`
 * columns yields `invalid_field` naming only `sort`. On success the handler
 * builds an `app::TableQuery`, calls `app::ITableSource::rows`, and serializes
 * the page with `app::serializeTableRowsJson`.
 *
 * Lifetime: the handler captures `&catalog`, so @p catalog must outlive the
 * @ref CommandRegistry the descriptor is added to.
 *
 * @param catalog The table allowlist the handler resolves names against; must
 * outlive the registry the descriptor is registered on.
 * @return A fully populated @ref CommandDescriptor ready for
 * @ref CommandRegistry::add.
 */
CommandDescriptor make_db_rows_descriptor(app::TableCatalog& catalog);

/**
 * @brief Raise-only permission for a status-filtered link enumeration:
 * `status=all|deleted` require `write`/admin, every other status stays `read`
 * (threat model T-71).
 *
 * The single shared rule behind both `link.list` (`make_link_list_descriptor`)
 * and `link.export` (`make_link_export_descriptor`), so the two enforcement
 * points can never drift: a `User` may enumerate `active`/`disabled`/`expired`
 * links but the bulk enumeration of `all` or soft-`deleted` links is a write
 * exposure (user rulings 2026-11-16/17). The function only ever **raises** to
 * `write`; it never lowers a caller's base permission (01.0 T-09). It is pure
 * and never throws; `status` has already been enum-validated by the registry, so
 * an out-of-set value can never reach it, and an absent `status` reads as the
 * descriptor default (`active`) and keeps `read`.
 *
 * @param args The validated argument view; only its optional `status` string is
 * read.
 * @return `Permission::write` when `status` is exactly `all` or `deleted`;
 * `Permission::read` otherwise (including when `status` is absent).
 */
Permission link_status_permission(const ArgsView& args);

}  // namespace url_shortener::commands
