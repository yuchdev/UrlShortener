/**
 * @file 21_serve_stdio_dispatch.cpp
 * @brief Unit tests for the `serve-stdio` runtime wiring (Task 05.0 subtask 17):
 * @ref url_shortener::cli::RunServeStdioOn drives one NDJSON session over injected
 * streams against the one composition root, with the file-path capability and
 * caller role fixed by the parsed options, logs only to stderr, and leaves no
 * worker thread behind.
 *
 * Milestone 0006, Task 05.0 (threat model T-95, T-102, T-106, T-107, D14).
 */
#define BOOST_TEST_MODULE CliServeStdioDispatch
#include <chrono>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <memory>
#include <optional>
#include <sstream>
#include <streambuf>
#include <string>
#include <vector>

#include <boost/test/unit_test.hpp>

#include <url_shortener/app/link_backend.hpp>
#include <url_shortener/cli/cli_parser.hpp>
#include <url_shortener/cli/serve_stdio_dispatch.hpp>
#include <url_shortener/commands/command_registry.hpp>
#include <url_shortener/commands/envelope.hpp>
#include <url_shortener/commands/error_codes.hpp>
#include <url_shortener/commands/job_runner.hpp>
#include <url_shortener/commands/job_submitter.hpp>
#include <url_shortener/commands/job_types.hpp>
#include <url_shortener/composition/command_runtime.hpp>
#include <url_shortener/composition/job_runtime.hpp>
#include <url_shortener/composition/link_backend_bootstrap.hpp>
#include <url_shortener/core/config.hpp>
#include <url_shortener/observability/logger_factory.hpp>
#include <url_shortener/observability/logging_config.hpp>
#include <url_shortener/security/control_set.hpp>

namespace
{
namespace obs = url_shortener::observability;
namespace composition = url_shortener::composition;
namespace commands = url_shortener::commands;
namespace app = url_shortener::app;

using url_shortener::cli::RunServeStdioOn;

/// Resets the process-wide job runtime and link backend, and forces logging onto
/// stderr exactly as `RunServeStdio` does, so each case starts with a fresh
/// `j-1` job counter, an empty installed store and an audit trail that can never
/// reach the injected `out` (threat model T-102).
struct Fixture
{
    Fixture()
    {
        isolate();
        obs::LoggingConfig cfg;
        cfg.enabled = true;
        cfg.sink = obs::LogSink::Stderr;
        obs::LoggerFactory::Initialize(cfg);
    }

    ~Fixture() { isolate(); }

    static void isolate()
    {
        composition::reset_process_job_runtime_for_tests();
        app::LinkBackend::reset();
    }
};

/// Splits NDJSON output into lines (dropping the trailing empty segment after the
/// final newline).
std::vector<std::string> lines_of(const std::string& text)
{
    std::vector<std::string> lines;
    std::istringstream in(text);
    std::string line;
    while (std::getline(in, line)) {
        lines.push_back(line);
    }
    return lines;
}

/// Runs one `serve-stdio` session over the fixed input and returns the written
/// output and exit code. Uses a default @ref ServerConfig and, by default, an
/// inactive storage handle (the legacy in-memory wiring every unit test here
/// relies on); a case may pass an active handle to exercise the configured
/// backend.
std::string run_session(const ServeStdioCliOptions& options,
                        const std::string& input, int& rc_out,
                        const app::LinkBackendHandle& backend =
                            app::LinkBackendHandle{})
{
    const ServerConfig config;
    std::istringstream in(input);
    std::ostringstream out;
    rc_out = RunServeStdioOn(config, options, backend, in, out);
    return out.str();
}

/// A one request-per-line NDJSON request envelope.
std::string envelope(const std::string& id, const std::string& command,
                     const std::string& args_json)
{
    return R"({"v":1,"id":")" + id + R"(","command":")" + command
        + R"(","args":)" + args_json + "}\n";
}

bool contains(const std::string& haystack, const std::string& needle)
{
    return haystack.find(needle) != std::string::npos;
}

/// Counts non-overlapping occurrences of @p needle in @p haystack.
std::size_t count_of(const std::string& haystack, const std::string& needle)
{
    std::size_t n = 0;
    std::size_t at = 0;
    while ((at = haystack.find(needle, at)) != std::string::npos) {
        ++n;
        at += needle.size();
    }
    return n;
}

/// RAII redirect of a std::ostream (std::cerr/std::cout) into a captured buffer.
class StreamCapture
{
public:
    explicit StreamCapture(std::ostream& target)
        : m_target(target), m_previous(target.rdbuf(m_buffer.rdbuf()))
    {
    }

    ~StreamCapture() { m_target.rdbuf(m_previous); }

    StreamCapture(const StreamCapture&) = delete;
    StreamCapture& operator=(const StreamCapture&) = delete;

    std::string str() const { return m_buffer.str(); }

private:
    std::ostream& m_target;
    std::ostringstream m_buffer;
    std::streambuf* m_previous;
};

/// An input streambuf that serves `prefix`, invokes `on_gap` once when the reader
/// first asks for more input, then serves `suffix`, then reports EOF. Lets one
/// NDJSON session run a synchronization step (here: waiting for a submitted job
/// to finish) deterministically between two request lines, without any sleep.
class GatedInBuf final : public std::streambuf
{
public:
    GatedInBuf(std::string prefix, std::function<void()> on_gap,
               std::string suffix)
        : m_prefix(std::move(prefix)), m_on_gap(std::move(on_gap)),
          m_suffix(std::move(suffix))
    {
    }

protected:
    int_type underflow() override
    {
        if (gptr() != nullptr && gptr() < egptr()) {
            return traits_type::to_int_type(*gptr());
        }
        while (m_stage < 2) {
            const int stage = m_stage++;
            if (stage == 0) {
                set_area(m_prefix);
            }
            else {
                if (m_on_gap) {
                    m_on_gap();
                }
                set_area(m_suffix);
            }
            if (gptr() != nullptr && gptr() < egptr()) {
                return traits_type::to_int_type(*gptr());
            }
        }
        return traits_type::eof();
    }

private:
    void set_area(std::string& segment)
    {
        char* begin = segment.empty() ? nullptr : &segment[0];
        setg(begin, begin, begin + segment.size());
    }

    std::string m_prefix;
    std::function<void()> m_on_gap;
    std::string m_suffix;
    int m_stage = 0;
};

commands::CallerContext admin_caller()
{
    commands::CallerContext caller;
    caller.principal = "local-operator";
    caller.transport = "stdio";
    caller.control_set = ControlSet::Admin;
    return caller;
}

}  // namespace

// Type: unit (one composition root; no mode-specific registration, C11).
// Scenario: command.list over a serve-stdio session equals command.list executed
//   on a runtime built directly from build_command_runtime for the same config.
// Boundaries: identical result_json (same names, same order).
// On failure first check: whether RunServeStdioOn builds the registry through a
//   different path than build_command_runtime (it must not).
BOOST_FIXTURE_TEST_CASE(command_list_matches_composition_root, Fixture)
{
    const ServerConfig config;
    const composition::CommandRuntime reference =
        composition::build_command_runtime(config, app::LinkBackendHandle{});
    commands::CommandRequest list_request;
    list_request.v = 1;
    list_request.id = "ref";
    list_request.command = "command.list";
    list_request.args_json = "{}";
    const commands::CommandResponse reference_response =
        reference.registry->execute(admin_caller(), list_request);
    BOOST_REQUIRE(reference_response.ok);

    int rc = -1;
    const std::string out =
        run_session(ServeStdioCliOptions{},
                    envelope("a", "command.list", "{}"), rc);
    BOOST_TEST(rc == 0);
    const std::vector<std::string> out_lines = lines_of(out);
    BOOST_REQUIRE(out_lines.size() == 1u);
    const commands::parse_response_result parsed =
        commands::parse_response(out_lines.front());
    BOOST_REQUIRE(parsed.response.has_value());
    BOOST_TEST(parsed.response->ok);
    BOOST_CHECK_EQUAL(parsed.response->result_json,
                      reference_response.result_json);
}

// Type: unit (one store shared across the session).
// Scenario: link.create then link.get in a single session resolve the same link.
// Boundaries: the second response's result echoes the created slug.
// On failure first check: did RunServeStdioOn bind the link service to a store
//   other than the process-wide one?
BOOST_FIXTURE_TEST_CASE(create_then_get_same_session, Fixture)
{
    const std::string slug = "ss21-create";
    int rc = -1;
    const std::string out = run_session(
        ServeStdioCliOptions{},
        envelope("c", "link.create",
                 R"({"target_url":"https://example.com/)" + slug
                     + R"(","slug":")" + slug + R"("})")
            + envelope("g", "link.get", R"({"slug":")" + slug + R"("})"),
        rc);

    BOOST_TEST(rc == 0);
    const std::vector<std::string> out_lines = lines_of(out);
    BOOST_REQUIRE(out_lines.size() == 2u);
    const commands::parse_response_result created =
        commands::parse_response(out_lines[0]);
    const commands::parse_response_result got =
        commands::parse_response(out_lines[1]);
    BOOST_REQUIRE(created.response.has_value() && got.response.has_value());
    BOOST_TEST(created.response->ok);
    BOOST_TEST(got.response->ok);
    BOOST_TEST(contains(got.response->result_json, slug));
}

// Type: unit (inline import -> job -> job.get succeeded, within one session).
// Scenario: link.import with inline CSV returns a job; a later job.get in the same
//   session reports it succeeded. A gated input stream waits for the job to reach
//   a terminal state between the two lines, so the assertion is deterministic.
// Boundaries: first response is a job envelope; job j-1 reaches state succeeded.
// On failure first check: did the import return a job (not a sync result), and is
//   the process-wide runner shared so the worker runs during the session?
BOOST_FIXTURE_TEST_CASE(inline_import_job_then_get_succeeded, Fixture)
{
    // Hold the process-wide runner so the gated input can wait on it; its first
    // job is j-1 (the fixture reset the counter).
    const std::shared_ptr<commands::JobRunner> runner =
        composition::process_job_runner();

    const std::string import_line = envelope(
        "imp", "link.import",
        R"({"format":"csv","data":"full_url,slug\nhttps://example.com/ss21imp,ss21imp\n"})");
    const std::string job_get_line =
        envelope("jg", "job.get", R"({"id":"j-1"})");

    GatedInBuf buf(import_line,
                   [&] { runner->wait_idle(std::chrono::seconds(20)); },
                   job_get_line);
    std::istream in(&buf);
    std::ostringstream out;
    const ServerConfig config;
    const int rc = RunServeStdioOn(config, ServeStdioCliOptions{},
                                   app::LinkBackendHandle{}, in, out);

    BOOST_TEST(rc == 0);
    const std::vector<std::string> out_lines = lines_of(out.str());
    BOOST_REQUIRE(out_lines.size() == 2u);
    BOOST_TEST(contains(out_lines[0], "\"job\""));
    BOOST_TEST(contains(out_lines[0], "j-1"));
    BOOST_TEST(contains(out_lines[1], "\"state\":\"succeeded\""));
}

// Type: unit (db.* allowlist wired into the one runtime, C6).
// Scenario: db.tables lists links, click_events and jobs.
// Boundaries: all three allowlisted table names appear.
// On failure first check: did the composition root stop joining the jobs table
//   source into the catalog?
BOOST_FIXTURE_TEST_CASE(db_tables_lists_links_click_events_jobs, Fixture)
{
    int rc = -1;
    const std::string out =
        run_session(ServeStdioCliOptions{}, envelope("t", "db.tables", "{}"),
                    rc);
    BOOST_TEST(rc == 0);
    const std::vector<std::string> out_lines = lines_of(out);
    BOOST_REQUIRE(out_lines.size() == 1u);
    BOOST_TEST(contains(out_lines.front(), R"("name":"links")"));
    BOOST_TEST(contains(out_lines.front(), R"("name":"click_events")"));
    BOOST_TEST(contains(out_lines.front(), R"("name":"jobs")"));
}

// Type: unit (read-only downgrade to User, T-107/C7).
// Scenario: with --read-only the local caller is User, so a write command is
//   not_authorized; the role comes from the option, not the stream.
// Boundaries: link.create -> not_authorized.
// On failure first check: did RunServeStdioOn pass read_only into TransportOptions
//   (which selects ControlSet::User)?
BOOST_FIXTURE_TEST_CASE(read_only_denies_write, Fixture)
{
    ServeStdioCliOptions options;
    options.read_only = true;
    int rc = -1;
    const std::string out = run_session(
        options,
        envelope("c", "link.create",
                 R"({"target_url":"https://example.com/ss21ro","slug":"ss21ro"})"),
        rc);
    BOOST_TEST(rc == 0);
    const std::vector<std::string> out_lines = lines_of(out);
    BOOST_REQUIRE(out_lines.size() == 1u);
    const commands::parse_response_result parsed =
        commands::parse_response(out_lines.front());
    BOOST_REQUIRE(parsed.response.has_value());
    BOOST_TEST((parsed.response->error.code
                == commands::CommandErrorCode::not_authorized));
}

// Type: unit (file capability default-deny by option, T-95).
// Scenario: with --no-file-paths, link.import naming a path is refused with
//   unsupported before any filesystem access.
// Boundaries: link.import {path} -> unsupported.
// On failure first check: did RunServeStdioOn set file_paths.allow_paths =
//   !no_file_paths?
BOOST_FIXTURE_TEST_CASE(no_file_paths_refuses_path, Fixture)
{
    ServeStdioCliOptions options;
    options.no_file_paths = true;
    int rc = -1;
    const std::string out = run_session(
        options,
        envelope("i", "link.import",
                 R"({"format":"csv","path":"/tmp/ss21-nope.csv"})"),
        rc);
    BOOST_TEST(rc == 0);
    const std::vector<std::string> out_lines = lines_of(out);
    BOOST_REQUIRE(out_lines.size() == 1u);
    const commands::parse_response_result parsed =
        commands::parse_response(out_lines.front());
    BOOST_REQUIRE(parsed.response.has_value());
    BOOST_TEST((parsed.response->error.code
                == commands::CommandErrorCode::unsupported));
}

// Type: unit (path confinement to --path-root, T-95/T-96).
// Scenario: a path outside the root is invalid_field; a real CSV file inside the
//   root is accepted (the import returns a job).
// Boundaries: out-of-root -> invalid_field; in-root existing file -> ok (job).
// On failure first check: did RunServeStdioOn set file_paths.root = path_root?
BOOST_FIXTURE_TEST_CASE(path_root_confines_paths, Fixture)
{
    namespace fs = std::filesystem;
    const fs::path root = fs::temp_directory_path()
        / ("ss21_root_"
           + std::to_string(
               std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directories(root);
    const fs::path inside = root / "data.csv";
    {
        std::ofstream f(inside, std::ios::binary);
        f << "full_url,slug\nhttps://example.com/ss21pr,ss21pr\n";
    }
    const fs::path outside =
        fs::temp_directory_path() / "ss21_outside.csv";
    {
        std::ofstream f(outside, std::ios::binary);
        f << "full_url,slug\n";
    }

    ServeStdioCliOptions options;
    options.path_root = root.string();

    // JSON-quote the paths (POSIX temp dirs contain no backslashes to escape).
    const std::string outside_args =
        R"({"format":"csv","path":")" + outside.string() + R"("})";
    const std::string inside_args =
        R"({"format":"csv","path":")" + inside.string() + R"("})";

    int rc = -1;
    const std::string out =
        run_session(options,
                    envelope("out", "link.import", outside_args)
                        + envelope("in", "link.import", inside_args),
                    rc);

    const std::vector<std::string> out_lines = lines_of(out);
    BOOST_REQUIRE(out_lines.size() == 2u);
    const commands::parse_response_result out_resp =
        commands::parse_response(out_lines[0]);
    const commands::parse_response_result in_resp =
        commands::parse_response(out_lines[1]);
    BOOST_REQUIRE(out_resp.response.has_value() && in_resp.response.has_value());
    BOOST_TEST((out_resp.response->error.code
                == commands::CommandErrorCode::invalid_field));
    BOOST_TEST(in_resp.response->ok);

    fs::remove(inside);
    fs::remove(outside);
    fs::remove(root);
}

// Type: unit (invalid --path-root fails before reading input, exit 2).
// Scenario: a nonexistent --path-root exits 2 with one stderr line and reads no
//   input (the input would otherwise create a link).
// Boundaries: rc == 2; stdout empty; no link created.
// On failure first check: the is_directory guard at the top of RunServeStdioOn.
BOOST_FIXTURE_TEST_CASE(nonexistent_path_root_exits_two_before_read, Fixture)
{
    ServeStdioCliOptions options;
    options.path_root = "/this/path/definitely/does/not/exist/ss21";

    StreamCapture cerr_capture(std::cerr);
    int rc = -1;
    const std::string out = run_session(
        options,
        envelope("c", "link.create",
                 R"({"target_url":"https://example.com/ss21pr2","slug":"ss21pr2"})"),
        rc);

    BOOST_TEST(rc == 2);
    BOOST_TEST(out.empty());  // no response written: input was never read
    BOOST_TEST(!cerr_capture.str().empty());  // one diagnostic line
}

// Type: unit (D14: audit to stderr only; wire carries envelopes only, T-102).
// Scenario: a successful mutating link.create writes exactly one command_audit
//   line to the captured stderr and nothing but its response envelope to out; the
//   real std::cout is untouched (no log leaks onto the wire).
// Boundaries: exactly one `msg=command_audit` on stderr; out holds one response;
//   std::cout empty.
// On failure first check: was the logger sink forced to stderr before the runtime
//   was built, and does the adapter write only envelopes to out?
BOOST_FIXTURE_TEST_CASE(audit_goes_to_stderr_not_the_wire, Fixture)
{
    StreamCapture cout_capture(std::cout);
    StreamCapture cerr_capture(std::cerr);

    int rc = -1;
    const std::string out = run_session(
        ServeStdioCliOptions{},
        envelope("c", "link.create",
                 R"({"target_url":"https://example.com/ss21audit","slug":"ss21audit"})"),
        rc);

    BOOST_TEST(rc == 0);
    const std::vector<std::string> out_lines = lines_of(out);
    BOOST_REQUIRE(out_lines.size() == 1u);
    const commands::parse_response_result parsed =
        commands::parse_response(out_lines.front());
    BOOST_REQUIRE(parsed.response.has_value());
    BOOST_TEST(parsed.response->ok);

    // Exactly one audit line, and it is on stderr, never on the wire.
    const std::string err = cerr_capture.str();
    BOOST_TEST(count_of(err, "msg=command_audit") == 1u);
    BOOST_TEST(!contains(out, "command_audit"));
    BOOST_TEST(cout_capture.str().empty());

    // serve_stdio_start carries version + backend names; serve_stdio_stop counts
    // the one response written (both value-free, stderr only).
    BOOST_TEST(contains(err, "msg=serve_stdio_start"));
    BOOST_TEST(contains(err, "version="));
    BOOST_TEST(contains(err, "metadata_backend="));
    BOOST_TEST(contains(err, "msg=serve_stdio_stop"));
    BOOST_TEST(contains(err, "responses=1"));
}

// Type: unit (no worker thread survives the session; the runner is shut down).
// Scenario: after a session that ran an import job, the process-wide runner is
//   shut down: a fresh submit is rejected (no job), proving shutdown joined the
//   workers before RunServeStdioOn returned.
// Boundaries: submit after the session yields no job.
// On failure first check: did RunServeStdioOn call shutdown_process_job_runtime
//   after the adapter returned?
BOOST_FIXTURE_TEST_CASE(runner_is_shut_down_after_session, Fixture)
{
    const std::shared_ptr<commands::JobRunner> runner =
        composition::process_job_runner();

    int rc = -1;
    const std::string out = run_session(
        ServeStdioCliOptions{},
        envelope("imp", "link.import",
                 R"({"format":"csv","data":"full_url,slug\nhttps://example.com/ss21sd,ss21sd\n"})"),
        rc);
    BOOST_TEST(rc == 0);
    BOOST_TEST(contains(out, "\"job\""));

    // The same process-wide runner is now shut down: submit returns busy (no job).
    const commands::JobSubmitResult rejected = runner->submit(
        "noop", "digest",
        [](commands::IJobContext&) {
            commands::JobOutcome outcome;
            outcome.ok = true;
            return outcome;
        });
    BOOST_TEST(!rejected.job.has_value());
}

// Type: unit (active --storage-config handle -> no split store, security HIGH).
// Scenario: with an ACTIVE SQLite handle threaded in, link.create then link.list
//   in one session returns the created link: the browse lister reads the same
//   configured repository the create wrote through.
// Boundaries: with the old inactive-handle hardcode the lister read the empty
//   legacy repository and link.list would omit the link.
// On failure first check: did RunServeStdioOn pass the given backend handle (not
//   LinkBackendHandle{}) into build_command_runtime?
BOOST_FIXTURE_TEST_CASE(active_handle_lister_sees_created_link, Fixture)
{
    namespace fs = std::filesystem;
    const std::string tag = std::to_string(
        std::chrono::steady_clock::now().time_since_epoch().count());
    const fs::path db = fs::temp_directory_path() / ("ss21_active_" + tag + ".db");
    const fs::path yaml =
        fs::temp_directory_path() / ("ss21_active_" + tag + ".yaml");
    fs::remove(db);
    {
        std::ofstream f(yaml, std::ios::binary);
        f << "metadata:\n  backend: sqlite\n  sqlite_path: " << db.string()
          << "\n";
    }

    ServerConfig config;
    config.storage_config_path = yaml.string();

    const std::string slug = "ss21-active";
    {
        app::LinkBackend::reset();
        app::LinkBackendHandle backend = app::InitLinkBackend(config);
        BOOST_REQUIRE(backend.active());
        BOOST_REQUIRE(backend.repository() != nullptr);

        std::istringstream in(
            envelope("c", "link.create",
                     R"({"target_url":"https://example.com/)" + slug
                         + R"(","slug":")" + slug + R"("})")
            + envelope("l", "link.list", "{}"));
        std::ostringstream out;
        const int rc =
            RunServeStdioOn(config, ServeStdioCliOptions{}, backend, in, out);

        BOOST_TEST(rc == 0);
        const std::vector<std::string> out_lines = lines_of(out.str());
        BOOST_REQUIRE(out_lines.size() == 2u);
        const commands::parse_response_result created =
            commands::parse_response(out_lines[0]);
        const commands::parse_response_result listed =
            commands::parse_response(out_lines[1]);
        BOOST_REQUIRE(created.response.has_value()
                      && listed.response.has_value());
        BOOST_TEST(created.response->ok);
        BOOST_TEST(listed.response->ok);
        BOOST_TEST(contains(listed.response->result_json, slug));
    }  // the handle destructs here (flusher stopped) before reset below

    app::LinkBackend::reset();
    fs::remove(db);
    fs::remove(yaml);
}
