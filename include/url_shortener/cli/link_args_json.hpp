/**
 * @file link_args_json.hpp
 * @brief Pure DTO -> `args` JSON converters for the `link <verb>` sugar.
 *
 * The `link <verb>` commands keep their hand-written flag grammar
 * (`link_command_args.cpp`) and keep producing the shared `app::`
 * command/query DTOs. This header adds the second half of that pipeline:
 * converters that turn each DTO into the flat `args` object text a registered
 * `link.*` command expects, built exclusively through
 * `url_shortener::commands::args_builder` (milestone contract C9: no JSON
 * library, one escaping path). The converters carry no validation beyond
 * choosing JSON types (contract C1); every domain and shape rule stays in the
 * registry's `validate_args` and in `LinkCommandService`.
 *
 * The declarations below forward-declare the `app::` DTOs and `LinkCliVerb`
 * rather than include their defining headers, which transitively pull in
 * Boost.Beast via `core/utils.hpp`. This keeps this header Beast-free; the
 * converter implementations include the full definitions (and call the
 * Beast-including `redirectTypeToString`) in the `.cpp` only.
 */
#pragma once

#include <string>
#include <string_view>

/// Link-management subcommand; the full definition (enumerators) lives in
/// `url_shortener/cli/cli_parser.hpp`. Forward-declared here so this header
/// stays free of that header's Beast-including transitive includes.
enum class LinkCliVerb;

namespace url_shortener
{
namespace app
{

/// Create command DTO; defined in `app/link_command_service.hpp`.
struct CreateLinkCommand;

/// Get/preview lookup query DTO; defined in `app/link_command_service.hpp`.
struct GetLinkQuery;

/// Partial-update command DTO; defined in `app/link_command_service.hpp`.
struct UpdateLinkCommand;

/// Stats query DTO; defined in `app/link_command_service.hpp`.
struct GetLinkStatsQuery;

}  // namespace app
}  // namespace url_shortener

namespace url_shortener::cli
{

/**
 * @brief Converts a parsed `link create` DTO into the `link.create` `args`
 * object text.
 *
 * Emits `target_url` (always, the one required argument) and only the optional
 * arguments the DTO actually carries: `slug`, `redirect_type`
 * (`"temporary"`/`"permanent"` via the service's `redirectTypeToString`),
 * `expires_at`, and `enabled` when their optionals are engaged; `tags` and
 * `metadata` only when non-empty (an empty list/map is omitted, matching the
 * legacy "flag not given" shape); and `campaign` as a nested object carrying
 * only its present UTM members when `command.campaign` is engaged. The result
 * passes `link.create`'s `validate_args` for any DTO the flag parser can
 * produce.
 *
 * @param command The create DTO produced by `parseCreateArgs`.
 * @return A single well-formed JSON object string (`"{}"`-safe) assembled only
 * with `commands::args_builder`, so every value is JSON-escaped.
 */
std::string create_args_json(const app::CreateLinkCommand& command);

/**
 * @brief Converts a `link get`/`link preview` lookup DTO into the matching
 * `args` object text.
 *
 * Emits exactly one member: `{"slug":...}` when `query.by == GetLinkBy::slug`,
 * otherwise `{"id":...}`. The single-selector shape satisfies the `slug_or_id`
 * one-of group of both the `link.get` and `link.preview` descriptors, so this
 * one converter backs both verbs.
 *
 * @param query The lookup DTO produced by `parseGetArgs`/`parsePreviewArgs`
 * (exactly one of slug/id is set by construction).
 * @return A single JSON object string with one escaped member.
 */
std::string get_args_json(const app::GetLinkQuery& query);

/**
 * @brief Converts a parsed `link update` DTO into the `link.update` `args`
 * object text, preserving PATCH tri-state semantics.
 *
 * Emits `slug` always (the required selector) and each optional field
 * according to its DTO state: `enabled` when engaged; `tags`/`metadata`
 * whenever present (an empty list/map is emitted as `[]`/`{}`, a full-clear);
 * and the double-optional `expires_at`/`campaign` as absent -> omitted,
 * inner-empty -> JSON `null` (clear), value -> the string / nested campaign
 * object. The nullable arguments rely on `args_builder::add_null`, which the
 * `link.update` ArgSpecs accept (`nullable = true`). The output passes
 * `link.update`'s `validate_args`.
 *
 * @param command The update DTO produced by `parseUpdateArgs`.
 * @return A single JSON object string assembled only with
 * `commands::args_builder`.
 */
std::string update_args_json(const app::UpdateLinkCommand& command);

/**
 * @brief Builds the single-`slug` `args` object shared by the lifecycle verbs.
 *
 * Backs `link delete`, `link enable`, `link disable`, and `link restore`, each
 * of whose descriptors declares exactly one required `slug` argument. The verb
 * (and the enable/disable boolean) is carried by the command name and service
 * method, not by `args`, so one converter serves all four.
 *
 * @param slug The addressed slug, copied verbatim from the lifecycle DTO.
 * @return `{"slug":...}` with @p slug JSON-escaped.
 */
std::string slug_args_json(const std::string& slug);

/**
 * @brief Converts a parsed `link stats` DTO into the `link.stats` `args`
 * object text.
 *
 * Emits all four string arguments the CLI always supplies: `slug`, `from`,
 * `to`, and `bucket`. `from`/`to` are emitted as JSON strings (the `link.stats`
 * ArgSpecs are free strings; timestamp and bucket validity stay in the stats
 * reader, preserving the REST `invalid_timestamp`/`invalid_bucket` details).
 * The CLI requires all four flags, so the range-mode shape is always produced.
 *
 * @param query The stats DTO produced by `parseStatsArgs`.
 * @return A single JSON object string with four escaped members.
 */
std::string stats_args_json(const app::GetLinkStatsQuery& query);

/**
 * @brief Maps a `link <verb>` enumerator to its registered command name.
 *
 * The single source of truth linking the CLI sugar to the registry:
 * `create -> "link.create"`, `get -> "link.get"`, `update -> "link.update"`,
 * `del -> "link.delete"`, `enable -> "link.enable"`,
 * `disable -> "link.disable"`, `restore -> "link.restore"`,
 * `preview -> "link.preview"`, `stats -> "link.stats"`. Implemented as an
 * exhaustive `switch` with no `default`, so adding a `LinkCliVerb` enumerator
 * is a compile-time prompt to map it here.
 *
 * @param verb The recognized link verb.
 * @return The dotted registry command name, as a view over a static literal
 * (valid for the whole program).
 */
std::string_view command_name_for(LinkCliVerb verb);

}  // namespace url_shortener::cli
