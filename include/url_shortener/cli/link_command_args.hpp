/**
 * @file link_command_args.hpp
 * @brief argv-to-DTO mapping for the `link` subcommands (create, get, update,
 * delete, enable, disable, restore, preview, stats).
 *
 * Each function parses the flag tokens that follow the verb (i.e. everything
 * after `url_shortener link <verb>`) into one of the shared
 * `url_shortener::app` command/query DTOs. Reusing those DTOs (rather than a
 * CLI-local struct) keeps a single command contract shared by the CLI and REST
 * adapters (Milestone 0003 contract C1). No storage, network, or Beast type is
 * touched here - this layer is a pure, transport-only argv mapper.
 */
#pragma once

#include <string>
#include <vector>

#include <url_shortener/app/link_command_service.hpp>
#include <url_shortener/url_shortener.h>

namespace url_shortener {
namespace cli {

/**
 * @brief Parse the flags of `link create` into a CreateLinkCommand.
 *
 * Recognized flags (all values are treated as untrusted argv input):
 *  - `--url <URL>` (required)            -> CreateLinkCommand::target_url
 *  - `--slug <SLUG>`                     -> CreateLinkCommand::slug
 *  - `--redirect-type <temporary|permanent>` -> CreateLinkCommand::redirect_type
 *  - `--expires-at <RFC3339>`            -> CreateLinkCommand::expires_at
 *  - `--enabled <BOOL>`                  -> CreateLinkCommand::enabled
 *  - `--tag <TAG>` (repeatable)          -> CreateLinkCommand::tags
 *  - `--metadata <KEY=VALUE>` (repeatable) -> CreateLinkCommand::metadata
 *  - `--campaign-name/-source/-medium/-term/-content/-id <VALUE>`
 *                                        -> CreateLinkCommand::campaign
 *  - `--base-domain <URL>`               -> config.shortener_base_domain
 *  - `--allow-private-targets`           -> config.shortener_allow_private_targets
 *
 * Only structural validation happens here (required/empty/enum/`key=value`
 * shape). Semantic checks - reachable targets, reserved slugs, RFC3339
 * validity - remain the responsibility of `LinkCommandService`.
 *
 * @param args  Flag tokens after the verb (argv[3..argc-1]).
 * @param config ServerConfig receiving create-time overrides (`--base-domain`,
 *        `--allow-private-targets`) that the dispatcher applies to the service.
 * @return app::CreateLinkCommand The parsed create command.
 * @throws std::invalid_argument On a missing/empty `--url`, an unknown flag, or
 *         a malformed value (bad boolean, redirect type, or metadata token).
 */
app::CreateLinkCommand parseCreateArgs(
    const std::vector<std::string>& args,
    ServerConfig& config);

/**
 * @brief Parse the flags of `link get` into a GetLinkQuery.
 *
 * Exactly one of `--slug <SLUG>` or `--id <ID>` must be supplied; passing both
 * or neither is a parse error. `--slug` selects @c GetLinkBy::slug and `--id`
 * selects @c GetLinkBy::id, with the flag value copied verbatim into
 * @c GetLinkQuery::value.
 *
 * @param args Flag tokens after the verb (argv[3..argc-1]).
 * @return app::GetLinkQuery The parsed lookup query.
 * @throws std::invalid_argument If both or neither selector is given, on an
 *         unknown flag, or on an empty selector value.
 */
app::GetLinkQuery parseGetArgs(const std::vector<std::string>& args);

/**
 * @brief Parse the flags of `link update` into an UpdateLinkCommand.
 *
 * `--slug <SLUG>` is the required selector; every other flag is optional and
 * maps onto @ref app::UpdateLinkCommand's PATCH-style three-state fields. A
 * field left off the command line stays "absent" (leave untouched); the value
 * and explicit-clear cases are folded onto each field's single flag:
 *  - `--enabled <BOOL>`      -> UpdateLinkCommand::enabled (present => set).
 *  - `--expires-at <RFC3339>` -> expires_at present-with-value (set expiry);
 *    the literal `--expires-at clear` is expires_at present-null (clear it).
 *  - `--tags <A,B,C>`        -> tags present with the comma-split replacement
 *    list; `--tags ""` is a present, empty list (drop all tags).
 *  - `--metadata <K=V,...>`  -> metadata present with the comma/`=`-split
 *    replacement map; `--metadata ""` is a present, empty map.
 *  - `--campaign-name/-source/-medium/-term/-content/-id <VALUE>`
 *                            -> campaign present-with-value.
 *  - `--clear-campaign`      -> campaign present-null (clear the campaign).
 *
 * `--campaign-*` and `--clear-campaign` remain mutually exclusive (passing both
 * is a parse error). Only argv well-formedness is enforced here - comma/`=`
 * tokenizing for `--tags`/`--metadata` - while RFC3339 validity, tag/metadata
 * limits, and campaign-field limits are validated independently by
 * `LinkCommandService` (via `validateTags`/`validateMetadata`).
 *
 * @param args Flag tokens after the verb (argv[3..argc-1]).
 * @return app::UpdateLinkCommand The parsed partial-update command.
 * @throws std::invalid_argument On a missing/empty `--slug`, an unknown flag, a
 *         malformed value (bad boolean or metadata entry), or a
 *         campaign/`--clear-campaign` conflict.
 */
app::UpdateLinkCommand parseUpdateArgs(const std::vector<std::string>& args);

/**
 * @brief Parse the flags of `link delete` into a DeleteLinkCommand.
 *
 * `--slug <SLUG>` is required and copied verbatim into
 * @ref app::DeleteLinkCommand::slug.
 *
 * @param args Flag tokens after the verb (argv[3..argc-1]).
 * @return app::DeleteLinkCommand The parsed soft-delete command.
 * @throws std::invalid_argument On a missing/empty `--slug` or an unknown flag.
 */
app::DeleteLinkCommand parseDeleteArgs(const std::vector<std::string>& args);

/**
 * @brief Parse the flags of `link enable` / `link disable` into a
 * SetLinkEnabledCommand.
 *
 * `--slug <SLUG>` is required. The target enabled state is supplied by the
 * dispatcher via @p enabled (true for `enable`, false for `disable`) rather than
 * a flag, matching the single-DTO/two-verb design from Task 01.0 subtask 01.
 *
 * @param args Flag tokens after the verb (argv[3..argc-1]).
 * @param enabled Target enabled state selected by the verb.
 * @return app::SetLinkEnabledCommand The parsed enable/disable command.
 * @throws std::invalid_argument On a missing/empty `--slug` or an unknown flag.
 */
app::SetLinkEnabledCommand parseSetEnabledArgs(
    const std::vector<std::string>& args,
    bool enabled);

/**
 * @brief Parse the flags of `link restore` into a RestoreLinkCommand.
 *
 * `--slug <SLUG>` is required and copied verbatim into
 * @ref app::RestoreLinkCommand::slug.
 *
 * @param args Flag tokens after the verb (argv[3..argc-1]).
 * @return app::RestoreLinkCommand The parsed restore command.
 * @throws std::invalid_argument On a missing/empty `--slug` or an unknown flag.
 */
app::RestoreLinkCommand parseRestoreArgs(const std::vector<std::string>& args);

/**
 * @brief Parse the flags of `link preview` into a GetLinkQuery.
 *
 * Preview reuses the same @ref app::GetLinkQuery lookup DTO as `link get` (per
 * Task 01.0 subtask 01), so it accepts exactly one of `--slug <SLUG>` or
 * `--id <ID>` with identical mutual-exclusion and non-emptiness rules.
 *
 * @param args Flag tokens after the verb (argv[3..argc-1]).
 * @return app::GetLinkQuery The parsed lookup query.
 * @throws std::invalid_argument If both or neither selector is given, on an
 *         unknown flag, or on an empty selector value.
 */
app::GetLinkQuery parsePreviewArgs(const std::vector<std::string>& args);

/**
 * @brief Parse the flags of `link stats` into a GetLinkStatsQuery.
 *
 * All four flags are required and copied verbatim: `--slug <SLUG>`,
 * `--from <EPOCH>`, `--to <EPOCH>`, `--bucket <hour|day|week>`. Timestamp and
 * bucket-granularity validation is deferred to `LinkCommandService`; the parser
 * only enforces presence and non-emptiness.
 *
 * @param args Flag tokens after the verb (argv[3..argc-1]).
 * @return app::GetLinkStatsQuery The parsed stats query.
 * @throws std::invalid_argument On a missing/empty required flag or an unknown
 *         flag.
 */
app::GetLinkStatsQuery parseStatsArgs(const std::vector<std::string>& args);

} // namespace cli
} // namespace url_shortener
