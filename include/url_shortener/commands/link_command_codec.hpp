#pragma once

#include <optional>
#include <string>
#include <string_view>

#include <url_shortener/app/app_error.hpp>
#include <url_shortener/commands/arg_spec.hpp>
#include <url_shortener/commands/command_descriptor.hpp>
#include <url_shortener/commands/error_codes.hpp>
#include <url_shortener/core/types.hpp>

namespace url_shortener
{
namespace app
{

// Forward declarations of the read-model views and query this codec maps to and
// from. The full definitions live in `app/link_command_service.hpp`, which
// transitively pulls in `core/utils.hpp` (Beast); that header is therefore
// included only by the .cpp, never here (01.0/13 include-hygiene test).
struct LinkView;
struct LinkStatsView;
struct GetLinkQuery;

}  // namespace app
}  // namespace url_shortener

namespace url_shortener::commands
{

/**
 * @brief Converts an application-layer error into its command-layer equivalent.
 *
 * The one place the command layer maps `app::AppError` onto a @ref CommandError
 * (milestone contract C1): every link command handler routes service failures
 * through this helper instead of mapping codes itself. The code is translated by
 * @ref from_app_error and the message is @p error `.detail` verbatim; because
 * `LinkCommandService` details are fixed strings (never request values), the
 * resulting message is safe to return to an untrusted caller (contract C6).
 *
 * @param error The application-layer failure to translate; its `detail` must be
 * fixed service text, not an argument value.
 * @return A @ref CommandError whose `code` is `from_app_error(error.code)` and
 * whose `message` is `error.detail` unchanged. A `none` input yields a `none`
 * code and is handled defensively (it is never produced on a real failure path).
 */
CommandError to_command_error(const app::AppError& error);

/**
 * @brief Stats detail token the REST layer promotes to an error code when a
 * timestamp fails to parse.
 *
 * Exposed so Task 04.0 can reproduce the legacy `codeForAppError` mapping from
 * an `invalid_field` detail; the command layer itself does not branch on it.
 */
inline constexpr std::string_view detail_invalid_timestamp = "invalid_timestamp";

/**
 * @brief Stats detail token the REST layer promotes to an error code when the
 * aggregation bucket is unrecognized.
 *
 * Exposed so Task 04.0 can reproduce the legacy `codeForAppError` mapping from
 * an `invalid_field` detail; the command layer itself does not branch on it.
 */
inline constexpr std::string_view detail_invalid_bucket = "invalid_bucket";

/**
 * @brief Wraps a `GetLink`/`CreateLink`/`UpdateLink`-style result as a
 * @ref CommandResult carrying the full-link JSON body.
 *
 * On success the result body is produced by the shared
 * `app::serializeLinkViewJson` (called, never re-implemented: contract C1/C3)
 * and the audit target is the returned view's own slug. On an expected failure
 * the error is translated by @ref to_command_error and the audit target is the
 * caller-addressed @p audit_target (the slug/id sent in the request, which may
 * not have passed service validation). An `ok()` result that carries no value is
 * a service programming error and maps to `internal` with a fixed message.
 *
 * @param result The service outcome to project; its value, when present,
 * supplies the success body and audit slug.
 * @param audit_target The slug or id the caller addressed, recorded as the audit
 * target on any non-success outcome; never an argument value other than the
 * addressed slug/id (contract C6).
 * @return On success, a @ref CommandResult with `result_json` set to the
 * serializer output verbatim and `audit_target` set to the view's slug; on a
 * service failure, a @ref CommandResult whose `error` is the translated code and
 * whose `audit_target` is @p audit_target; on an `ok()` result with no value, a
 * @ref CommandResult with `error.code == internal`.
 */
CommandResult link_view_result(const app::Result<app::LinkView>& result,
                               std::string_view audit_target);

/**
 * @brief Wraps a `PreviewLink` result as a @ref CommandResult carrying the
 * reduced preview JSON body.
 *
 * Identical to @ref link_view_result except that the success body is produced by
 * the shared `app::serializeLinkPreviewJson` (called, never re-implemented:
 * contract C1/C3).
 *
 * @param result The service outcome to project; its value, when present,
 * supplies the success body and audit slug.
 * @param audit_target The slug or id the caller addressed, recorded as the audit
 * target on any non-success outcome.
 * @return On success, a @ref CommandResult with `result_json` set to the preview
 * serializer output verbatim and `audit_target` set to the view's slug; on a
 * service failure, a @ref CommandResult whose `error` is the translated code and
 * whose `audit_target` is @p audit_target; on an `ok()` result with no value, a
 * @ref CommandResult with `error.code == internal`.
 */
CommandResult link_preview_result(const app::Result<app::LinkView>& result,
                                  std::string_view audit_target);

/**
 * @brief Wraps a `GetLinkStats` result as a @ref CommandResult carrying the
 * stats JSON body.
 *
 * Identical to @ref link_view_result except that the success body is produced by
 * the shared `app::serializeLinkStatsJson` (called, never re-implemented:
 * contract C1/C3) and the audit target is the stats view's slug.
 *
 * @param result The service outcome to project; its value, when present,
 * supplies the success body and audit slug.
 * @param audit_target The slug the caller addressed, recorded as the audit
 * target on any non-success outcome.
 * @return On success, a @ref CommandResult with `result_json` set to the stats
 * serializer output verbatim and `audit_target` set to the view's slug; on a
 * service failure, a @ref CommandResult whose `error` is the translated code and
 * whose `audit_target` is @p audit_target; on an `ok()` result with no value, a
 * @ref CommandResult with `error.code == internal`.
 */
CommandResult link_stats_result(const app::Result<app::LinkStatsView>& result,
                                std::string_view audit_target);

/**
 * @brief Decodes the `campaign` object argument into a `Link::Campaign`.
 *
 * Reads the six optional string members (`name`, `source`, `medium`, `term`,
 * `content`, `id`) declared in the argument's @ref ArgSpec `fields`. The
 * registry has already rejected unknown inner keys and non-string members, so
 * this cannot fail on shape; an absent member leaves the corresponding
 * `Link::Campaign` field `std::nullopt`, and an empty object `{}` yields a
 * present campaign with all fields unset. Callers invoke this only when
 * @ref patch_state_of reports @ref patch_state::value; the explicit-null case is
 * handled by the caller, not here. The @ref CommandError out-param is kept for
 * future fields that could fail decoding.
 *
 * @param args The validated view over the enclosing `args` object.
 * @param name The argument key of the campaign object (the fixed schema name,
 * never a request value).
 * @param[out] out When non-null and decoding succeeds, set to the decoded
 * campaign; left unchanged on failure.
 * @param[out] error When non-null and the call fails, set to `internal`; left
 * unchanged on success.
 * @return `true` when the campaign was decoded (including from an empty object);
 * `false` only on internal inconsistency (the named argument is not a present
 * object, which the registry should already have guaranteed).
 */
bool decode_campaign(const ArgsView& args,
                     std::string_view name,
                     std::optional<Link::Campaign>* out,
                     CommandError* error);

/**
 * @brief The three distinguishable PATCH states of a nullable argument.
 *
 * Partial-update commands must tell "field absent" (leave untouched) from
 * "field explicitly null" (clear) from "field present with a value" (replace).
 * @ref patch_state_of computes this for `expires_at` and `campaign`; the handler
 * then maps it onto the matching `std::optional<std::optional<...>>` layers of
 * `app::UpdateLinkCommand`.
 */
enum class patch_state
{
    ///< The argument key was absent from the `args` object; the handler leaves
    ///< the corresponding field untouched.
    absent,

    ///< The argument was present as an explicit JSON `null`; the handler clears
    ///< the corresponding field.
    null_value,

    ///< The argument was present with a concrete value; the handler decodes and
    ///< applies it (via @ref decode_campaign for `campaign`).
    value
};

/**
 * @brief Classifies a nullable argument into its PATCH @ref patch_state.
 *
 * @param args The validated view over the `args` object.
 * @param name The argument key to classify (the fixed schema name).
 * @return @ref patch_state::absent when the key is missing,
 * @ref patch_state::null_value when it is present as an explicit JSON `null`,
 * and @ref patch_state::value otherwise.
 */
patch_state patch_state_of(const ArgsView& args, std::string_view name);

/**
 * @brief Selects the single present `slug`/`id` argument into a `GetLinkQuery`.
 *
 * Shared by `link.get` and `link.preview`. The registry enforces "exactly one of
 * `slug`/`id`" through `ArgSpec::one_of_group = "slug_or_id"` (failing with
 * `invalid_field` and a `"<arg>: one_of_violation"` message) before the handler
 * runs, so this helper only has to pick whichever identifier is present:
 * `slug` sets `by = slug`, otherwise `id` sets `by = id`.
 *
 * @param args The validated view over the `args` object.
 * @param[out] out When non-null and an identifier is present, set to the chosen
 * lookup discriminator and value; left unchanged on failure.
 * @param[out] error When non-null and neither identifier is present, set to
 * `invalid_field` with message `"slug: one_of_violation"`; left unchanged on
 * success.
 * @return `true` when exactly one of `slug`/`id` was present and copied into
 * @p out; `false` only on the defensive path where neither is present (which the
 * registry's `one_of_group` check should already have rejected).
 */
bool decode_slug_or_id(const ArgsView& args,
                       app::GetLinkQuery* out,
                       CommandError* error);

}  // namespace url_shortener::commands
