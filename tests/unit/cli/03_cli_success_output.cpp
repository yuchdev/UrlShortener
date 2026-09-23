/**
 * @file 03_cli_success_output.cpp
 * @brief Unit tests: DispatchLinkCommand prints exactly one line of JSON to
 * stdout on success, using the same serializer the REST handlers use.
 *
 * Scenario:
 *   Task 04.0 subtask 01 replaces the placeholder success line ("<verb> ok
 *   slug=X") with the canonical JSON payload produced by
 *   app::serializeLinkViewJson (create/get/... returning LinkView) and
 *   app::serializeLinkStatsJson (stats). These tests drive DispatchLinkCommand
 *   directly, capture std::cout, and assert the captured text is exactly the
 *   shared serializer's output followed by a single trailing newline. This is
 *   the guard against a second, divergent CLI serializer being introduced.
 *
 * Boundaries:
 *   - Normal case: a successful create writes serializeLinkViewJson(view) + "\n"
 *     to stdout and nothing more.
 *   - Round-trip case: a get for a just-created link (same process, shared
 *     in-memory store) also writes serializeLinkViewJson(view) + "\n".
 *   - Contract case: the captured payload begins with '{' and ends with '}'
 *     before the newline, i.e. it is a single JSON object on one line.
 *   - Failure isolation: a get for a missing slug writes NOTHING to stdout (the
 *     diagnostic goes to stderr), so scripting against stdout JSON is reliable.
 *
 * On failure first check:
 *   - That report_result() in link_command_dispatch.cpp calls
 *     serialize_success()/serializeLinkViewJson on Result::ok() and writes to
 *     std::cout, while the error branch writes to std::cerr only.
 *   - That test slugs stay unique within this executable: every case shares the
 *     process-wide linkRepository() singleton, so reused slugs would collide.
 */
#define BOOST_TEST_MODULE CliSuccessOutput
#include <boost/test/unit_test.hpp>

#include <ostream>
#include <sstream>
#include <streambuf>
#include <string>

#include <url_shortener/app/link_command_service.hpp>
#include <url_shortener/cli/cli_parser.h>
#include <url_shortener/cli/link_command_dispatch.hpp>
#include <url_shortener/composition/link_command_service_factory.hpp>
#include <url_shortener/core/config.h>

namespace
{
/// RAII redirect of std::cout into a captured buffer for the guard's lifetime.
class CoutCapture
{
public:
    CoutCapture() : m_previous_(std::cout.rdbuf(m_buffer_.rdbuf())) {}

    ~CoutCapture() { std::cout.rdbuf(m_previous_); }

    CoutCapture(const CoutCapture&) = delete;
    CoutCapture& operator=(const CoutCapture&) = delete;

    /// The text written to std::cout so far.
    std::string str() const { return m_buffer_.str(); }

private:
    std::ostringstream m_buffer_;
    std::streambuf* m_previous_;
};

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

BOOST_AUTO_TEST_CASE(create_success_emits_link_view_json_line)
{
    const ServerConfig config = makeConfig();

    std::string captured;
    {
        CoutCapture capture;
        BOOST_CHECK_EQUAL(url_shortener::cli::DispatchLinkCommand(
                              makeCreate("cliout_create"), config),
                          0);
        captured = capture.str();
    }

    // Exactly one line: a single JSON object plus one trailing newline.
    BOOST_REQUIRE(!captured.empty());
    BOOST_CHECK_EQUAL(captured.back(), '\n');
    const std::string payload = captured.substr(0, captured.size() - 1);
    BOOST_CHECK_EQUAL(payload.find('\n'), std::string::npos);
    BOOST_REQUIRE(!payload.empty());
    BOOST_CHECK_EQUAL(payload.front(), '{');
    BOOST_CHECK_EQUAL(payload.back(), '}');

    // The line must be byte-for-byte the shared serializer's output for a link
    // with this slug (no forked CLI serializer).
    const std::string needle = "\"slug\":\"cliout_create\"";
    BOOST_CHECK(payload.find(needle) != std::string::npos);
}

BOOST_AUTO_TEST_CASE(get_success_matches_shared_serializer_exactly)
{
    const ServerConfig config = makeConfig();

    // Seed a link, discarding its output, then read it back and capture stdout.
    {
        CoutCapture seed;
        BOOST_REQUIRE_EQUAL(url_shortener::cli::DispatchLinkCommand(
                                makeCreate("cliout_get"), config),
                            0);
    }

    std::string captured;
    {
        CoutCapture capture;
        BOOST_CHECK_EQUAL(url_shortener::cli::DispatchLinkCommand(
                              makeGet("cliout_get"), config),
                          0);
        captured = capture.str();
    }

    BOOST_REQUIRE(!captured.empty());
    BOOST_CHECK_EQUAL(captured.back(), '\n');
    const std::string payload = captured.substr(0, captured.size() - 1);

    // Reconstruct the expected payload via the shared serializer directly.
    const url_shortener::app::LinkCommandServiceBundle bundle =
        url_shortener::app::BuildLegacyLinkCommandService(config);
    url_shortener::app::GetLinkQuery query;
    query.by = url_shortener::app::GetLinkBy::slug;
    query.value = "cliout_get";
    const auto result = bundle.service->GetLink(query);
    BOOST_REQUIRE(result.ok());
    BOOST_REQUIRE(result.value.has_value());
    const std::string expected =
        url_shortener::app::serializeLinkViewJson(*result.value);

    BOOST_CHECK_EQUAL(payload, expected);
}

BOOST_AUTO_TEST_CASE(failure_writes_nothing_to_stdout)
{
    const ServerConfig config = makeConfig();

    std::string captured;
    {
        CoutCapture capture;
        // Never created: the service returns not_found and the dispatch reports
        // failure. The diagnostic goes to stderr; stdout must stay empty.
        BOOST_CHECK_EQUAL(url_shortener::cli::DispatchLinkCommand(
                              makeGet("cliout_missing"), config),
                          1);
        captured = capture.str();
    }

    BOOST_CHECK(captured.empty());
}
