#pragma once

#include <cstddef>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include <url_shortener/app/app_error.hpp>
#include <url_shortener/commands/command_descriptor.hpp>
#include <url_shortener/commands/pair_file_access.hpp>

namespace url_shortener
{
namespace app
{

// Forward declaration: the import job factory binds to a shared service and
// only calls its `CreateLink`/`ValidateCreateLink`. The full definition lives
// in `app/link_command_service.hpp`, which transitively pulls in Beast via
// `core/utils.hpp`; that header is therefore included only by the registering
// `.cpp`, never here (01.0/13 include-hygiene test).
class LinkCommandService;

// Forward declaration: the export page source yields already-decoded link views
// the export body serializes. The full `LinkView` definition lives in
// `app/link_command_service.hpp` (Beast via `core/utils.hpp`); declaring it
// here keeps this header off the forbidden include list while still letting
// `LinkPage` name it (a `std::vector<LinkView>` member and a `Result<LinkPage>`
// alias need `LinkView` complete only in the `.cpp`, never here).
struct LinkView;

}  // namespace app
}  // namespace url_shortener

namespace url_shortener::commands
{

// Forward declaration: `register_pair_commands` takes the process registry by
// reference and only calls `add`. The full definition lives in
// `commands/command_registry.hpp`, included by the registering `.cpp`, so this
// header stays free of the registry's `std::map`/envelope includes.
class CommandRegistry;

/**
 * @brief The filtered-link selection a `link.export` job pages through.
 *
 * A plain mirror of the `link.list` filter set (03.0/14) as strings, so this
 * header need not include the browse/lister headers. `build_command_runtime`
 * (05.0/20) binds a @ref LinkPageSource that translates one of these into an
 * `app::LinkListQuery` against the shared `ILinkLister`, keeping export on the
 * one query path `link.list` uses (constraint C1). `sort`/`order` are fixed to
 * `created_at`/`desc` by the export command for stable paging and are carried
 * here only so the single adapter can stay identical to `link.list`.
 */
struct LinkListFilter
{
    ///< Lifecycle filter token (`all`/`active`/`disabled`/`expired`/`deleted`);
    ///< defaults to `active`, matching `link.list`.
    std::string status = "active";

    ///< Case-insensitive substring filter on slug or target URL; empty means no
    ///< text filter.
    std::string q;

    ///< Exact-match tag filter; empty means no tag filter.
    std::string tag;

    ///< Exact-match campaign-name filter; empty means no campaign filter.
    std::string campaign;

    ///< Order field; fixed to `created_at` by the export command for stable
    ///< offset paging.
    std::string sort = "created_at";

    ///< Order direction; fixed to `desc` by the export command.
    std::string order = "desc";
};

/**
 * @brief One filtered, counted page of link views returned by a
 * @ref LinkPageSource.
 *
 * Mirrors `app::LinkListPage` but carries only what the export body needs: the
 * window of views and the total match count (stable across pages) used to bound
 * the paging loop and declare the job's progress total.
 */
struct LinkPage
{
    ///< The requested window of already-decoded views in
    ///< `[offset, offset + limit)`; empty past the end.
    std::vector<app::LinkView> items;

    ///< Number of links matching the filter before the window was applied;
    ///< independent of `offset`/`limit`.
    std::size_t total = 0;
};

/**
 * @brief The single query path a `link.export` job pages through.
 *
 * A callable from `(filter, offset, limit)` to one @ref LinkPage, bound by
 * `build_command_runtime` (05.0/20) over the shared `app::ILinkLister` so
 * export never queries `IMetadataRepository` directly (constraint C1). The
 * export body captures a copy by value, so it must own everything it needs (the
 * binding captures its `std::shared_ptr<ILinkLister>` by value, threat model
 * T-87).
 *
 * @return `app::Result<LinkPage>` with the page on success, or a failure whose
 * code the body maps to the job's `failed` outcome.
 */
using LinkPageSource = std::function<app::Result<LinkPage>(
    const LinkListFilter& filter, std::size_t offset, std::size_t limit)>;

/**
 * @brief The page size a `link.export` job requests from its
 * @ref LinkPageSource.
 *
 * Fixed so the file sink streams page by page and the inline buffer grows a
 * bounded amount per iteration rather than materializing every match at once.
 */
inline constexpr std::size_t kExportPageSize = 200;

/**
 * @brief Dependencies the pair commands (`link.import`, `link.export`) capture
 * into their job bodies (Task 05.0 subtasks 08, 10).
 *
 * A plain value aggregate: `register_pair_commands` deep-copies exactly these
 * fields into each job body so a body holds no reference to the registry, a
 * per-request `CommandRuntime`, the caller, the request buffer or this struct
 * (threat model T-87). The `service` is shared by ownership (a
 * `std::shared_ptr` copy), every other field is a value.
 */
struct PairCommandDeps
{
    ///< The link command service each import row is created through; shared by
    ///< ownership with the `link.*` handlers. Must be non-null when a real
    ///< (non-dry-run) import runs. The job body keeps a `std::shared_ptr` copy,
    ///< so the service outlives the registry that registered the command.
    std::shared_ptr<app::LinkCommandService> service;

    ///< Maximum accepted inline `data` payload in bytes; the caller passes
    ///< `ServerConfig::max_request_body_bytes`. A larger inline payload is
    ///< refused by the factory as `invalid_field`.
    std::size_t max_inline_bytes = 65536;

    ///< The local-filesystem capability and optional root jail applied to a
    ///< `path` argument. Default-deny (`allow_paths == false`, threat model
    ///< T-95): only the local-operator compositions (`cmd`/`tui`/`serve-stdio`)
    ///< grant it, so the REST per-request runtime refuses `path` as
    ///< `unsupported` by this capability alone, never by a transport name.
    PathPolicy file_paths;

    ///< The single filtered-link query path a `link.export` job pages through,
    ///< bound by `build_command_runtime` (05.0/20) over the shared
    ///< `app::ILinkLister`. Default-constructed (empty) means "export
    ///< unavailable": @ref register_pair_commands then registers only
    ///< `link.import`, so `link.export` is absent from `command.list`. When
    ///< set, the export body captures a copy by value (threat model T-87).
    LinkPageSource list_page;
};

/**
 * @brief Builds the `link.export` command descriptor (catalog 3.3).
 *
 * The one definition of the bulk-export operation (contract C1): the returned
 * descriptor is a `read` `kind == job` command that is **not** mutating and has
 * no dry-run. Its base permission is `read`, raised to `write` by a
 * @ref PermissionHook whenever a `path` argument directs the export to a file
 * (decision log D16), so a read-only caller is denied `path` export before the
 * factory runs while inline export stays `read`. The factory pages through
 * @p deps `.list_page`, serializes each link as CSV or NDJSON and delivers it
 * as size-capped inline `data` or atomically to the requested `path`.
 *
 * Lifetime: the factory and the bodies it builds deep-copy @p deps (the
 * `std::function` page source and the plain values), so a body holds no
 * reference to @p deps, the caller, the request or the registry (threat model
 * T-87).
 *
 * @param deps The page source and path/size policy the body captures by value;
 * @ref PairCommandDeps::list_page must be set (the caller guarantees this).
 * @return A fully populated @ref CommandDescriptor ready for
 * @ref CommandRegistry::add.
 */
CommandDescriptor make_link_export_descriptor(PairCommandDeps deps);

/**
 * @brief Builds the `link.import` command descriptor (catalog 3.3).
 *
 * The one definition of the bulk-import operation (milestone contract C1): the
 * returned descriptor is `write`, `kind == job`, `mutating`,
 * `supports_dry_run`, with the catalog argument schema (`format` enum required,
 * `data`/`path` strings, `on_conflict` enum default `skip`). Its job factory
 * validates the arguments, opens the inline/file source and returns a
 * self-contained
 * @ref JobBody that creates each row through
 * `app::LinkCommandService::CreateLink` (or `ValidateCreateLink` under
 * `options.dry_run`). No URL/slug/SSRF/conflict check is performed in the body;
 * all domain validation stays in the service.
 *
 * Lifetime: the factory and the body it returns deep-copy @p deps (the
 * `std::shared_ptr` service and the plain values), so the body holds no
 * reference to @p deps, the caller, the request or the registry (threat model
 * T-87).
 *
 * @param deps The service and path/size policy the body captures by value.
 * @return A fully populated @ref CommandDescriptor ready for
 * @ref CommandRegistry::add.
 */
CommandDescriptor make_link_import_descriptor(PairCommandDeps deps);

/**
 * @brief Registers the pair commands on @p registry (`link.import`, and
 * `link.export` when a page source is available).
 *
 * The single registration entry point for the pair command family: it builds
 * the `link.import` descriptor from @p deps and hands it to
 * @ref CommandRegistry::add, then - only when @p deps `.list_page` is set -
 * builds and adds the `link.export` descriptor. A default-constructed
 * `list_page` means "export unavailable", so `link.export` is simply not
 * registered (and therefore absent from `command.list`). This function only
 * builds descriptors and copies @p deps into them; it never runs a body, opens
 * a listener or touches storage.
 *
 * @param registry The registry the descriptors are added to; must not already
 * contain a `link.import` (or, when `list_page` is set, `link.export`) command
 * (a duplicate name makes @ref CommandRegistry::add throw).
 * @param deps The service, page source and path/size policy the bodies capture
 * by value; see @ref PairCommandDeps.
 */
void register_pair_commands(CommandRegistry& registry, PairCommandDeps deps);

}  // namespace url_shortener::commands
