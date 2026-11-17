#pragma once

#include <cstddef>
#include <memory>

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
 * @brief Dependencies the pair commands (`link.import`, later `link.export`)
 * capture into their job bodies (Task 05.0 subtask 08).
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
};

/**
 * @brief Builds the `link.import` command descriptor (catalog 3.3).
 *
 * The one definition of the bulk-import operation (milestone contract C1): the
 * returned descriptor is `write`, `kind == job`, `mutating`, `supports_dry_run`,
 * with the catalog argument schema (`format` enum required, `data`/`path`
 * strings, `on_conflict` enum default `skip`). Its job factory validates the
 * arguments, opens the inline/file source and returns a self-contained
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
 * @brief Registers the pair commands on @p registry (currently `link.import`;
 * `link.export` is added by subtask 10).
 *
 * The single registration entry point for the pair command family: it builds
 * the descriptor from @p deps and hands it to @ref CommandRegistry::add. This
 * function only builds the descriptor and copies @p deps into it; it never runs
 * a body, opens a listener or touches storage.
 *
 * @param registry The registry the descriptor is added to; must not already
 * contain a `link.import` command (a duplicate name makes
 * @ref CommandRegistry::add throw).
 * @param deps The service and path/size policy the body captures by value; see
 * @ref PairCommandDeps.
 */
void register_pair_commands(CommandRegistry& registry, PairCommandDeps deps);

}  // namespace url_shortener::commands
