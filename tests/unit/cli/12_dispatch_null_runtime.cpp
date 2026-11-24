/**
 * @file 12_dispatch_null_runtime.cpp
 * @brief Unit tests: both one-shot dispatchers fail closed when the shared
 * command runtime cannot be acquired.
 *
 * Scenario:
 *   `acquire_command_runtime` returns `nullptr` only when no process-wide
 *   runtime is installed and the fallback `build_command_runtime` fails - a
 *   path no `ServerConfig` can force, because the fallback build wires only
 *   process-wide holders and never opens a resource. The production code
 *   exposes a single test-only seam
 *   (`detail::set_fallback_runtime_builder_for_testing`, mirroring the
 *   `set_start_flusher_hook_for_testing` pattern) that stands in for that
 *   fallback build; a builder returning `nullptr` drives the exact branch.
 *   These tests assert that under that condition `link <verb>` and `cmd`
 *   both return the shared table's `internal` exit code, write nothing to
 *   stdout, and emit only a fixed, value-free diagnostic to stderr.
 *
 * Boundaries:
 *   - link <verb>: `link get` -> "get failed: internal\n" on stderr, empty
 *     stdout, exit == ExitCodeForAppError(internal) (4).
 *   - cmd: `cmd system.info` -> "cmd failed: internal\n" on stderr, empty
 *     stdout, exit 4.
 *   - value-free: neither diagnostic contains the slug or the command name's
 *     arguments; the strings are fixed vocabulary.
 *
 * On failure first check:
 *   - That the fallback-builder seam is cleared between cases (the RAII guard),
 *     so a leaked override does not poison an unrelated test.
 *   - That no process-wide runtime was installed in this executable (none is),
 *     which would short-circuit the seam before it is consulted.
 */
#define BOOST_TEST_MODULE CliDispatchNullRuntime
#include <boost/test/unit_test.hpp>

#include <memory>
#include <sstream>
#include <streambuf>
#include <string>

#include <url_shortener/app/app_error.hpp>
#include <url_shortener/cli/app_exit_code.hpp>
#include <url_shortener/cli/cli_parser.hpp>
#include <url_shortener/cli/cmd_dispatch.hpp>
#include <url_shortener/cli/command_runtime_acquire.hpp>
#include <url_shortener/cli/link_command_dispatch.hpp>
#include <url_shortener/composition/command_runtime.hpp>
#include <url_shortener/core/config.hpp>

namespace
{
/// RAII redirect of a std::ostream (std::cout/std::cerr) into a captured
/// buffer for the guard's lifetime.
class StreamCapture
{
public:
    explicit StreamCapture(std::ostream& target)
        : m_target_(target), m_previous_(target.rdbuf(m_buffer_.rdbuf()))
    {
    }

    ~StreamCapture() { m_target_.rdbuf(m_previous_); }

    StreamCapture(const StreamCapture&) = delete;
    StreamCapture& operator=(const StreamCapture&) = delete;

    /// The text written to the captured stream so far.
    std::string str() const { return m_buffer_.str(); }

private:
    std::ostream& m_target_;
    std::ostringstream m_buffer_;
    std::streambuf* m_previous_;
};

/// RAII installer of a fallback-runtime builder that always yields a null
/// runtime, so `acquire_command_runtime` returns `nullptr`. Clears the seam on
/// destruction so it never leaks into another test.
class NullFallbackGuard
{
public:
    NullFallbackGuard()
    {
        namespace cli = url_shortener::cli;
        cli::detail::set_fallback_runtime_builder_for_testing(
            [](const ServerConfig&)
            {
                return std::shared_ptr<
                    url_shortener::composition::CommandRuntime>();
            });
    }

    ~NullFallbackGuard()
    {
        url_shortener::cli::detail::set_fallback_runtime_builder_for_testing(
            nullptr);
    }

    NullFallbackGuard(const NullFallbackGuard&) = delete;
    NullFallbackGuard& operator=(const NullFallbackGuard&) = delete;
};

/// Exit code the shared table assigns to `internal` (expected: 4).
int internal_exit_code()
{
    return url_shortener::cli::ExitCodeForAppError(
        url_shortener::app::AppErrorCode::internal);
}

/// Build a `link get` command addressing a slug by value.
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

BOOST_AUTO_TEST_CASE(internal_exit_code_is_four)
{
    BOOST_CHECK_EQUAL(internal_exit_code(), 4);
}

BOOST_AUTO_TEST_CASE(link_verb_fails_closed_on_null_runtime)
{
    const ServerConfig config;
    const NullFallbackGuard guard;

    std::string out;
    std::string err;
    int rc = 0;
    {
        StreamCapture cout_capture(std::cout);
        StreamCapture cerr_capture(std::cerr);
        rc = url_shortener::cli::DispatchLinkCommand(makeGet("slug_x"), config);
        out = cout_capture.str();
        err = cerr_capture.str();
    }

    BOOST_CHECK_EQUAL(rc, internal_exit_code());
    BOOST_CHECK(out.empty());
    BOOST_CHECK_EQUAL(err, "get failed: internal\n");
    // Value-free: the addressed slug never appears in the diagnostic.
    BOOST_CHECK(err.find("slug_x") == std::string::npos);
}

BOOST_AUTO_TEST_CASE(cmd_fails_closed_on_null_runtime)
{
    const ServerConfig config;
    const NullFallbackGuard guard;

    CmdCliCommand command;
    command.command = "system.info";

    std::istringstream in;
    std::ostringstream out;
    std::ostringstream err;
    const int rc = url_shortener::cli::DispatchCmdCommand(command, config, in,
                                                          out, err);

    BOOST_CHECK_EQUAL(rc, internal_exit_code());
    BOOST_CHECK(out.str().empty());
    BOOST_CHECK_EQUAL(err.str(), "cmd failed: internal\n");
}
