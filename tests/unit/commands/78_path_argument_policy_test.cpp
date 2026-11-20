/**
 * @file 78_path_argument_policy_test.cpp
 * @brief The `path` argument policy for `link.import` / `link.export`
 * (Milestone 0006, Task 07.0 subtask 20, threats T-95/T-96/T-98/T-99/T-118).
 *
 * Proves at the real choke point (`pair_file_access`): a confinement root is
 * enforced component-wise (so the `/foo` vs `/foobar` string-prefix trap is
 * refused), the capability is default-deny (`unsupported` when `allow_paths`
 * is false), special / device / NUL / over-long paths are rejected before any
 * read, a symlink that escapes the root is refused (POSIX), export is
 * create-new (never overwrites), CSV cells can never begin with a formula
 * character (safe by construction), and a read-only caller cannot export to a
 * `path` at all (the registry raises the path export to `write`). A sentinel
 * file outside the root is proven unmodified after a rejected escape - the real
 * proof of confinement, not the error code alone.
 */
#define BOOST_TEST_MODULE CommandsPathArgumentPolicyTest
#include <chrono>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <string>
#include <system_error>
#include <variant>
#include <vector>

#include <boost/test/unit_test.hpp>

#ifndef _WIN32
#    include <sys/stat.h>  // mkfifo
#endif

#include "commands_test_fakes.hpp"
#include "url_shortener/commands/command_registry.hpp"
#include "url_shortener/commands/envelope.hpp"
#include "url_shortener/commands/error_codes.hpp"
#include "url_shortener/commands/job_commands.hpp"
#include "url_shortener/commands/job_runner.hpp"
#include "url_shortener/commands/link_commands.hpp"
#include "url_shortener/commands/pair_commands.hpp"
#include "url_shortener/commands/pair_file_access.hpp"
#include "url_shortener/commands/pair_formatter.hpp"
#include "url_shortener/core/clock.hpp"
#include "url_shortener/core/types.hpp"
#include "url_shortener/security/control_set.hpp"

namespace fs = std::filesystem;

using url_shortener::jsonString;
using url_shortener::commands::AtomicFileSink;
using url_shortener::commands::CallerContext;
using url_shortener::commands::CommandErrorCode;
using url_shortener::commands::CommandRegistry;
using url_shortener::commands::CommandRequest;
using url_shortener::commands::CommandResponse;
using url_shortener::commands::format_csv_row;
using url_shortener::commands::JobRunner;
using url_shortener::commands::JobRunnerOptions;
using url_shortener::commands::LineSource;
using url_shortener::commands::LinkListFilter;
using url_shortener::commands::LinkPage;
using url_shortener::commands::LinkPageSource;
using url_shortener::commands::PairCommandDeps;
using url_shortener::commands::PathError;
using url_shortener::commands::PathPolicy;
using url_shortener::commands::register_job_commands;
using url_shortener::commands::register_link_commands;
using url_shortener::commands::register_pair_commands;

namespace app = url_shortener::app;

namespace
{

/// A per-test temp directory removed in the destructor.
struct TempDir
{
    fs::path path;

    TempDir()
        : path(fs::temp_directory_path()
               / ("path_policy_"
                  + std::to_string(std::chrono::steady_clock::now()
                                       .time_since_epoch()
                                       .count())))
    {
        fs::create_directories(path);
    }

    ~TempDir()
    {
        std::error_code ec;
        fs::remove_all(path, ec);
    }
};

PathPolicy allow_policy()
{
    PathPolicy policy;
    policy.allow_paths = true;
    return policy;
}

PathPolicy rooted_policy(const fs::path& root)
{
    PathPolicy policy;
    policy.allow_paths = true;
    policy.root = root.string();
    return policy;
}

void write_file(const fs::path& path, const std::string& content)
{
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out << content;
}

std::string read_file(const fs::path& path)
{
    std::ifstream in(path, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(in)),
                       std::istreambuf_iterator<char>());
}

bool is_rejected(const std::variant<LineSource, PathError>& result,
                 const std::string& code)
{
    return std::holds_alternative<PathError>(result)
        && std::get<PathError>(result).code == code;
}

app::LinkView make_view(const std::string& slug, const std::string& target)
{
    app::LinkView view;
    view.id = "id-" + slug;
    view.slug = slug;
    view.target_url = target;
    view.short_url = "http://sho.rt/" + slug;
    view.created_at = "2025-01-01T00:00:00Z";
    view.updated_at = "2025-01-01T00:00:00Z";
    view.enabled = true;
    view.redirect_type = url_shortener::RedirectType::temporary;
    return view;
}

}  // namespace

// Type: unit (root jail, component-wise, with a sentinel / T-96).
// Scenario: with a root <tmp>/r, an in-root file opens; `../x`, `a/../../x`, an
//   absolute path outside the root and the `/foo` vs `/foobar` string-prefix
//   sibling (<tmp>/rx/a.csv) are all refused invalid_field. A sentinel OUTSIDE
//   the root is unchanged after a rejected escaping export (the real proof the
//   file was never opened for write).
// On failure first check: did is_within_root switch to a string-prefix test, or
//   stop canonicalizing before the check?
BOOST_AUTO_TEST_CASE(path_root_policy)
{
    TempDir dir;
    const fs::path root = dir.path / "r";
    const fs::path sibling =
        dir.path / "rx";  // string-prefix trap: "r" vs "rx"
    fs::create_directories(root);
    fs::create_directories(sibling);

    const fs::path in_root = root / "a.csv";
    write_file(in_root, "http://a.example/,aaa\n");
    const fs::path in_sibling = sibling / "a.csv";
    write_file(in_sibling, "http://evil.example/,bbb\n");

    const PathPolicy policy = rooted_policy(root);

    // In-root read is allowed.
    BOOST_TEST(std::holds_alternative<LineSource>(
        LineSource::from_path(in_root.string(), policy)));

    // Escapes are refused invalid_field.
    BOOST_TEST(
        is_rejected(LineSource::from_path((root / ".." / "x").string(), policy),
                    "invalid_field"));
    BOOST_TEST(
        is_rejected(LineSource::from_path(
                        (root / "a" / ".." / ".." / "x").string(), policy),
                    "invalid_field"));
    BOOST_TEST(is_rejected(LineSource::from_path(in_sibling.string(), policy),
                           "invalid_field"));
    BOOST_TEST(is_rejected(
        LineSource::from_path((dir.path / "outside.csv").string(), policy),
        "invalid_field"));

    // Sentinel outside the root: a create-new export escaping to it is refused
    // and the sentinel content is provably untouched (file never opened).
    const fs::path sentinel = dir.path / "sentinel.txt";
    const std::string sentinel_body = "SENTINEL-UNIQUE-DO-NOT-TOUCH";
    write_file(sentinel, sentinel_body);

    const std::variant<AtomicFileSink, PathError> export_escape =
        AtomicFileSink::create_new(sentinel.string(), policy);
    BOOST_REQUIRE(std::holds_alternative<PathError>(export_escape));
    BOOST_TEST(std::get<PathError>(export_escape).code
               == std::string("invalid_field"));
    BOOST_TEST(read_file(sentinel) == sentinel_body);  // unmodified
}

// Type: unit (default-deny capability / T-95).
// Scenario: allow_paths=false -> both read (from_path) and write (create_new)
//   return `unsupported` with no filesystem access.
// On failure first check: did a factory branch on a transport name instead of
//   the capability flag?
BOOST_AUTO_TEST_CASE(path_disabled_policy)
{
    TempDir dir;
    const fs::path file = dir.path / "x.csv";
    write_file(file, "http://a.example/,aaa\n");

    PathPolicy denied;
    denied.allow_paths = false;

    BOOST_TEST(is_rejected(LineSource::from_path(file.string(), denied),
                           "unsupported"));

    const std::variant<AtomicFileSink, PathError> sink =
        AtomicFileSink::create_new((dir.path / "new.csv").string(), denied);
    BOOST_REQUIRE(std::holds_alternative<PathError>(sink));
    BOOST_TEST(std::get<PathError>(sink).code == std::string("unsupported"));
}

// Type: unit (special / device / non-regular targets rejected / T-95).
// Scenario: a directory and (POSIX) a FIFO and known device/proc files are
//   refused before any read; Windows device names are guarded by _WIN32.
// On failure first check: did the special-file guard stop running before the
//   open, or accept a non-regular file?
BOOST_AUTO_TEST_CASE(path_special_files_rejected)
{
    TempDir dir;

    // A directory is not a regular file.
    const fs::path subdir = dir.path / "adir";
    fs::create_directories(subdir);
    BOOST_TEST(
        is_rejected(LineSource::from_path(subdir.string(), allow_policy()),
                    "invalid_field"));

#ifndef _WIN32
    // A FIFO is not a regular file.
    const fs::path fifo = dir.path / "afifo";
    if (::mkfifo(fifo.c_str(), 0600) == 0) {
        BOOST_TEST(
            is_rejected(LineSource::from_path(fifo.string(), allow_policy()),
                        "invalid_field"));
    }

    // Character device and procfs, when present on this host.
    for (const char* special : {"/dev/zero", "/proc/self/environ"}) {
        if (fs::exists(special)) {
            const std::variant<LineSource, PathError> result =
                LineSource::from_path(special, allow_policy());
            BOOST_TEST(std::holds_alternative<PathError>(result),
                       std::string("expected rejection for ") + special);
        }
    }
#else
    // Windows reserved / device names are rejected by name.
    for (const char* name : {"NUL", "CON", "\\\\.\\COM1", "\\\\host\\share\\f"})
    {
        BOOST_TEST(is_rejected(LineSource::from_path(name, allow_policy()),
                               "invalid_field"));
    }
#endif
}

// Type: unit (symlink escape under a root is refused / T-96).
// Scenario: POSIX - a symlink inside the root that points outside is refused
//   invalid_field by canonicalization; skipped where symlinks are unavailable.
// On failure first check: did the rooted open stop resolving symlinks before
//   the component-wise check?
BOOST_AUTO_TEST_CASE(path_symlink_escape_rejected_under_root)
{
#ifndef _WIN32
    TempDir dir;
    const fs::path root = dir.path / "r";
    fs::create_directories(root);

    const fs::path outside = dir.path / "secret.csv";
    write_file(outside, "http://evil.example/,zzz\n");

    const fs::path link = root / "escape.csv";
    std::error_code ec;
    fs::create_symlink(outside, link, ec);
    if (ec) {
        BOOST_TEST_MESSAGE("symlink unavailable; skipping");
        return;
    }

    BOOST_TEST(
        is_rejected(LineSource::from_path(link.string(), rooted_policy(root)),
                    "invalid_field"));
#else
    BOOST_TEST_MESSAGE(
        "symlink escape test is POSIX-only; skipping on Windows");
#endif
}

// Type: unit (embedded NUL and over-long path rejected / T-95).
// Scenario: a path containing a NUL byte and a path longer than 4096 chars are
//   both refused invalid_field before any read.
// On failure first check: did the path string validation stop checking length
//   or NUL bytes?
BOOST_AUTO_TEST_CASE(path_nul_and_overlong)
{
    std::string with_nul = "/tmp/a";
    with_nul.push_back('\0');
    with_nul += "b.csv";
    BOOST_TEST(is_rejected(LineSource::from_path(with_nul, allow_policy()),
                           "invalid_field"));

    const std::string overlong = "/tmp/" + std::string(4100, 'a') + ".csv";
    BOOST_TEST(is_rejected(LineSource::from_path(overlong, allow_policy()),
                           "invalid_field"));
}

// Type: unit (export is create-new, never overwrites / T-99).
// Scenario: AtomicFileSink::create_new to an existing file is refused
//   invalid_field, the existing file is untouched, and no temp residue is left.
// On failure first check: did create_new stop using O_EXCL / the linkat EEXIST
//   single-committer?
BOOST_AUTO_TEST_CASE(export_path_never_overwrites)
{
    TempDir dir;
    const fs::path target = dir.path / "exists.csv";
    const std::string original = "ORIGINAL-CONTENT\n";
    write_file(target, original);

    const std::variant<AtomicFileSink, PathError> sink =
        AtomicFileSink::create_new(target.string(), allow_policy());
    BOOST_REQUIRE(std::holds_alternative<PathError>(sink));
    BOOST_TEST(std::get<PathError>(sink).code == std::string("invalid_field"));

    BOOST_TEST(read_file(target) == original);  // untouched

    // No temp artifact left behind.
    for (const auto& entry : fs::directory_iterator(dir.path)) {
        BOOST_TEST(
            entry.path().filename().string().find(".tmp-") == std::string::npos,
            "unexpected temp residue: " + entry.path().string());
    }
}

// Type: unit (CSV cells never start with a formula char / T-98, safe by
//   construction).
// Scenario: for a range of accepted targets and valid slugs, every emitted CSV
//   cell's first character is never one of = + - @ (so no spreadsheet executes
//   it), because targets are http(s):// and slugs match the slug pattern.
// On failure first check: did format_csv_row start emitting a raw leading
//   formula char, or did a cell pick up one from an un-escaped field?
BOOST_AUTO_TEST_CASE(export_cells_cannot_start_with_formula_chars)
{
    const std::vector<app::LinkView> views = {
        make_view("abc", "https://example.com/+plus"),
        make_view("a-b_c", "http://example.com/=eq"),
        make_view("Zz9", "https://example.com/@at"),
        make_view("slug123", "http://example.com/-dash"),
    };

    for (const app::LinkView& view : views) {
        const std::string row = format_csv_row(view);
        // Split the (single) CSV row into its comma-separated top-level cells.
        // The fields are http(s):// URLs and slug-pattern strings, so no cell
        // is quoted and a simple split on ',' is sufficient to inspect each
        // start.
        std::size_t start = 0;
        while (start <= row.size()) {
            const std::size_t comma = row.find(',', start);
            const std::size_t end =
                comma == std::string::npos ? row.find('\n', start) : comma;
            const std::string cell = row.substr(
                start, (end == std::string::npos ? row.size() : end) - start);
            if (!cell.empty()) {
                const char c = cell.front();
                BOOST_TEST((c != '=' && c != '+' && c != '-' && c != '@'),
                           "formula-leading cell in row: " + row);
            }
            if (comma == std::string::npos) {
                break;
            }
            start = comma + 1;
        }
    }
}

// Type: unit (read-only caller cannot export to a path / T-118, C7).
// Scenario: a ControlSet::User may run an inline export (read) but is denied a
//   `path` export with not_authorized before the job factory runs, so no file
//   is created - the path raise to `write` holds regardless of transport.
// On failure first check: did link.export lose its path->write permission
//   raise, or did the registry stop enforcing it before dispatch?
BOOST_AUTO_TEST_CASE(read_only_export_to_path_denied)
{
    TempDir dir;

    commands_test_fakes::FakeLinkStore store;
    commands_test_fakes::FakeStatsReader reader;
    ServerConfig config = commands_test_fakes::make_test_config();
    ManualClock clock(std::chrono::system_clock::time_point {});
    auto runner =
        std::make_shared<JobRunner>(clock, JobRunnerOptions {1u, 16u, 64u});
    auto service =
        std::make_shared<app::LinkCommandService>(store, reader, config);

    CommandRegistry registry;
    registry.set_job_submitter(runner);
    register_link_commands(registry, *service);
    register_job_commands(registry, *runner);

    PairCommandDeps deps;
    deps.service = service;
    deps.max_inline_bytes = 65536;
    deps.file_paths.allow_paths =
        true;  // capability present; authz still denies
    deps.list_page = [](const LinkListFilter&,
                        std::size_t,
                        std::size_t) -> app::Result<LinkPage>
    {
        app::Result<LinkPage> result;
        result.value = LinkPage {};
        return result;
    };
    register_pair_commands(registry, std::move(deps));

    CallerContext user;
    user.principal = "local-operator";
    user.transport = "test";
    user.control_set = ControlSet::User;

    const fs::path target = dir.path / "out.csv";
    CommandRequest request;
    request.id = "export-ro";
    request.command = "link.export";
    request.args_json =
        "{\"format\":\"csv\",\"path\":" + jsonString(target.string()) + "}";

    const CommandResponse response = registry.execute(user, request);
    BOOST_TEST(response.ok == false);
    BOOST_TEST((response.error.code == CommandErrorCode::not_authorized));
    BOOST_TEST(!response.job.has_value());
    BOOST_TEST(!fs::exists(target));  // no file created
}
