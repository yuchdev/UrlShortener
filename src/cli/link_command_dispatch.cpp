/**
 * @file link_command_dispatch.cpp
 * @brief One-shot CLI dispatch for `link <verb>` commands.
 *
 * IMPORTANT (plan.md C4/C5): this translation unit must never start the HTTP
 * server, construct a Boost.Asio @c io_context, or touch @c UriMapSingleton /
 * `uri.txt`. It therefore includes nothing under `url_shortener/http/` and no
 * `boost/asio` headers. Keep it that way; the constraint is an architecture
 * invariant verified by grep during review.
 */
#include <url_shortener/cli/link_command_dispatch.hpp>

#include <iostream>
#include <string_view>
#include <variant>

#include <url_shortener/app/app_error.hpp>
#include <url_shortener/app/link_command_service.hpp>
#include <url_shortener/cli/cli_parser.h>
#include <url_shortener/composition/link_command_service_factory.hpp>
#include <url_shortener/core/config.h>

namespace url_shortener::cli
{
namespace
{
/// Human-readable token for a link verb, used only for error diagnostics.
std::string_view verb_token(LinkCliVerb verb)
{
    switch (verb) {
    case LinkCliVerb::create:
        return "create";
    case LinkCliVerb::get:
        return "get";
    case LinkCliVerb::update:
        return "update";
    case LinkCliVerb::del:
        return "delete";
    case LinkCliVerb::enable:
        return "enable";
    case LinkCliVerb::disable:
        return "disable";
    case LinkCliVerb::restore:
        return "restore";
    case LinkCliVerb::preview:
        return "preview";
    case LinkCliVerb::stats:
        return "stats";
    }
    return "unknown";
}

/**
 * @brief Serialize a successful command payload to its canonical JSON form.
 *
 * These thin overloads bind each view type to the exact serializer the REST
 * handlers use (see src/http/handlers/link_handlers.cpp), so the CLI and REST
 * success payloads stay byte-for-byte identical and no second serializer is
 * ever introduced for the CLI path (Task 04.0 subtask 01 constraint).
 *
 * @param view Fully populated view returned by a @c LinkCommandService method.
 * @return The canonical JSON object for @p view.
 * @{
 */
std::string serialize_success(const app::LinkView& view)
{
    return app::serializeLinkViewJson(view);
}

std::string serialize_success(const app::LinkStatsView& view)
{
    return app::serializeLinkStatsJson(view);
}
/// @}

/**
 * @brief Write a failure diagnostic to stderr and map the error to an exit code.
 *
 * Shared failure tail for every report_* helper: writes a human-readable
 * diagnostic (verb + the error detail) to stderr only, leaves stdout empty, and
 * returns the exit code from @ref ExitCodeForAppError so callers can branch on
 * `$?` alone.
 *
 * @param verb  Human-readable verb token for the diagnostic line.
 * @param error The error carried by the failed service @c Result.
 * @return The mapped non-zero exit code for @p error.
 */
int report_failure(std::string_view verb, const app::AppError& error)
{
    std::cerr << verb << " failed: " << error.detail << '\n';
    return ExitCodeForAppError(error.code);
}

/**
 * @brief Emit the outcome of a service @c Result following the CLI contract.
 *
 * On success, writes exactly one line to stdout: the shared REST serializer's
 * JSON for the carried view plus a trailing newline, and nothing else, so that
 * stdout carries only the success payload and scripting against it is reliable
 * (Task 04.0). On failure, delegates to @ref report_failure.
 *
 * @tparam View The value type carried by the service @c Result.
 * @param verb   Human-readable verb token for the failure diagnostic.
 * @param result Result returned by the invoked @c LinkCommandService method.
 * @return 0 when the command succeeded, otherwise the mapped non-zero exit code.
 */
template <typename View>
int report_result(std::string_view verb, const app::Result<View>& result)
{
    if (result.ok()) {
        if (result.value.has_value()) {
            std::cout << serialize_success(*result.value) << '\n';
        }
        // An ok result without a value carries no payload to print here; the
        // command still succeeded, so do not emit a spurious failure line.
        return 0;
    }
    return report_failure(verb, result.error);
}

/**
 * @brief Emit a `link preview` outcome using the reduced preview projection.
 *
 * `preview` returns the same @ref app::LinkView type as `get`, but its purpose
 * is an operator safety check (is the link enabled, expired, or soft-deleted?),
 * so it serializes with @ref app::serializeLinkPreviewJson - the exact reduced
 * shape the REST `GET /api/v1/links/{slug}/preview` endpoint returns - instead
 * of the full @ref app::serializeLinkViewJson used by the other verbs. Success
 * and failure output otherwise follow the same contract as @ref report_result.
 *
 * @param verb   Human-readable verb token for the failure diagnostic.
 * @param result Result returned by @ref app::LinkCommandService::PreviewLink.
 * @return 0 when the command succeeded, otherwise the mapped non-zero exit code.
 */
int report_preview_result(std::string_view verb,
                          const app::Result<app::LinkView>& result)
{
    if (result.ok()) {
        if (result.value.has_value()) {
            std::cout << app::serializeLinkPreviewJson(*result.value) << '\n';
        }
        return 0;
    }
    return report_failure(verb, result.error);
}
}  // namespace

int ExitCodeForAppError(app::AppErrorCode code)
{
    // Mirrors the switch shape of statusForAppError/codeForAppError in
    // src/http/handlers/link_handlers.cpp, but yields a small, stable set of
    // process exit codes instead of an HTTP status. Grouping rationale:
    //   1  not_found        - the addressed link does not exist.
    //   2  bad input        - the request itself is malformed or disallowed:
    //                         invalid_url/invalid_slug/invalid_field, and
    //                         reserved_slug (the caller chose a disallowed slug,
    //                         a client-side input problem, distinct from a
    //                         collision with an existing link).
    //   3  conflict         - slug_conflict: the slug is already taken.
    //   4  server/internal  - storage_failure/internal: not the caller's fault.
    // The switch is exhaustive over AppErrorCode with no default so that adding
    // an enumerator is a compile-time prompt to classify it here.
    switch (code) {
    case app::AppErrorCode::none:
        return 0;
    case app::AppErrorCode::not_found:
        return 1;
    case app::AppErrorCode::invalid_url:
    case app::AppErrorCode::invalid_slug:
    case app::AppErrorCode::invalid_field:
    case app::AppErrorCode::reserved_slug:
        return 2;
    case app::AppErrorCode::slug_conflict:
        return 3;
    case app::AppErrorCode::storage_failure:
    case app::AppErrorCode::internal:
        return 4;
    }
    // UNREACHABLE: AppErrorCode is a closed enum exhaustively handled above.
    // Any unmapped value is treated as an internal failure.
    return 4;
}

int DispatchLinkCommand(const LinkCliCommand& command,
                        const ServerConfig& config)
{
    // Build the exact LinkCommandService the REST handlers use
    // (app::BuildLegacyLinkCommandService, see src/http/handlers/
    // link_handlers.cpp::makeCommandService). The bundle owns the legacy
    // in-memory store and stats reader; it must outlive the single service
    // call below, so it is kept as a local for the whole invocation. Per
    // plan.md and this subtask's constraint, the CLI reuses this factory
    // verbatim and does NOT introduce a second storage path.
    const app::LinkCommandServiceBundle bundle =
        app::BuildLegacyLinkCommandService(config);
    app::LinkCommandService& service = *bundle.service;

    const std::string_view verb = verb_token(command.verb);

    // Dispatch to the verb-appropriate LinkCommandService method, extracting the
    // matching DTO alternative from the discriminated payload. `enable` and
    // `disable` share SetLinkEnabled; `get` and `preview` share GetLinkQuery.
    switch (command.verb) {
    case LinkCliVerb::create:
        return report_result(
            verb,
            service.CreateLink(
                std::get<app::CreateLinkCommand>(command.payload)));
    case LinkCliVerb::get:
        return report_result(
            verb,
            service.GetLink(std::get<app::GetLinkQuery>(command.payload)));
    case LinkCliVerb::update:
        return report_result(
            verb,
            service.UpdateLink(
                std::get<app::UpdateLinkCommand>(command.payload)));
    case LinkCliVerb::del:
        return report_result(
            verb,
            service.DeleteLink(
                std::get<app::DeleteLinkCommand>(command.payload)));
    case LinkCliVerb::enable:
    case LinkCliVerb::disable:
        return report_result(
            verb,
            service.SetLinkEnabled(
                std::get<app::SetLinkEnabledCommand>(command.payload)));
    case LinkCliVerb::restore:
        return report_result(
            verb,
            service.RestoreLink(
                std::get<app::RestoreLinkCommand>(command.payload)));
    case LinkCliVerb::preview:
        // preview reuses the reduced REST preview shape, not the full LinkView.
        return report_preview_result(
            verb,
            service.PreviewLink(std::get<app::GetLinkQuery>(command.payload)));
    case LinkCliVerb::stats:
        return report_result(
            verb,
            service.GetLinkStats(
                std::get<app::GetLinkStatsQuery>(command.payload)));
    }

    // UNREACHABLE: LinkCliVerb is a closed enum exhaustively handled above; this
    // return exists only to satisfy the compiler's non-void return check.
    return 1;
}

}  // namespace url_shortener::cli
