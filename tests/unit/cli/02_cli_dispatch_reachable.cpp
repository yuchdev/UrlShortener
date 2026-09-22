/**
 * @file 02_cli_dispatch_reachable.cpp
 * @brief Unit tests: DispatchLinkCommand wires a real LinkCommandService via the
 * shared factory and reaches the verb-appropriate method, without starting the
 * network server.
 *
 * Scenario:
 *   Subtask 01 added the main() dispatch branch and a placeholder body; subtask
 *   02 replaces that body with real logic that builds a LinkCommandServiceBundle
 *   via app::BuildLegacyLinkCommandService(config) - the same factory the REST
 *   handlers use - and invokes the matching service method for the parsed verb.
 *   These tests exercise that entry point directly to prove the wiring reaches a
 *   real service call and inspects its Result.
 *
 * Boundaries:
 *   - Normal case: a valid create command succeeds (exit code 0).
 *   - Wiring proof: a create followed by a get in the SAME process observes the
 *     just-created link, because BuildLegacyLinkCommandService wraps the
 *     process-wide in-memory linkRepository() singleton (a create and a get in
 *     two SEPARATE CLI processes would NOT share state - see subtask summary).
 *   - Failure case: a get for a slug that was never created returns not_found
 *     from the real service (exit code 1), proving the actual service call ran.
 *   - Alternative-variant case: a stats command extracts a different payload
 *     alternative (GetLinkStatsQuery) and reaches GetLinkStats without throwing.
 *
 * On failure first check:
 *   - That link_command_dispatch.cpp still builds the bundle via
 *     app::BuildLegacyLinkCommandService and dispatches on command.verb.
 *   - That the C4/C5 invariant holds: link_command_dispatch.cpp must not
 *     #include anything under url_shortener/http/ nor construct an io_context /
 *     HttpServer / touch UriMapSingleton. This is an architecture invariant
 *     enforced by review + grep, not a runtime assertion.
 *   - That test slugs remain unique within this executable: every case shares
 *     the process-wide linkRepository() singleton, so reused slugs would collide.
 */
#define BOOST_TEST_MODULE CliDispatchReachable
#include <boost/test/unit_test.hpp>

#include <string>

#include <url_shortener/app/link_command_service.hpp>
#include <url_shortener/cli/cli_parser.h>
#include <url_shortener/cli/link_command_dispatch.hpp>
#include <url_shortener/core/config.h>

namespace
{
/// Minimal ServerConfig sufficient for LinkCommandService construction and the
/// short-URL rendering the create path performs.
ServerConfig makeConfig()
{
    ServerConfig config;
    config.shortener_base_domain = "http://sho.rt";
    config.shortener_generated_slug_length = 7;
    config.shortener_default_redirect_type = "temporary";
    config.shortener_allow_private_targets = false;
    return config;
}

/// Build a `link create` command for an explicit slug and target URL.
LinkCliCommand makeCreate(const std::string& slug)
{
    url_shortener::app::CreateLinkCommand payload;
    payload.target_url = "https://example.com/target";
    payload.slug = slug;

    LinkCliCommand command;
    command.verb = LinkCliVerb::create;
    command.payload = payload;
    return command;
}

/// Build a `link get` command that looks a link up by slug.
LinkCliCommand makeGet(const std::string& slug)
{
    url_shortener::app::GetLinkQuery payload;
    payload.by = url_shortener::app::GetLinkBy::slug;
    payload.value = slug;

    LinkCliCommand command;
    command.verb = LinkCliVerb::get;
    command.payload = payload;
    return command;
}
}  // namespace

BOOST_AUTO_TEST_CASE(dispatch_valid_create_returns_success)
{
    const ServerConfig config = makeConfig();
    const LinkCliCommand command = makeCreate("cliwirecreate");

    BOOST_CHECK_EQUAL(
        url_shortener::cli::DispatchLinkCommand(command, config), 0);
}

BOOST_AUTO_TEST_CASE(dispatch_create_then_get_roundtrip_same_process)
{
    const ServerConfig config = makeConfig();

    // A create followed by a get in the same process must observe the same
    // link, proving DispatchLinkCommand reaches the shared in-memory store.
    BOOST_CHECK_EQUAL(
        url_shortener::cli::DispatchLinkCommand(makeCreate("cliwireround"),
                                                config),
        0);
    BOOST_CHECK_EQUAL(
        url_shortener::cli::DispatchLinkCommand(makeGet("cliwireround"),
                                                config),
        0);
}

BOOST_AUTO_TEST_CASE(dispatch_get_missing_slug_returns_failure)
{
    const ServerConfig config = makeConfig();
    const LinkCliCommand command = makeGet("cliwireabsent");

    // The link was never created, so the real service returns not_found and the
    // dispatch maps that to a non-zero exit code.
    BOOST_CHECK_EQUAL(
        url_shortener::cli::DispatchLinkCommand(command, config), 1);
}

BOOST_AUTO_TEST_CASE(dispatch_stats_reaches_service_without_throwing)
{
    const ServerConfig config = makeConfig();

    url_shortener::app::GetLinkStatsQuery payload;
    payload.slug = "cliwirestats";
    payload.from = "2020-01-01T00:00:00Z";
    payload.to = "2020-01-02T00:00:00Z";
    payload.bucket = "day";

    LinkCliCommand command;
    command.verb = LinkCliVerb::stats;
    command.payload = payload;

    // Exercises a different payload alternative; must reach GetLinkStats and
    // return cleanly rather than throwing bad_variant_access.
    BOOST_CHECK_NO_THROW(
        url_shortener::cli::DispatchLinkCommand(command, config));
}
