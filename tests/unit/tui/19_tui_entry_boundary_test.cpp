/**
 * @file 19_tui_entry_boundary_test.cpp
 * @brief Boundary tests for the `tui` CLI wiring (Task 06.0 subtask 19).
 *
 * Two kinds of check:
 *  1. Static source scan (contract C5/C8/C11, threat model T-114):
 * `tui_entry.cpp` and `app.cpp` name no server/`io_context`/`uri`-map symbol,
 * and the `tui` branch of `main.cpp` (delimited by the `// tui-mode-begin` /
 * `// tui-mode-end` markers) likewise names none; `tui_entry.cpp` obtains its
 * registry from `build_command_runtime`, constructs no registry/job-runner of
 * its own, and opts into file paths (`allow_paths = true`).
 *  2. A round-trip smoke (T-114): a link created through an in-process client
 * over a runtime built by `build_command_runtime` is read back through the SAME
 *     client, proving the TUI reads and writes the one active store.
 *
 * The source scan resolves the repository root from the compile-time
 * `TUI_GOLDEN_DIR` (`<root>/tests/unit/tui/golden`), so no extra build wiring
 * is needed.
 */
#define BOOST_TEST_MODULE TuiEntryBoundaryTest

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

#include <boost/test/unit_test.hpp>
#include <url_shortener/app/link_backend.hpp>
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

namespace obs = url_shortener::observability;
namespace composition = url_shortener::composition;
namespace commands = url_shortener::commands;
namespace app = url_shortener::app;
namespace tui = url_shortener::tui;

namespace
{

/// Repository root, derived from the golden dir `<root>/tests/unit/tui/golden`.
std::filesystem::path repo_root()
{
    std::filesystem::path golden = TUI_GOLDEN_DIR;
    // golden -> tui -> unit -> tests -> <root>
    return golden.parent_path().parent_path().parent_path().parent_path();
}

/// Read a whole source file into a string.
std::string read_file(const std::filesystem::path& path)
{
    std::ifstream in(path);
    BOOST_REQUIRE_MESSAGE(in.good(), "cannot open " + path.string());
    std::stringstream buffer;
    buffer << in.rdbuf();
    return buffer.str();
}

/// The server/listener/data-file symbols the TUI path must never name.
const char* const k_forbidden[] = {
    "HttpServer",
    "http_server.hpp",
    "UriMapSingleton",
    "uri.txt",
    "io_context",
};

void assert_free_of_forbidden(const std::string& text, const std::string& where)
{
    for (const char* token : k_forbidden) {
        BOOST_CHECK_MESSAGE(text.find(token) == std::string::npos,
                            where + " must not name '" + token + "'");
    }
}

/// Isolate the process-wide holders between round-trip cases.
struct Fixture
{
    Fixture()
    {
        isolate();
        obs::LoggingConfig cfg;
        cfg.enabled = false;  // keep audit lines off the test's stderr
        obs::LoggerFactory::Initialize(cfg);
    }

    ~Fixture() { isolate(); }

    static void isolate()
    {
        composition::reset_process_job_runtime_for_tests();
        app::LinkBackend::reset();
    }
};

}  // namespace

// Type: unit (static boundary, C5). Scenario: tui_entry.cpp names no
//   server/io_context/uri-map symbol.
BOOST_AUTO_TEST_CASE(tui_entry_names_no_server_symbol)
{
    const std::string text = read_file(repo_root() / "src/tui/tui_entry.cpp");
    assert_free_of_forbidden(text, "tui_entry.cpp");
}

// Type: unit (static boundary, C5). Scenario: app.cpp names no server symbol.
BOOST_AUTO_TEST_CASE(app_names_no_server_symbol)
{
    const std::string text = read_file(repo_root() / "src/tui/app.cpp");
    assert_free_of_forbidden(text, "app.cpp");
}

// Type: unit (static boundary, C5). Scenario: the `tui` branch of main.cpp,
//   between its markers, names no server symbol - even though main.cpp as a
//   whole does (it runs the server in the other branch).
// On failure first check: were the `// tui-mode-begin` / `// tui-mode-end`
// markers
//   removed, or did server construction leak into the branch?
BOOST_AUTO_TEST_CASE(main_tui_branch_names_no_server_symbol)
{
    const std::string text = read_file(repo_root() / "src/main.cpp");
    const std::string begin = "// tui-mode-begin";
    const std::string end = "// tui-mode-end";
    const std::size_t at = text.find(begin);
    const std::size_t to = text.find(end);
    BOOST_REQUIRE(at != std::string::npos);
    BOOST_REQUIRE(to != std::string::npos);
    BOOST_REQUIRE(to > at);
    const std::string region = text.substr(at, to - at);
    assert_free_of_forbidden(region, "main.cpp tui branch");
    // The branch must actually dispatch to run_tui (not fall through to
    // server).
    BOOST_CHECK(region.find("run_tui") != std::string::npos);
}

// Type: unit (static boundary, C8/C11/T-114). Scenario: tui_entry.cpp takes its
//   registry from build_command_runtime, constructs no registry/job-runner of
//   its own, and opts into file paths.
BOOST_AUTO_TEST_CASE(tui_entry_uses_the_one_composition_root)
{
    const std::string text = read_file(repo_root() / "src/tui/tui_entry.cpp");
    BOOST_CHECK(text.find("build_command_runtime") != std::string::npos);
    BOOST_CHECK(text.find("allow_paths = true") != std::string::npos);
    // The registry/job-runner come from the one runtime, never built here.
    BOOST_CHECK(text.find("CommandRegistry") == std::string::npos);
    BOOST_CHECK(text.find("JobRunner") == std::string::npos);
}

// Type: unit (T-114 round-trip). Scenario: a link created through the
// in-process
//   client over a build_command_runtime registry is read back through the same
//   client, proving one active store underlies both reads and writes.
// On failure first check: did build_command_runtime bind the link service to a
//   store other than the active one (the serve-stdio split-store bug)?
BOOST_FIXTURE_TEST_CASE(tui_client_reads_back_its_own_write, Fixture)
{
    const ServerConfig config;
    app::LinkBackendHandle backend;  // inactive -> the in-memory default store
    composition::CommandRuntimeOptions runtime_options;
    runtime_options.file_paths.allow_paths = true;
    composition::CommandRuntime runtime =
        composition::build_command_runtime(config, backend, runtime_options);

    commands::CallerContext caller;
    caller.principal = "local-operator";
    caller.control_set = ControlSet::Admin;
    caller.transport = "tui";
    tui::InProcessClient client(*runtime.registry, caller);

    const std::string slug = "tui19rt";

    commands::CommandRequest create;
    create.command = "link.create";
    create.id = "c1";
    create.args_json = R"({"target_url":"https://example.com/)" + slug
        + R"(","slug":")" + slug + R"("})";
    const commands::CommandResponse created = client.call(create);
    BOOST_REQUIRE(created.ok);

    commands::CommandRequest get;
    get.command = "link.get";
    get.id = "g1";
    get.args_json = R"({"slug":")" + slug + R"("})";
    const commands::CommandResponse got = client.call(get);

    BOOST_REQUIRE(got.ok);
    BOOST_CHECK(got.result_json.find(slug) != std::string::npos);
}
