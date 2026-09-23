/**
 * @file 04_cli_exit_codes.cpp
 * @brief Unit tests: DispatchLinkCommand maps each AppErrorCode failure to the
 * exit code from ExitCodeForAppError, and never writes to stdout on error.
 *
 * Scenario:
 *   Task 04.0 subtask 02 replaces the placeholder unconditional `return 1` in
 *   the dispatch failure path with the ExitCodeForAppError mapping. These tests
 *   drive DispatchLinkCommand through representative failures of each mapped
 *   class, assert the process exit code matches the mapping, and assert stdout
 *   stays empty (the spec's explicit success criterion: a non-empty stdout must
 *   only ever mean success).
 *
 * Boundaries:
 *   - not_found: `get` for a slug that was never created -> exit 1.
 *   - bad input: `create` with a non-absolute target URL -> invalid_url -> 2.
 *   - conflict:  `create` reusing an already-taken slug -> slug_conflict -> 3.
 *   - Pure mapping: ExitCodeForAppError over every AppErrorCode enumerator
 *     returns the documented code (none->0, not_found->1, invalid_*->2,
 *     slug_conflict->3, storage_failure/internal->4).
 *   - stdout isolation: every error case above captures std::cout and asserts
 *     it is empty; the diagnostic goes to std::cerr only.
 *
 * On failure first check:
 *   - That report_result() in link_command_dispatch.cpp routes the error path
 *     through ExitCodeForAppError(result.error.code) rather than a hard-coded 1.
 *   - That test slugs stay unique within this executable: every case shares the
 *     process-wide in-memory store singleton, so reused slugs would collide.
 */
#define BOOST_TEST_MODULE CliExitCodes
#include <boost/test/unit_test.hpp>

#include <ostream>
#include <sstream>
#include <streambuf>
#include <string>

#include <url_shortener/app/app_error.hpp>
#include <url_shortener/app/link_command_service.hpp>
#include <url_shortener/cli/cli_parser.h>
#include <url_shortener/cli/link_command_dispatch.hpp>
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
/// short-URL rendering the create path performs. Private targets are rejected
/// so the SSRF/URL guard is exercised by the bad-input case.
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
LinkCliCommand makeCreate(const std::string& slug, const std::string& target)
{
    url_shortener::app::CreateLinkCommand payload;
    payload.target_url = target;
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

BOOST_AUTO_TEST_CASE(not_found_maps_to_exit_1_with_empty_stdout)
{
    const ServerConfig config = makeConfig();

    std::string captured;
    {
        CoutCapture capture;
        // Slug was never created: GetLink returns not_found.
        BOOST_CHECK_EQUAL(url_shortener::cli::DispatchLinkCommand(
                              makeGet("clixc_missing"), config),
                          1);
        captured = capture.str();
    }
    BOOST_CHECK(captured.empty());
}

BOOST_AUTO_TEST_CASE(invalid_url_maps_to_exit_2_with_empty_stdout)
{
    const ServerConfig config = makeConfig();

    std::string captured;
    {
        CoutCapture capture;
        // Not an absolute http/https URL: CreateLink returns invalid_url.
        BOOST_CHECK_EQUAL(
            url_shortener::cli::DispatchLinkCommand(
                makeCreate("clixc_badurl", "not-an-absolute-url"), config),
            2);
        captured = capture.str();
    }
    BOOST_CHECK(captured.empty());
}

BOOST_AUTO_TEST_CASE(slug_conflict_maps_to_exit_3_with_empty_stdout)
{
    const ServerConfig config = makeConfig();

    // Seed the slug once (success, output discarded).
    {
        CoutCapture seed;
        BOOST_REQUIRE_EQUAL(
            url_shortener::cli::DispatchLinkCommand(
                makeCreate("clixc_dupe", "https://example.com/a"), config),
            0);
    }

    std::string captured;
    {
        CoutCapture capture;
        // Reusing the slug: CreateLink returns slug_conflict.
        BOOST_CHECK_EQUAL(
            url_shortener::cli::DispatchLinkCommand(
                makeCreate("clixc_dupe", "https://example.com/b"), config),
            3);
        captured = capture.str();
    }
    BOOST_CHECK(captured.empty());
}

BOOST_AUTO_TEST_CASE(exit_code_mapping_covers_every_enumerator)
{
    using url_shortener::app::AppErrorCode;
    using url_shortener::cli::ExitCodeForAppError;

    BOOST_CHECK_EQUAL(ExitCodeForAppError(AppErrorCode::none), 0);
    BOOST_CHECK_EQUAL(ExitCodeForAppError(AppErrorCode::not_found), 1);
    BOOST_CHECK_EQUAL(ExitCodeForAppError(AppErrorCode::invalid_url), 2);
    BOOST_CHECK_EQUAL(ExitCodeForAppError(AppErrorCode::invalid_slug), 2);
    BOOST_CHECK_EQUAL(ExitCodeForAppError(AppErrorCode::invalid_field), 2);
    BOOST_CHECK_EQUAL(ExitCodeForAppError(AppErrorCode::reserved_slug), 2);
    BOOST_CHECK_EQUAL(ExitCodeForAppError(AppErrorCode::slug_conflict), 3);
    BOOST_CHECK_EQUAL(ExitCodeForAppError(AppErrorCode::storage_failure), 4);
    BOOST_CHECK_EQUAL(ExitCodeForAppError(AppErrorCode::internal), 4);
}
