/**
 * @file 02_cli_dispatch_reachable.cpp
 * @brief Unit tests: DispatchLinkCommand is reachable and returns cleanly for
 * `link <verb>` invocations without starting the server.
 *
 * Scenario:
 *   The main() dispatch branch (subtask 01) calls
 *   url_shortener::cli::DispatchLinkCommand once a `link <verb>` positional is
 *   recognized. These tests exercise that entry point directly to prove it is
 *   reachable, verb-dispatching, and side-effect free with respect to the
 *   network server.
 *
 * Boundaries:
 *   - Normal case: a create command returns exit code 0.
 *   - Edge case: a stats command (different variant alternative) does not throw.
 *   - The placeholder body only writes to stdout and returns; the real
 *     LinkCommandService wiring lands in subtask 02.
 *
 * On failure first check:
 *   - That link_command_dispatch.cpp still compiles and links into
 *     url_shortener_common (see sources.cmake).
 *   - That the C4/C5 invariant holds: link_command_dispatch.cpp must not
 *     #include anything under url_shortener/http/ nor construct an io_context /
 *     HttpServer / touch UriMapSingleton. This is an architecture invariant
 *     enforced by review + grep, not a runtime assertion.
 */
#define BOOST_TEST_MODULE CliDispatchReachable
#include <boost/test/unit_test.hpp>

#include <url_shortener/cli/cli_parser.h>
#include <url_shortener/cli/link_command_dispatch.hpp>

namespace
{
/// Build a LinkCliCommand carrying only the verb; the payload variant is left
/// default-constructed, which is sufficient for the subtask-01 placeholder.
LinkCliCommand make_command(LinkCliVerb verb)
{
    LinkCliCommand command;
    command.verb = verb;
    return command;
}
}  // namespace

BOOST_AUTO_TEST_CASE(dispatch_create_returns_success)
{
    ServerConfig config;
    const LinkCliCommand command = make_command(LinkCliVerb::create);

    BOOST_CHECK_EQUAL(
        url_shortener::cli::DispatchLinkCommand(command, config), 0);
}

BOOST_AUTO_TEST_CASE(dispatch_stats_does_not_throw)
{
    ServerConfig config;
    const LinkCliCommand command = make_command(LinkCliVerb::stats);

    BOOST_CHECK_NO_THROW(
        url_shortener::cli::DispatchLinkCommand(command, config));
}
