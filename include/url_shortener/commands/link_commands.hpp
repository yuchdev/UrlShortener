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

// Forward declaration: `register_link_commands` takes the process registry by
// reference and only calls `add`. The full definition lives in
// `commands/command_registry.hpp`, included by the registering `.cpp`, so this
// header stays free of the registry's `std::map`/envelope includes.
class CommandRegistry;

/**
 * @brief Registers the nine link commands on @p registry (milestone contract
 * C1/C3): `link.create`, `link.get`, `link.update`, `link.delete`,
 * `link.enable`, `link.disable`, `link.restore`, `link.preview`, `link.stats`.
 *
 * The single registration entry point for the link command family: it calls the
 * nine `make_link_*_descriptor` factories with @p service and hands each
 * descriptor to @ref CommandRegistry::add, in catalog order. Every descriptor's
 * handler produces exactly the body and error class the equivalent REST
 * resource route produces today (the parity gate proves this), so a transport
 * can swap onto the registry without changing any status, field, or error code
 * (contract C3). `link.list` and the job commands are registered elsewhere.
 *
 * This function only builds descriptors and binds them to @p service; it never
 * invokes a handler, opens a listener, or touches `uri.txt`/`UriMapSingleton`
 * (threat model T-58), so it is safe to call from a catalog/registry builder.
 *
 * Lifetime: each descriptor's handler captures `&service`, so @p service must
 * outlive @p registry. Because the registry stores the handlers and is
 * non-copyable/non-movable, the handlers never outlive the registry, but the
 * registry must not outlive @p service.
 *
 * @param registry The registry the nine descriptors are added to; must be empty
 * of these nine names (a duplicate name makes @ref CommandRegistry::add throw).
 * @param service The application service every handler delegates to; must
 * outlive @p registry.
 */
void register_link_commands(CommandRegistry& registry,
                            app::LinkCommandService& service);

/**
 * @brief Builds the `link.create` command descriptor (catalog 3.1).
 *
 * The one definition of the create operation (milestone contract C1): the
 * returned descriptor is `write`, `sync`, `mutating`, `supports_dry_run`, and
 * its handler decodes the validated arguments into an `app::CreateLinkCommand`,
 * calls `app::LinkCommandService::CreateLink` (or `ValidateCreateLink` under
 * `options.dry_run`), and projects the outcome with
 * `app::serializeLinkViewJson` via @ref link_view_result. All domain validation
 * (URL normalization, SSRF checks, slug/tag/metadata/campaign rules) stays in
 * the service; the handler only decodes and forwards.
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

/**
 * @brief Builds the `link.update` command descriptor (catalog 3.1).
 *
 * The one definition of the partial-update operation (milestone contract C1):
 * the returned descriptor is `write`, `sync`, `mutating`, `supports_dry_run`,
 * and its handler decodes the validated arguments into an
 * `app::UpdateLinkCommand`, calls `app::LinkCommandService::UpdateLink` (or
 * `ValidateUpdateLink` under `options.dry_run`), and projects the outcome with
 * `app::serializeLinkViewJson` via @ref link_view_result. PATCH semantics are
 * identical to `PATCH /api/v1/links/{slug}`: an absent argument leaves the
 * field untouched, an explicit JSON `null` clears the nullable
 * `expires_at`/`campaign`, and a value replaces. The handler only decodes the
 * three states (via
 * @ref patch_state_of) into the double-optional command fields; all domain
 * validation (expiry parsing, SSRF/URL checks are not applicable here, and
 * tag/metadata/campaign limits) stays in the service. `target_url` is immutable
 * through this command and is therefore not a declared argument.
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
CommandDescriptor make_link_update_descriptor(app::LinkCommandService& service);

/**
 * @brief Builds the `link.get` command descriptor (catalog 3.1).
 *
 * The one definition of the read-by-identifier operation (milestone contract
 * C1): the returned descriptor is `read`, `sync`, non-`mutating` and does not
 * support `dry_run`. Its handler selects the single present `slug`/`id`
 * argument via @ref decode_slug_or_id, calls
 * `app::LinkCommandService::GetLink` (the by-`id` path is the indexed
 * `findById` lookup, never a scan), and projects the outcome with
 * `app::serializeLinkViewJson` via @ref link_view_result. The registry enforces
 * "exactly one of `slug`/`id`" through the `slug_or_id` one-of group before the
 * handler runs; no field validation lives in the handler.
 *
 * Lifetime: the handler captures `&service`, so @p service must outlive the
 * @ref CommandRegistry the descriptor is added to.
 *
 * @param service The application service the handler delegates to; must outlive
 * the registry the descriptor is registered on.
 * @return A fully populated @ref CommandDescriptor ready for
 * @ref CommandRegistry::add.
 */
CommandDescriptor make_link_get_descriptor(app::LinkCommandService& service);

/**
 * @brief Builds the `link.preview` command descriptor (catalog 3.1).
 *
 * The one definition of the preview operation (milestone contract C1): the
 * returned descriptor is `read`, `sync`, non-`mutating` and does not support
 * `dry_run`. Its handler selects the single present `slug`/`id` argument via
 * @ref decode_slug_or_id, calls `app::LinkCommandService::PreviewLink`, and
 * projects the outcome through the reduced-projection serializer
 * `app::serializeLinkPreviewJson` via @ref link_preview_result. The registry
 * enforces "exactly one of `slug`/`id`" through the `slug_or_id` one-of group
 * before the handler runs.
 *
 * Lifetime: the handler captures `&service`, so @p service must outlive the
 * @ref CommandRegistry the descriptor is added to.
 *
 * @param service The application service the handler delegates to; must outlive
 * the registry the descriptor is registered on.
 * @return A fully populated @ref CommandDescriptor ready for
 * @ref CommandRegistry::add.
 */
CommandDescriptor make_link_preview_descriptor(
    app::LinkCommandService& service);

/**
 * @brief Builds the `link.stats` command descriptor (catalog 3.1).
 *
 * The one definition of the stats operation (milestone contract C1): the
 * returned descriptor is `read`, `sync`, non-`mutating` and does not support
 * `dry_run`. Its handler reproduces the two REST result shapes exactly: when
 * `from`, `to` and `bucket` are all supplied (and non-empty) it calls
 * `app::LinkCommandService::GetLinkStats` and projects the range report with
 * `app::serializeLinkStatsJson` via @ref link_stats_result; otherwise it calls
 * `app::LinkCommandService::GetLink` for the addressed slug and emits the
 * lifetime-counter body with `app::serializeLinkLifetimeStatsJson`. The
 * `bucket` argument is a free string (not an `enumeration` @ref ArgSpec) so
 * timestamp and bucket validation stay in `ILinkStatsReader`, preserving the
 * REST `invalid_timestamp` / `invalid_bucket` details (contract C3); the
 * handler never parses `from`/`to`/`bucket` itself.
 *
 * Lifetime: the handler captures `&service`, so @p service must outlive the
 * @ref CommandRegistry the descriptor is added to.
 *
 * @param service The application service the handler delegates to; must outlive
 * the registry the descriptor is registered on.
 * @return A fully populated @ref CommandDescriptor ready for
 * @ref CommandRegistry::add.
 */
CommandDescriptor make_link_stats_descriptor(app::LinkCommandService& service);

/**
 * @brief Builds the `link.delete` command descriptor (catalog 3.1).
 *
 * The one definition of the soft-delete operation (milestone contract C1): the
 * returned descriptor is `write`, `sync`, `mutating`, and does NOT support
 * `dry_run` (the catalog lists no dry-run mode, so `options.dry_run = true` is
 * rejected by the registry as `unsupported` before the handler runs, never a
 * silent real mutation). Its single `slug` argument is decoded by the one
 * shared lifecycle slug decoder, the handler calls
 * `app::LinkCommandService::DeleteLink`, and the outcome is projected with
 * `app::serializeLinkViewJson` via @ref link_view_result. Preserves today's
 * REST `DELETE /api/v1/links/{slug}` semantics exactly: `deleted_at` and
 * `updated_at` are set, the enabled flag is untouched, and re-deleting an
 * already-deleted link succeeds and overwrites `deleted_at`.
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
CommandDescriptor make_link_delete_descriptor(app::LinkCommandService& service);

/**
 * @brief Builds the `link.enable` command descriptor (catalog 3.1).
 *
 * The one definition of the enable operation (milestone contract C1): the
 * returned descriptor is `write`, `sync`, `mutating`, and does NOT support
 * `dry_run` (an `options.dry_run = true` request is rejected by the registry as
 * `unsupported` before the handler, never a silent real mutation). Its single
 * `slug` argument is decoded by the one shared lifecycle slug decoder, the
 * handler calls `app::LinkCommandService::SetLinkEnabled` with the enabled flag
 * set to `true`, and the outcome is projected with `app::serializeLinkViewJson`
 * via @ref link_view_result. Preserves today's REST
 * `POST /api/v1/links/{slug}/enable` semantics exactly, including that enabling
 * a soft-deleted link succeeds while its computed status stays `deleted`.
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
CommandDescriptor make_link_enable_descriptor(app::LinkCommandService& service);

/**
 * @brief Builds the `link.disable` command descriptor (catalog 3.1).
 *
 * The one definition of the disable operation (milestone contract C1): the
 * returned descriptor is `write`, `sync`, `mutating`, and does NOT support
 * `dry_run` (an `options.dry_run = true` request is rejected by the registry as
 * `unsupported` before the handler, never a silent real mutation). Its single
 * `slug` argument is decoded by the one shared lifecycle slug decoder, the
 * handler calls `app::LinkCommandService::SetLinkEnabled` with the enabled flag
 * set to `false`, and the outcome is projected with
 * `app::serializeLinkViewJson` via @ref link_view_result. Preserves today's
 * REST `POST /api/v1/links/{slug}/disable` semantics exactly.
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
CommandDescriptor make_link_disable_descriptor(
    app::LinkCommandService& service);

/**
 * @brief Builds the `link.restore` command descriptor (catalog 3.1).
 *
 * The one definition of the restore operation (milestone contract C1): the
 * returned descriptor is `write`, `sync`, `mutating`, and does NOT support
 * `dry_run` (an `options.dry_run = true` request is rejected by the registry as
 * `unsupported` before the handler, never a silent real mutation). Its single
 * `slug` argument is decoded by the one shared lifecycle slug decoder, the
 * handler calls `app::LinkCommandService::RestoreLink`, and the outcome is
 * projected with `app::serializeLinkViewJson` via @ref link_view_result.
 * Preserves today's REST `POST /api/v1/links/{slug}/restore` semantics exactly:
 * only `deleted_at` is cleared (and `updated_at` refreshed).
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
CommandDescriptor make_link_restore_descriptor(
    app::LinkCommandService& service);

}  // namespace url_shortener::commands
