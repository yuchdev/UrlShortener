/**
 * @file 10_link_dispatch_via_registry.cpp
 * @brief Unit tests: `DispatchLinkCommand` routes every `link <verb>` through
 * the shared command registry while keeping stdout, stderr and exit codes
 * byte-identical to the legacy service-switch dispatch (Task 04.0 subtask 15).
 *
 * Scenario:
 *   Subtask 15 re-points the nine `link <verb>` commands from direct
 *   `LinkCommandService` calls to one `commands::CommandRegistry::execute`, so
 *   CLI sugar, `cmd`, REST and the TUI run the same descriptor/handler. These
 *   tests drive `DispatchLinkCommand` directly, capture std::cout AND std::cerr,
 *   and assert the contract C4 observables are unchanged: one serializer line on
 *   stdout for success, a `"<verb> failed: <detail>"` line on stderr for
 *   failure, empty stdout on every failure, and the shared-table exit code. They
 *   also pin decision log D14: a successful mutating verb must leave stderr
 *   completely empty (no `command_audit` line leaks from the runtime's audit
 *   sink), and prove the CLI sugar and `cmd` produce the same `result` for the
 *   same link.
 *
 * Boundaries:
 *   - Success per verb equals the shared REST serializer's output; mutating
 *     verbs (create/update/delete/enable/disable/restore) leave stderr empty.
 *   - Failure classes: not_found -> 1, invalid_url/invalid_slug/reserved_slug/
 *     invalid stats input -> 2, slug_conflict -> 3, storage_failure -> 4; stdout
 *     stays empty on all of them.
 *   - `ExitCodeForAppError` maps every `AppErrorCode` through the one shared
 *     error table (01.0/02).
 *
 * On failure first check:
 *   - That `DispatchLinkCommand` initializes one-shot logging as *disabled*
 *     before executing (D14) so the audit sink never writes to stderr.
 *   - That the fallback runtime and the legacy factory bind to the SAME
 *     process-wide `LinkBackend::store()`, so a create then a read in one process
 *     observe the same link (every case shares that singleton; reused slugs
 *     collide, so each case uses a unique slug).
 *   - That `link_command_dispatch.cpp` includes nothing under http/ and never
 *     constructs an `HttpServer`/`io_context` (architecture invariant; this test
 *     binary links the common library and references none of them).
 */
#define BOOST_TEST_MODULE LinkDispatchViaRegistry
#include <boost/test/unit_test.hpp>

#include <memory>
#include <optional>
#include <ostream>
#include <sstream>
#include <streambuf>
#include <string>

#include <url_shortener/app/app_error.hpp>
#include <url_shortener/app/link_backend.hpp>
#include <url_shortener/app/link_command_service.hpp>
#include <url_shortener/cli/cli_parser.hpp>
#include <url_shortener/cli/cmd_dispatch.hpp>
#include <url_shortener/cli/link_command_dispatch.hpp>
#include <url_shortener/composition/link_command_service_factory.hpp>
#include <url_shortener/core/config.hpp>
#include <url_shortener/core/types.hpp>

namespace
{
namespace app = url_shortener::app;
using url_shortener::Link;

/// RAII redirect of a std::ostream (cout/cerr) into a captured buffer.
class StreamCapture
{
public:
    explicit StreamCapture(std::ostream& stream)
        : m_stream_(stream), m_previous_(stream.rdbuf(m_buffer_.rdbuf()))
    {
    }

    ~StreamCapture() { m_stream_.rdbuf(m_previous_); }

    StreamCapture(const StreamCapture&) = delete;
    StreamCapture& operator=(const StreamCapture&) = delete;

    /// The text written to the stream so far.
    std::string str() const { return m_buffer_.str(); }

private:
    std::ostream& m_stream_;
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

LinkCliCommand makeCreate(const std::string& slug, const std::string& target)
{
    app::CreateLinkCommand payload;
    payload.target_url = target;
    payload.slug = slug;
    LinkCliCommand command;
    command.verb = LinkCliVerb::create;
    command.payload = payload;
    return command;
}

LinkCliCommand makeGetLike(LinkCliVerb verb, const std::string& slug)
{
    app::GetLinkQuery payload;
    payload.by = app::GetLinkBy::slug;
    payload.value = slug;
    LinkCliCommand command;
    command.verb = verb;
    command.payload = payload;
    return command;
}

LinkCliCommand makeUpdateDisable(const std::string& slug)
{
    app::UpdateLinkCommand payload;
    payload.slug = slug;
    payload.enabled = false;
    LinkCliCommand command;
    command.verb = LinkCliVerb::update;
    command.payload = payload;
    return command;
}

template <typename Dto>
LinkCliCommand makeLifecycle(LinkCliVerb verb, const std::string& slug)
{
    Dto payload;
    payload.slug = slug;
    LinkCliCommand command;
    command.verb = verb;
    command.payload = payload;
    return command;
}

LinkCliCommand makeStats(const std::string& slug, const std::string& from,
                         const std::string& to, const std::string& bucket)
{
    app::GetLinkStatsQuery payload;
    payload.slug = slug;
    payload.from = from;
    payload.to = to;
    payload.bucket = bucket;
    LinkCliCommand command;
    command.verb = LinkCliVerb::stats;
    command.payload = payload;
    return command;
}

/// Seed a link with an explicit slug, discarding its output. Returns exit code.
int seed(const ServerConfig& config, const std::string& slug)
{
    StreamCapture out(std::cout);
    StreamCapture err(std::cerr);
    return url_shortener::cli::DispatchLinkCommand(
        makeCreate(slug, "https://example.com/target"), config);
}

/// Reconstruct the current serializeLinkViewJson of a link through the shared
/// factory (same process-wide store the dispatch wrote through).
std::string currentViewJson(const ServerConfig& config, const std::string& slug)
{
    const app::LinkCommandServiceBundle bundle =
        app::BuildLegacyLinkCommandService(config);
    app::GetLinkQuery query;
    query.by = app::GetLinkBy::slug;
    query.value = slug;
    const auto result = bundle.service->GetLink(query);
    BOOST_REQUIRE(result.ok());
    BOOST_REQUIRE(result.value.has_value());
    return app::serializeLinkViewJson(*result.value);
}

/// A store whose writes always fail with storage_failure, to force the exit-4
/// class end-to-end through the legacy store seam (LinkBackend::install).
class FailingLinkStore : public app::ILinkStore
{
public:
    bool create(const Link&, app::AppError* error) override
    {
        fail(error);
        return false;
    }

    std::optional<Link> findBySlug(const std::string&) const override
    {
        return std::nullopt;
    }

    std::optional<Link> findById(const std::string&) const override
    {
        return std::nullopt;
    }

    bool slugExists(const std::string&) const override { return false; }

    void invalidateCache(const std::string&) override {}

    bool update(const Link&, app::AppError* error) override
    {
        fail(error);
        return false;
    }

    bool recordRedirect(const Link&, app::AppError* error) override
    {
        fail(error);
        return false;
    }

private:
    static void fail(app::AppError* error)
    {
        if (error != nullptr) {
            error->code = app::AppErrorCode::storage_failure;
            error->detail = "storage unavailable";
        }
    }
};

}  // namespace

// ---- success: one serializer line on stdout, empty stderr ------------------

BOOST_AUTO_TEST_CASE(create_success_one_line_empty_stderr)
{
    const ServerConfig config = makeConfig();

    std::string out_text;
    std::string err_text;
    {
        StreamCapture out(std::cout);
        StreamCapture err(std::cerr);
        BOOST_CHECK_EQUAL(url_shortener::cli::DispatchLinkCommand(
                              makeCreate("reg_create", "https://e.com/a"),
                              config),
                          0);
        out_text = out.str();
        err_text = err.str();
    }

    BOOST_REQUIRE(!out_text.empty());
    BOOST_CHECK_EQUAL(out_text.back(), '\n');
    const std::string payload = out_text.substr(0, out_text.size() - 1);
    BOOST_CHECK_EQUAL(payload.find('\n'), std::string::npos);
    BOOST_CHECK_EQUAL(payload.front(), '{');
    BOOST_CHECK_EQUAL(payload.back(), '}');
    BOOST_CHECK(payload.find("\"slug\":\"reg_create\"") != std::string::npos);
    // D14: no command_audit line leaks to stderr on a successful mutation.
    BOOST_CHECK(err_text.empty());
}

BOOST_AUTO_TEST_CASE(get_success_matches_shared_serializer)
{
    const ServerConfig config = makeConfig();
    BOOST_REQUIRE_EQUAL(seed(config, "reg_get"), 0);

    std::string out_text;
    std::string err_text;
    {
        StreamCapture out(std::cout);
        StreamCapture err(std::cerr);
        BOOST_CHECK_EQUAL(url_shortener::cli::DispatchLinkCommand(
                              makeGetLike(LinkCliVerb::get, "reg_get"), config),
                          0);
        out_text = out.str();
        err_text = err.str();
    }

    BOOST_REQUIRE(!out_text.empty());
    BOOST_CHECK_EQUAL(out_text.back(), '\n');
    BOOST_CHECK_EQUAL(out_text.substr(0, out_text.size() - 1),
                      currentViewJson(config, "reg_get"));
    BOOST_CHECK(err_text.empty());
}

BOOST_AUTO_TEST_CASE(preview_success_matches_preview_serializer)
{
    const ServerConfig config = makeConfig();
    BOOST_REQUIRE_EQUAL(seed(config, "reg_preview"), 0);

    std::string out_text;
    {
        StreamCapture out(std::cout);
        StreamCapture err(std::cerr);
        BOOST_CHECK_EQUAL(
            url_shortener::cli::DispatchLinkCommand(
                makeGetLike(LinkCliVerb::preview, "reg_preview"), config),
            0);
        out_text = out.str();
        BOOST_CHECK(err.str().empty());
    }

    const app::LinkCommandServiceBundle bundle =
        app::BuildLegacyLinkCommandService(config);
    app::GetLinkQuery query;
    query.by = app::GetLinkBy::slug;
    query.value = "reg_preview";
    const auto result = bundle.service->PreviewLink(query);
    BOOST_REQUIRE(result.ok());
    BOOST_REQUIRE(result.value.has_value());
    BOOST_CHECK_EQUAL(out_text.substr(0, out_text.size() - 1),
                      app::serializeLinkPreviewJson(*result.value));
}

BOOST_AUTO_TEST_CASE(update_success_matches_and_empty_stderr)
{
    const ServerConfig config = makeConfig();
    BOOST_REQUIRE_EQUAL(seed(config, "reg_update"), 0);

    std::string out_text;
    std::string err_text;
    {
        StreamCapture out(std::cout);
        StreamCapture err(std::cerr);
        BOOST_CHECK_EQUAL(url_shortener::cli::DispatchLinkCommand(
                              makeUpdateDisable("reg_update"), config),
                          0);
        out_text = out.str();
        err_text = err.str();
    }

    BOOST_CHECK_EQUAL(out_text.substr(0, out_text.size() - 1),
                      currentViewJson(config, "reg_update"));
    BOOST_CHECK(err_text.empty());
}

BOOST_AUTO_TEST_CASE(enable_disable_success_empty_stderr)
{
    const ServerConfig config = makeConfig();
    BOOST_REQUIRE_EQUAL(seed(config, "reg_toggle"), 0);

    for (const LinkCliVerb verb : {LinkCliVerb::disable, LinkCliVerb::enable}) {
        std::string out_text;
        std::string err_text;
        {
            StreamCapture out(std::cout);
            StreamCapture err(std::cerr);
            BOOST_CHECK_EQUAL(
                url_shortener::cli::DispatchLinkCommand(
                    makeLifecycle<app::SetLinkEnabledCommand>(verb,
                                                              "reg_toggle"),
                    config),
                0);
            out_text = out.str();
            err_text = err.str();
        }
        BOOST_CHECK_EQUAL(out_text.substr(0, out_text.size() - 1),
                          currentViewJson(config, "reg_toggle"));
        BOOST_CHECK(err_text.empty());
    }
}

BOOST_AUTO_TEST_CASE(delete_success_one_line_empty_stderr)
{
    const ServerConfig config = makeConfig();
    BOOST_REQUIRE_EQUAL(seed(config, "reg_delete"), 0);

    std::string out_text;
    std::string err_text;
    {
        StreamCapture out(std::cout);
        StreamCapture err(std::cerr);
        BOOST_CHECK_EQUAL(
            url_shortener::cli::DispatchLinkCommand(
                makeLifecycle<app::DeleteLinkCommand>(LinkCliVerb::del,
                                                      "reg_delete"),
                config),
            0);
        out_text = out.str();
        err_text = err.str();
    }

    BOOST_REQUIRE(!out_text.empty());
    BOOST_CHECK_EQUAL(out_text.back(), '\n');
    const std::string payload = out_text.substr(0, out_text.size() - 1);
    BOOST_CHECK_EQUAL(payload.find('\n'), std::string::npos);
    BOOST_CHECK_EQUAL(payload.front(), '{');
    BOOST_CHECK_EQUAL(payload.back(), '}');
    BOOST_CHECK(err_text.empty());
}

BOOST_AUTO_TEST_CASE(restore_success_matches_and_empty_stderr)
{
    const ServerConfig config = makeConfig();
    BOOST_REQUIRE_EQUAL(seed(config, "reg_restore"), 0);

    {
        StreamCapture out(std::cout);
        StreamCapture err(std::cerr);
        BOOST_REQUIRE_EQUAL(
            url_shortener::cli::DispatchLinkCommand(
                makeLifecycle<app::DeleteLinkCommand>(LinkCliVerb::del,
                                                      "reg_restore"),
                config),
            0);
    }

    std::string out_text;
    std::string err_text;
    {
        StreamCapture out(std::cout);
        StreamCapture err(std::cerr);
        BOOST_CHECK_EQUAL(
            url_shortener::cli::DispatchLinkCommand(
                makeLifecycle<app::RestoreLinkCommand>(LinkCliVerb::restore,
                                                       "reg_restore"),
                config),
            0);
        out_text = out.str();
        err_text = err.str();
    }

    BOOST_CHECK_EQUAL(out_text.substr(0, out_text.size() - 1),
                      currentViewJson(config, "reg_restore"));
    BOOST_CHECK(err_text.empty());
}

BOOST_AUTO_TEST_CASE(stats_success_matches_serializer)
{
    const ServerConfig config = makeConfig();
    BOOST_REQUIRE_EQUAL(seed(config, "reg_stats"), 0);

    std::string out_text;
    {
        StreamCapture out(std::cout);
        StreamCapture err(std::cerr);
        BOOST_CHECK_EQUAL(
            url_shortener::cli::DispatchLinkCommand(
                makeStats("reg_stats", "1700000000", "1800000000", "day"),
                config),
            0);
        out_text = out.str();
        BOOST_CHECK(err.str().empty());
    }

    const app::LinkCommandServiceBundle bundle =
        app::BuildLegacyLinkCommandService(config);
    app::GetLinkStatsQuery query;
    query.slug = "reg_stats";
    query.from = "1700000000";
    query.to = "1800000000";
    query.bucket = "day";
    const auto result = bundle.service->GetLinkStats(query);
    BOOST_REQUIRE(result.ok());
    BOOST_REQUIRE(result.value.has_value());
    BOOST_CHECK_EQUAL(out_text.substr(0, out_text.size() - 1),
                      app::serializeLinkStatsJson(*result.value));
}

// ---- failures: exit codes, stderr text, empty stdout -----------------------

BOOST_AUTO_TEST_CASE(get_unknown_exit1_with_legacy_stderr)
{
    const ServerConfig config = makeConfig();

    std::string out_text;
    std::string err_text;
    {
        StreamCapture out(std::cout);
        StreamCapture err(std::cerr);
        BOOST_CHECK_EQUAL(
            url_shortener::cli::DispatchLinkCommand(
                makeGetLike(LinkCliVerb::get, "reg_missing"), config),
            1);
        out_text = out.str();
        err_text = err.str();
    }

    BOOST_CHECK(out_text.empty());
    BOOST_CHECK_EQUAL(err_text, "get failed: Link not found\n");
}

BOOST_AUTO_TEST_CASE(create_invalid_url_exit2_empty_stdout)
{
    const ServerConfig config = makeConfig();
    std::string out_text;
    {
        StreamCapture out(std::cout);
        StreamCapture err(std::cerr);
        BOOST_CHECK_EQUAL(
            url_shortener::cli::DispatchLinkCommand(
                makeCreate("reg_badurl", "not-an-absolute-url"), config),
            2);
        out_text = out.str();
    }
    BOOST_CHECK(out_text.empty());
}

BOOST_AUTO_TEST_CASE(create_reserved_slug_exit2_empty_stdout)
{
    const ServerConfig config = makeConfig();
    std::string out_text;
    {
        StreamCapture out(std::cout);
        StreamCapture err(std::cerr);
        BOOST_CHECK_EQUAL(url_shortener::cli::DispatchLinkCommand(
                              makeCreate("admin", "https://e.com/a"), config),
                          2);
        out_text = out.str();
    }
    BOOST_CHECK(out_text.empty());
}

BOOST_AUTO_TEST_CASE(create_invalid_slug_exit2_empty_stdout)
{
    const ServerConfig config = makeConfig();
    std::string out_text;
    {
        StreamCapture out(std::cout);
        StreamCapture err(std::cerr);
        BOOST_CHECK_EQUAL(url_shortener::cli::DispatchLinkCommand(
                              makeCreate("a!b", "https://e.com/a"), config),
                          2);
        out_text = out.str();
    }
    BOOST_CHECK(out_text.empty());
}

BOOST_AUTO_TEST_CASE(create_duplicate_slug_exit3_empty_stdout)
{
    const ServerConfig config = makeConfig();
    BOOST_REQUIRE_EQUAL(seed(config, "reg_dupe"), 0);

    std::string out_text;
    {
        StreamCapture out(std::cout);
        StreamCapture err(std::cerr);
        BOOST_CHECK_EQUAL(url_shortener::cli::DispatchLinkCommand(
                              makeCreate("reg_dupe", "https://e.com/b"), config),
                          3);
        out_text = out.str();
    }
    BOOST_CHECK(out_text.empty());
}

BOOST_AUTO_TEST_CASE(lifecycle_and_read_unknown_slug_exit1)
{
    const ServerConfig config = makeConfig();

    const auto check_exit1 = [&config](const LinkCliCommand& command)
    {
        std::string out_text;
        {
            StreamCapture out(std::cout);
            StreamCapture err(std::cerr);
            BOOST_CHECK_EQUAL(
                url_shortener::cli::DispatchLinkCommand(command, config), 1);
            out_text = out.str();
        }
        BOOST_CHECK(out_text.empty());
    };

    check_exit1(makeUpdateDisable("reg_none"));
    check_exit1(makeLifecycle<app::DeleteLinkCommand>(LinkCliVerb::del,
                                                      "reg_none"));
    check_exit1(makeLifecycle<app::SetLinkEnabledCommand>(LinkCliVerb::enable,
                                                          "reg_none"));
    check_exit1(makeLifecycle<app::SetLinkEnabledCommand>(LinkCliVerb::disable,
                                                          "reg_none"));
    check_exit1(makeLifecycle<app::RestoreLinkCommand>(LinkCliVerb::restore,
                                                       "reg_none"));
    check_exit1(makeGetLike(LinkCliVerb::preview, "reg_none"));
}

BOOST_AUTO_TEST_CASE(stats_invalid_timestamp_and_bucket_exit2)
{
    const ServerConfig config = makeConfig();
    BOOST_REQUIRE_EQUAL(seed(config, "reg_statsbad"), 0);

    const app::LinkCommandServiceBundle bundle =
        app::BuildLegacyLinkCommandService(config);

    // Reconstruct the legacy detail text from the same reader so the assertion
    // is parity, not a hard-coded string (contract C4).
    const auto expect_failure = [&](const std::string& from,
                                    const std::string& to,
                                    const std::string& bucket)
    {
        app::GetLinkStatsQuery query;
        query.slug = "reg_statsbad";
        query.from = from;
        query.to = to;
        query.bucket = bucket;
        const auto expected = bundle.service->GetLinkStats(query);
        BOOST_REQUIRE(!expected.ok());

        std::string out_text;
        std::string err_text;
        {
            StreamCapture out(std::cout);
            StreamCapture err(std::cerr);
            BOOST_CHECK_EQUAL(
                url_shortener::cli::DispatchLinkCommand(
                    makeStats("reg_statsbad", from, to, bucket), config),
                2);
            out_text = out.str();
            err_text = err.str();
        }
        BOOST_CHECK(out_text.empty());
        BOOST_CHECK_EQUAL(
            err_text, "stats failed: " + expected.error.detail + "\n");
        BOOST_CHECK_EQUAL(
            url_shortener::cli::ExitCodeForAppError(expected.error.code), 2);
    };

    expect_failure("not-an-epoch", "1800000000", "day");   // invalid timestamp
    expect_failure("1700000000", "1800000000", "minute");  // invalid bucket
}

BOOST_AUTO_TEST_CASE(storage_failure_exit4_empty_stdout)
{
    const ServerConfig config = makeConfig();

    // Install a store whose writes fail, then restore the default afterwards so
    // later cases (and other test binaries) see the normal in-memory store.
    struct StoreGuard
    {
        StoreGuard()
        {
            app::LinkBackend::install(std::make_shared<FailingLinkStore>());
        }
        ~StoreGuard() { app::LinkBackend::reset(); }
    } guard;

    std::string out_text;
    {
        StreamCapture out(std::cout);
        StreamCapture err(std::cerr);
        BOOST_CHECK_EQUAL(url_shortener::cli::DispatchLinkCommand(
                              makeCreate("reg_fail", "https://e.com/a"), config),
                          4);
        out_text = out.str();
    }
    BOOST_CHECK(out_text.empty());
}

// ---- shared exit-code table and cross-transport parity ---------------------

BOOST_AUTO_TEST_CASE(exit_code_mapping_reads_shared_table)
{
    using app::AppErrorCode;
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

BOOST_AUTO_TEST_CASE(cmd_and_link_get_produce_same_result)
{
    const ServerConfig config = makeConfig();
    BOOST_REQUIRE_EQUAL(seed(config, "reg_parity"), 0);

    std::string sugar_out;
    {
        StreamCapture out(std::cout);
        StreamCapture err(std::cerr);
        BOOST_CHECK_EQUAL(
            url_shortener::cli::DispatchLinkCommand(
                makeGetLike(LinkCliVerb::get, "reg_parity"), config),
            0);
        sugar_out = out.str();
    }

    // `cmd link.get --raw` prints only the envelope's `result` (the same handler
    // output), so the two transports must be byte-identical for the same link.
    CmdCliCommand cmd;
    cmd.command = "link.get";
    cmd.json_args = "{\"slug\":\"reg_parity\"}";
    cmd.raw = true;

    std::istringstream in;
    std::ostringstream cmd_out;
    std::ostringstream cmd_err;
    BOOST_CHECK_EQUAL(
        url_shortener::cli::DispatchCmdCommand(cmd, config, in, cmd_out,
                                               cmd_err),
        0);
    BOOST_CHECK_EQUAL(sugar_out, cmd_out.str());
}

// ---- config flags flow through the runtime ---------------------------------

BOOST_AUTO_TEST_CASE(base_domain_flows_into_short_url)
{
    ServerConfig config = makeConfig();
    config.shortener_base_domain = "http://custom.example";

    std::string out_text;
    {
        StreamCapture out(std::cout);
        StreamCapture err(std::cerr);
        BOOST_CHECK_EQUAL(url_shortener::cli::DispatchLinkCommand(
                              makeCreate("reg_base", "https://e.com/a"), config),
                          0);
        out_text = out.str();
    }
    BOOST_CHECK(out_text.find("http://custom.example") != std::string::npos);
}

BOOST_AUTO_TEST_CASE(allow_private_targets_true_allows_loopback)
{
    ServerConfig config = makeConfig();
    config.shortener_allow_private_targets = true;

    std::string out_text;
    {
        StreamCapture out(std::cout);
        StreamCapture err(std::cerr);
        BOOST_CHECK_EQUAL(
            url_shortener::cli::DispatchLinkCommand(
                makeCreate("reg_loopback", "http://127.0.0.1/x"), config),
            0);
        out_text = out.str();
    }
    BOOST_CHECK(!out_text.empty());
    BOOST_CHECK_EQUAL(out_text.back(), '\n');
}
