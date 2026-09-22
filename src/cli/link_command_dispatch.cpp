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

#include <url_shortener/cli/cli_parser.h>
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
}  // namespace

int DispatchLinkCommand(const LinkCliCommand& command,
                        const ServerConfig& config)
{
    // `config` will feed the LinkCommandService wiring added in subtask 02.
    (void)config;

    // TODO(0003 subtask 02): build LinkCommandService from `config`, invoke the
    // verb-appropriate method on it, and (Task 04.0) format the result. For now
    // emit a placeholder line so the dispatch path is reachable and
    // structurally exercised without starting the server (plan.md C4/C5).
    std::cout << "TODO: dispatch " << verb_token(command.verb) << '\n';
    return 0;
}

}  // namespace url_shortener::cli
