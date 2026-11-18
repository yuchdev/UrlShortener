/**
 * @file 19_tui_options_test.cpp
 * @brief Unit tests for the `tui` option mapping and logging derivation
 * (Task 06.0 subtask 19): @ref url_shortener::tui::to_tui_options maps each
 * launch flag and propagates `max_inline_bytes`; @ref
 * url_shortener::tui::tui_logging_config always yields a `File` sink (never a
 * terminal, never disabled); and the initialized log file is created `0600`
 * with audit lines that carry no argument values (decision log D14, threat
 * model T-119).
 */
#define BOOST_TEST_MODULE TuiOptionsTest

#include <optional>
#include <string>

#if !defined(_WIN32)
#    include <sys/stat.h>
#endif

#include <filesystem>
#include <fstream>
#include <sstream>

#include <boost/test/unit_test.hpp>
#include <url_shortener/app/link_backend.hpp>
#include <url_shortener/cli/cli_parser.hpp>
#include <url_shortener/commands/command_descriptor.hpp>
#include <url_shortener/commands/command_registry.hpp>
#include <url_shortener/commands/envelope.hpp>
#include <url_shortener/composition/command_runtime.hpp>
#include <url_shortener/composition/job_runtime.hpp>
#include <url_shortener/composition/link_backend_bootstrap.hpp>
#include <url_shortener/core/config.hpp>
#include <url_shortener/observability/logger_factory.hpp>
#include <url_shortener/observability/logging_config.hpp>
#include <url_shortener/security/control_set.hpp>
#include <url_shortener/tui/command_client.hpp>
#include <url_shortener/tui/in_process_client.hpp>
#include <url_shortener/tui/theme.hpp>
#include <url_shortener/tui/tui_entry.hpp>
#include <url_shortener/tui/tui_options.hpp>

namespace obs = url_shortener::observability;
namespace composition = url_shortener::composition;
namespace commands = url_shortener::commands;
namespace app = url_shortener::app;
namespace tui = url_shortener::tui;

namespace
{

/// Resets the process-wide job runtime and link backend so a case starts with a
/// fresh job counter and an empty installed store, and restores logging to a
/// quiet (disabled) sink afterwards so a File sink never leaks into another
/// case.
struct Fixture
{
    Fixture() { isolate(); }

    ~Fixture()
    {
        isolate();
        obs::LoggingConfig cfg;
        cfg.enabled = false;
        obs::LoggerFactory::Initialize(cfg);
    }

    static void isolate()
    {
        composition::reset_process_job_runtime_for_tests();
        app::LinkBackend::reset();
    }
};

/// A unique temporary log path for a case (removed by the caller).
std::filesystem::path temp_log_path(const std::string& stem)
{
    return std::filesystem::temp_directory_path()
        / ("url_shortener_tui_" + stem + ".log");
}

}  // namespace

// Type: unit (normal). Scenario: each theme name maps to its ThemeName; the
//   defaults (read-write, confirmations on) and max_inline_bytes carry across.
BOOST_AUTO_TEST_CASE(to_tui_options_maps_each_theme_and_defaults)
{
    const ServerConfig config;  // max_request_body_bytes default (65536)

    for (const auto& pair :
         {std::pair<std::string, tui::ThemeName> {"dark", tui::ThemeName::dark},
          {"light", tui::ThemeName::light},
          {"mono", tui::ThemeName::mono}})
    {
        TuiCliOptions cli;
        cli.theme = pair.first;
        std::string error = "untouched";
        const std::optional<tui::TuiOptions> options =
            tui::to_tui_options(cli, config, &error);

        BOOST_REQUIRE(options.has_value());
        BOOST_CHECK(options->theme == pair.second);
        BOOST_CHECK_EQUAL(options->read_only, false);
        BOOST_CHECK_EQUAL(options->assume_yes, false);
        BOOST_CHECK_EQUAL(error,
                          std::string("untouched"));  // success: untouched
        BOOST_CHECK_EQUAL(
            options->max_inline_bytes,
            static_cast<std::size_t>(config.max_request_body_bytes));
    }
}

// Type: unit. Scenario: read_only / assume_yes flags propagate to TuiOptions.
BOOST_AUTO_TEST_CASE(to_tui_options_propagates_flags)
{
    ServerConfig config;
    config.max_request_body_bytes = 4096;  // a non-default inline bound

    TuiCliOptions cli;
    cli.theme = "mono";
    cli.read_only = true;
    cli.assume_yes = true;

    const std::optional<tui::TuiOptions> options =
        tui::to_tui_options(cli, config, nullptr);

    BOOST_REQUIRE(options.has_value());
    BOOST_CHECK_EQUAL(options->read_only, true);
    BOOST_CHECK_EQUAL(options->assume_yes, true);
    BOOST_CHECK_EQUAL(options->max_inline_bytes,
                      static_cast<std::size_t>(4096));
}

// Type: unit (invalid-input). Scenario: an unrecognized theme fails the mapping
//   and writes a value-free error (defense in depth behind the parser).
BOOST_AUTO_TEST_CASE(to_tui_options_rejects_unknown_theme)
{
    const ServerConfig config;
    TuiCliOptions cli;
    cli.theme = "solarized";
    std::string error;

    const std::optional<tui::TuiOptions> options =
        tui::to_tui_options(cli, config, &error);

    BOOST_CHECK(!options.has_value());
    BOOST_CHECK(!error.empty());
    BOOST_CHECK(error.find("solarized") == std::string::npos);  // no echo
}

// Type: unit (normal). Scenario: with no --log-file the logging config is a
// File
//   sink at the default path, enabled, never a terminal sink.
BOOST_AUTO_TEST_CASE(tui_logging_config_default_is_file_sink)
{
    TuiCliOptions cli;  // no log_file
    const obs::LoggingConfig cfg = tui::tui_logging_config(cli);

    BOOST_CHECK_EQUAL(cfg.enabled, true);
    BOOST_CHECK(cfg.sink == obs::LogSink::File);
    BOOST_CHECK(cfg.sink != obs::LogSink::Stdout);
    BOOST_CHECK(cfg.sink != obs::LogSink::Stderr);
    BOOST_CHECK_EQUAL(cfg.file_path, std::string("url_shortener-tui.log"));
}

// Type: unit (normal). Scenario: --log-file selects the File sink path
// verbatim.
BOOST_AUTO_TEST_CASE(tui_logging_config_honours_log_file)
{
    TuiCliOptions cli;
    cli.log_file = std::string("/var/log/my-tui.log");
    const obs::LoggingConfig cfg = tui::tui_logging_config(cli);

    BOOST_CHECK_EQUAL(cfg.enabled, true);
    BOOST_CHECK(cfg.sink == obs::LogSink::File);
    BOOST_CHECK_EQUAL(cfg.file_path, std::string("/var/log/my-tui.log"));
}

// Type: unit (T-119). Scenario: after Initialize(tui_logging_config), the
// created
//   log file is 0600 (POSIX), and a mutating command's command_audit line
//   carries the command name but no argument value.
// On failure first check: does the File sink still open with mode 0600, and
// does
//   LoggerAuditSink still log only name/principal/outcome (never arg payloads)?
BOOST_FIXTURE_TEST_CASE(log_file_is_0600_and_audit_has_no_arg_values, Fixture)
{
    const std::filesystem::path log = temp_log_path("audit");
    std::error_code rm_ec;
    std::filesystem::remove(log, rm_ec);  // start from no file

    TuiCliOptions cli;
    cli.log_file = log.string();

    // Initialize to the File sink BEFORE building the runtime, exactly as
    // run_tui does, so the runtime's LoggerAuditSink writes to this file.
    std::string init_error;
    BOOST_REQUIRE(obs::LoggerFactory::Initialize(tui::tui_logging_config(cli),
                                                 &init_error));

#if !defined(_WIN32)
    struct ::stat st {};
    BOOST_REQUIRE_EQUAL(::stat(log.c_str(), &st), 0);
    BOOST_CHECK_EQUAL(st.st_mode & 0777, 0600);
#endif

    // Build the one runtime over an inactive handle (the in-memory default) and
    // run a mutating link.create as the local operator through the in-process
    // client. The unique target marker must NOT appear in the audit file.
    const ServerConfig config;
    app::LinkBackendHandle backend;  // inactive -> in-memory
    composition::CommandRuntimeOptions runtime_options;
    runtime_options.file_paths.allow_paths = true;
    composition::CommandRuntime runtime =
        composition::build_command_runtime(config, backend, runtime_options);

    commands::CallerContext caller;
    caller.principal = "local-operator";
    caller.control_set = ControlSet::Admin;
    caller.transport = "tui";
    tui::InProcessClient client(*runtime.registry, caller);

    const std::string marker = "t119-unique-target-xyz";
    commands::CommandRequest request;
    request.command = "link.create";
    request.id = "t119-1";
    request.args_json = R"({"target_url":"https://example.com/)" + marker
        + R"(","slug":"t119slug"})";
    const commands::CommandResponse response = client.call(request);
    BOOST_REQUIRE(response.ok);

    std::ifstream in(log);
    std::stringstream buffer;
    buffer << in.rdbuf();
    const std::string contents = buffer.str();

    BOOST_CHECK(contents.find("command_audit") != std::string::npos);
    BOOST_CHECK(contents.find("link.create") != std::string::npos);
    // The audit line records the command's resource target (the slug) so the
    // trail is useful, but never an argument *value* payload like the
    // target_url (contract C6, threat model T-119).
    BOOST_CHECK(contents.find("target=t119slug") != std::string::npos);
    BOOST_CHECK(contents.find(marker) == std::string::npos);

    std::filesystem::remove(log, rm_ec);
}
