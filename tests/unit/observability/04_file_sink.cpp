/**
 * @file 04_file_sink.cpp
 * @brief Unit tests for the append-only logging File sink and its fail-closed
 * wiring through LoggerFactory::Initialize (milestone 0006, Task 04.0/13).
 *
 * Type: unit (in-process; no server, no listener).
 *
 * Scenario:
 *   FileSink::Open creates the log file private (0600) and append-only, refuses
 *   to follow a symlink at the final path component, and fails closed (nullptr,
 *   value-free error) when the file cannot be opened. LoggerFactory::Initialize
 *   surfaces that failure and, on success, routes log lines to the file instead
 *   of stderr.
 *
 * Boundaries:
 *   - A second Open on the same path appends rather than truncates.
 *   - The create mode is exactly 0600 regardless of umask (POSIX).
 *   - A symlinked final component is refused (POSIX).
 *   - An unopenable path (missing directory) -> Initialize returns false and
 *     the error text never contains the path.
 *
 * On failure first check:
 *   - FileSink::Open uses O_CREAT|O_NOFOLLOW with mode 0600 (POSIX) and sets a
 *     fixed "log file unavailable" error on failure.
 *   - LoggerFactory::Initialize opens the sink before mutating global state.
 */
#define BOOST_TEST_MODULE 04_file_sink
#include <boost/test/unit_test.hpp>

#include <filesystem>
#include <fstream>
#include <string>

#include <sys/stat.h>

#include "url_shortener/observability/file_sink.hpp"
#include "url_shortener/observability/logger.hpp"
#include "url_shortener/observability/logger_factory.hpp"
#include "url_shortener/observability/logging_config.hpp"

namespace obs = url_shortener::observability;
namespace fs = std::filesystem;

namespace
{

/// Returns a unique, nonexistent temp path for one test's log file.
fs::path unique_log_path(const std::string& stem)
{
    static int counter = 0;
    const fs::path dir = fs::temp_directory_path() / "url_shortener_file_sink";
    fs::create_directories(dir);
    return dir / (stem + std::to_string(++counter) + ".log");
}

/// Reads the whole file at @p path into a string (empty if missing).
std::string read_all(const fs::path& path)
{
    std::ifstream in(path, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(in)),
                       std::istreambuf_iterator<char>());
}

}  // namespace

BOOST_AUTO_TEST_CASE(open_writes_and_appends)
{
    const fs::path path = unique_log_path("writes");
    fs::remove(path);

    {
        std::string error;
        auto sink = obs::FileSink::Open(path.string(), error);
        BOOST_REQUIRE_MESSAGE(sink != nullptr, error);
        sink->Write("line one\n");
        sink->Write("line two\n");
    }
    // A second open on the same path must append, not truncate.
    {
        std::string error;
        auto sink = obs::FileSink::Open(path.string(), error);
        BOOST_REQUIRE_MESSAGE(sink != nullptr, error);
        sink->Write("line three\n");
    }

    const std::string contents = read_all(path);
    BOOST_CHECK_EQUAL(contents, "line one\nline two\nline three\n");
    fs::remove(path);
}

BOOST_AUTO_TEST_CASE(created_file_has_mode_0600)
{
    const fs::path path = unique_log_path("mode");
    fs::remove(path);

    std::string error;
    auto sink = obs::FileSink::Open(path.string(), error);
    BOOST_REQUIRE_MESSAGE(sink != nullptr, error);
    sink->Write("x\n");

    struct stat st {};
    BOOST_REQUIRE_EQUAL(::stat(path.string().c_str(), &st), 0);
    // Only the permission bits of a freshly created file are asserted.
    BOOST_CHECK_EQUAL(st.st_mode & 0777, 0600);
    fs::remove(path);
}

BOOST_AUTO_TEST_CASE(open_refuses_symlink_final_component)
{
    const fs::path target = unique_log_path("symlink_target");
    const fs::path link = unique_log_path("symlink_link");
    fs::remove(target);
    fs::remove(link);

    // Point a symlink at a real file; Open must refuse to follow it.
    { std::ofstream(target) << "pre-existing\n"; }
    fs::create_symlink(target, link);

    std::string error;
    auto sink = obs::FileSink::Open(link.string(), error);
    BOOST_CHECK(sink == nullptr);
    BOOST_CHECK(!error.empty());
    // The target content is untouched (the symlink was not followed/written).
    BOOST_CHECK_EQUAL(read_all(target), "pre-existing\n");

    fs::remove(link);
    fs::remove(target);
}

BOOST_AUTO_TEST_CASE(open_unopenable_path_fails_without_leaking_path)
{
    // A path under a directory that does not exist cannot be opened.
    const fs::path path =
        fs::temp_directory_path() / "url_shortener_no_such_dir_xyz" / "a.log";

    std::string error;
    auto sink = obs::FileSink::Open(path.string(), error);
    BOOST_CHECK(sink == nullptr);
    BOOST_CHECK(!error.empty());
    // The error text must never disclose the path or a path fragment.
    BOOST_CHECK(error.find(path.string()) == std::string::npos);
    BOOST_CHECK(error.find("url_shortener_no_such_dir_xyz") ==
                std::string::npos);
}

BOOST_AUTO_TEST_CASE(initialize_fails_closed_on_unopenable_file)
{
    const fs::path path =
        fs::temp_directory_path() / "url_shortener_no_such_dir_xyz" / "b.log";

    obs::LoggingConfig cfg;
    cfg.sink = obs::LogSink::File;
    cfg.file_path = path.string();

    std::string error;
    const bool ok = obs::LoggerFactory::Initialize(cfg, &error);
    BOOST_CHECK(!ok);
    BOOST_CHECK(!error.empty());
    // Fail-closed diagnostic is value-free: never the configured path.
    BOOST_CHECK(error.find(path.string()) == std::string::npos);
}

BOOST_AUTO_TEST_CASE(initialize_routes_log_lines_to_file)
{
    const fs::path path = unique_log_path("routed");
    fs::remove(path);

    obs::LoggingConfig cfg;
    cfg.sink = obs::LogSink::File;
    cfg.file_path = path.string();
    cfg.level = obs::LogLevel::Info;

    std::string error;
    BOOST_REQUIRE_MESSAGE(obs::LoggerFactory::Initialize(cfg, &error), error);

    obs::Logger logger = obs::LoggerFactory::Component("test");
    logger.info("routed_message");

    // Re-initialize to a stderr sink so the file handle is closed and flushed.
    obs::LoggingConfig off;
    off.sink = obs::LogSink::Stderr;
    BOOST_REQUIRE(obs::LoggerFactory::Initialize(off, &error));

    const std::string contents = read_all(path);
    BOOST_CHECK(contents.find("routed_message") != std::string::npos);
    BOOST_CHECK(contents.find("component=test") != std::string::npos);
    fs::remove(path);
}
