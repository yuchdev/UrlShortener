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

}  // namespace url_shortener::commands
