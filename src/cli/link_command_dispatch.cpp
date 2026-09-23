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
/// Human-readable token for a link verb, used only for placeholder output.
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
 * @brief Emit a minimal, placeholder outcome line for a service @c Result.
 *
 * NOTE (Task 04.0): the final success-JSON payload and error-envelope format,
 * plus the full exit-code mapping, are owned by Task 04.0. This helper is a
 * deliberate placeholder: it exists only to prove the real service call ran
 * and that its @c Result was inspected for success/failure. Both @c LinkView
 * and @c LinkStatsView expose a @c slug member, so a single template serves
 * every verb.
 *
 * @tparam View The value type carried by the service @c Result.
 * @param verb   Human-readable verb token for the outcome line.
 * @param result Result returned by the invoked @c LinkCommandService method.
 * @return 0 when the command succeeded, 1 otherwise.
 */
template <typename View>
int report_result(std::string_view verb, const app::Result<View>& result)
{
    if (result.ok()) {
        if (result.value.has_value()) {
            std::cout << verb << " ok slug=" << result.value->slug << '\n';
        }
        // An ok result without a value carries no payload to print here; the
        // command still succeeded, so do not emit a spurious "failed:" line.
        return 0;
    }
    std::cerr << verb << " failed: " << result.error.detail << '\n';
    return 1;
}
}  // namespace

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
        return report_result(
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
