/**
 * @file link_command_args.hpp
 * @brief argv-to-DTO mapping for the `link create`, `link get`, and
 * `link stats` subcommands.
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
