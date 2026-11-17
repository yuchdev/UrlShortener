#define BOOST_TEST_MODULE CommandsPairFileAccessTest
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include <boost/test/unit_test.hpp>

#ifndef _WIN32
#    include <sys/stat.h>
#endif

#include "url_shortener/commands/pair_file_access.hpp"
#include "url_shortener/commands/pair_line_parser.hpp"

namespace fs = std::filesystem;

using url_shortener::commands::AtomicFileSink;
using url_shortener::commands::kMaxImportFileBytes;
using url_shortener::commands::kMaxImportLineBytes;
using url_shortener::commands::line_status;
using url_shortener::commands::LineSource;
using url_shortener::commands::PathError;
using url_shortener::commands::PathPolicy;

namespace
{

/// A capability that permits local paths with no root jail.
PathPolicy allow_policy()
{
    PathPolicy policy;
    policy.allow_paths = true;
    return policy;
}

/// A capability that permits local paths confined to @p root.
PathPolicy rooted_policy(const fs::path& root)
{
    PathPolicy policy;
    policy.allow_paths = true;
    policy.root = root.string();
    return policy;
}

/// Writes @p content to @p path, truncating.
void write_file(const fs::path& path, std::string_view content)
{
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out.write(content.data(), static_cast<std::streamsize>(content.size()));
}

/// Reads @p path back as raw bytes.
std::string read_file(const fs::path& path)
{
    std::ifstream in(path, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(in)),
                       std::istreambuf_iterator<char>());
}

/// Drains a line source into a vector of lines, stopping at eof/error.
std::vector<std::string> drain(LineSource& src)
{
    std::vector<std::string> lines;
    std::string line;
    for (;;) {
        const line_status st = src.next(line, kMaxImportLineBytes);
        if (st == line_status::line || st == line_status::too_long) {
            lines.push_back(line);
            continue;
        }
        break;
    }
    return lines;
}

/// True when @p dir holds any temp artifact (name contains ".tmp-").
bool has_temp_file(const fs::path& dir)
{
    for (const auto& entry : fs::directory_iterator(dir)) {
        if (entry.path().filename().string().find(".tmp-") != std::string::npos)
        {
            return true;
        }
    }
    return false;
}

/// Per-test temp directory, removed recursively on teardown.
struct TempDirFixture
{
    fs::path dir;

    TempDirFixture()
    {
        static std::atomic<unsigned> seq {0};
        const auto now =
            std::chrono::steady_clock::now().time_since_epoch().count();
        dir = fs::temp_directory_path()
            / ("pfa_" + std::to_string(now) + "_"
               + std::to_string(seq.fetch_add(1)));
        fs::create_directories(dir);
    }

    ~TempDirFixture()
    {
        std::error_code ec;
        fs::remove_all(dir, ec);
    }
};

}  // namespace

// ---------------------------------------------------------------------------
// Reading
// ---------------------------------------------------------------------------

// Type: unit (I/O boundary).
// Scenario: a plain LF file yields its lines in order then eof.
// Boundaries: trailing newline; three lines.
// On failure first check: read_bounded_line's newline handling.
BOOST_FIXTURE_TEST_CASE(read_normal_file, TempDirFixture)
{
    const fs::path p = dir / "a.csv";
    write_file(p, "one\ntwo\nthree\n");
    auto r = LineSource::from_path(p.string(), allow_policy());
    BOOST_REQUIRE(std::holds_alternative<LineSource>(r));
    LineSource src = std::get<LineSource>(std::move(r));
    const std::vector<std::string> lines = drain(src);
    BOOST_REQUIRE_EQUAL(lines.size(), 3U);
    BOOST_TEST(lines[0] == "one");
    BOOST_TEST(lines[2] == "three");
}

// Type: unit.
// Scenario: CRLF terminators parse like LF (the CR is stripped, uncounted).
// Boundaries: \r\n on every line.
// On failure first check: the CRLF peek branch in read_bounded_line.
BOOST_FIXTURE_TEST_CASE(read_crlf_file, TempDirFixture)
{
    const fs::path p = dir / "a.csv";
    write_file(p, "one\r\ntwo\r\n");
    auto r = LineSource::from_path(p.string(), allow_policy());
    LineSource src = std::get<LineSource>(std::move(r));
    const std::vector<std::string> lines = drain(src);
    BOOST_REQUIRE_EQUAL(lines.size(), 2U);
    BOOST_TEST(lines[0] == "one");
    BOOST_TEST(lines[1] == "two");
}

// Type: unit.
// Scenario: a file whose last line lacks a newline still yields that line.
// Boundaries: no terminator on the final line.
// On failure first check: the saw_byte/final-line branch of read_bounded_line.
BOOST_FIXTURE_TEST_CASE(read_last_line_without_newline, TempDirFixture)
{
    const fs::path p = dir / "a.csv";
    write_file(p, "one\ntwo");
    auto r = LineSource::from_path(p.string(), allow_policy());
    LineSource src = std::get<LineSource>(std::move(r));
    const std::vector<std::string> lines = drain(src);
    BOOST_REQUIRE_EQUAL(lines.size(), 2U);
    BOOST_TEST(lines[1] == "two");
}

// Type: unit.
// Scenario: an empty file yields eof immediately with no line.
// Boundaries: zero bytes.
// On failure first check: the eof-on-no-byte branch.
BOOST_FIXTURE_TEST_CASE(read_empty_file, TempDirFixture)
{
    const fs::path p = dir / "a.csv";
    write_file(p, "");
    auto r = LineSource::from_path(p.string(), allow_policy());
    LineSource src = std::get<LineSource>(std::move(r));
    std::string line;
    BOOST_TEST((src.next(line, kMaxImportLineBytes) == line_status::eof));
}

// Type: unit (resource bound).
// Scenario: a line exactly at the cap is accepted; one byte over is too_long
// and the following line is still readable.
// Boundaries: max and max+1 bytes.
// On failure first check: the overflow handling and recovery in next().
BOOST_FIXTURE_TEST_CASE(line_cap_boundary, TempDirFixture)
{
    const fs::path p = dir / "a.csv";
    write_file(p, "12345678\n123456789\nafter\n");
    auto r = LineSource::from_path(p.string(), allow_policy());
    LineSource src = std::get<LineSource>(std::move(r));
    std::string line;
    BOOST_TEST((src.next(line, 8) == line_status::line));
    BOOST_TEST(line == "12345678");
    BOOST_TEST((src.next(line, 8) == line_status::too_long));
    BOOST_TEST((src.next(line, 8) == line_status::line));
    BOOST_TEST(line == "after");
}

// Type: unit (success criterion 2: bounded memory on a huge single line).
// Scenario: a 1 MiB single-line file is reported too_long without growing the
// line buffer past 2x the per-line cap.
// Boundaries: single line far larger than kMaxImportLineBytes.
// On failure first check: did overflow stop storing into the line string?
BOOST_FIXTURE_TEST_CASE(huge_single_line_bounded, TempDirFixture)
{
    const fs::path p = dir / "big.csv";
    write_file(p, std::string(1024 * 1024, 'x'));
    auto r = LineSource::from_path(p.string(), allow_policy());
    LineSource src = std::get<LineSource>(std::move(r));
    std::string line;
    BOOST_TEST((src.next(line, kMaxImportLineBytes) == line_status::too_long));
    BOOST_TEST(line.size() == kMaxImportLineBytes);
    BOOST_TEST(line.capacity() < 2 * kMaxImportLineBytes);
}

// Type: unit (resource bound).
// Scenario: a file one byte over kMaxImportFileBytes is rejected (sparse, so no
// 64 MiB is written).
// Boundaries: kMaxImportFileBytes + 1.
// On failure first check: the file_size cap in from_path.
BOOST_FIXTURE_TEST_CASE(file_over_cap_rejected, TempDirFixture)
{
    const fs::path p = dir / "big.csv";
    write_file(p, "x");
    std::error_code ec;
    fs::resize_file(p, kMaxImportFileBytes + 1, ec);
    BOOST_REQUIRE(!ec);
    auto r = LineSource::from_path(p.string(), allow_policy());
    BOOST_REQUIRE(std::holds_alternative<PathError>(r));
    BOOST_TEST(std::get<PathError>(r).code == "invalid_field");
}

// Type: unit.
// Scenario: a missing file is not_found; a directory is invalid_field.
// Boundaries: non-existent vs. non-regular.
// On failure first check: the status/exists mapping.
BOOST_FIXTURE_TEST_CASE(missing_and_directory, TempDirFixture)
{
    auto miss =
        LineSource::from_path((dir / "nope.csv").string(), allow_policy());
    BOOST_REQUIRE(std::holds_alternative<PathError>(miss));
    BOOST_TEST(std::get<PathError>(miss).code == "not_found");

    auto d = LineSource::from_path(dir.string(), allow_policy());
    BOOST_REQUIRE(std::holds_alternative<PathError>(d));
    BOOST_TEST(std::get<PathError>(d).code == "invalid_field");
}

// Type: unit (input validation).
// Scenario: empty path, embedded NUL, and a 4097-byte path are all rejected.
// Boundaries: length 0, a control byte, length 4097.
// On failure first check: validate_path_string.
BOOST_FIXTURE_TEST_CASE(malformed_path_strings, TempDirFixture)
{
    auto empty = LineSource::from_path("", allow_policy());
    BOOST_TEST(std::get<PathError>(empty).code == "invalid_field");

    std::string with_nul = (dir / "a").string();
    with_nul.push_back('\0');
    with_nul += "b";
    auto nul = LineSource::from_path(with_nul, allow_policy());
    BOOST_TEST(std::get<PathError>(nul).code == "invalid_field");

    const std::string long_path(4097, 'a');
    auto lp = LineSource::from_path(long_path, allow_policy());
    BOOST_TEST(std::get<PathError>(lp).code == "invalid_field");
}

// Type: unit.
// Scenario: inline data exactly at max_bytes is accepted; one byte over is
// rejected.
// Boundaries: max_bytes and max_bytes+1.
// On failure first check: from_inline's size guard.
BOOST_AUTO_TEST_CASE(inline_size_boundary)
{
    auto ok = LineSource::from_inline(std::string(10, 'a'), 10);
    BOOST_REQUIRE(std::holds_alternative<LineSource>(ok));

    auto over = LineSource::from_inline(std::string(11, 'a'), 10);
    BOOST_REQUIRE(std::holds_alternative<PathError>(over));
    BOOST_TEST(std::get<PathError>(over).code == "invalid_field");
}

#ifndef _WIN32
// Type: unit (POSIX special files).
// Scenario: a FIFO and a character device are rejected; a symlink to a regular
// file is accepted (rootless local operator).
// Boundaries: non-regular targets vs. a benign symlink.
// On failure first check: is_regular_file after symlink resolution.
BOOST_FIXTURE_TEST_CASE(posix_special_files, TempDirFixture)
{
    const fs::path fifo = dir / "f.fifo";
    BOOST_REQUIRE_EQUAL(::mkfifo(fifo.c_str(), 0600), 0);
    auto f = LineSource::from_path(fifo.string(), allow_policy());
    BOOST_TEST(std::get<PathError>(f).code == "invalid_field");

    auto dev = LineSource::from_path("/dev/zero", allow_policy());
    BOOST_TEST(std::get<PathError>(dev).code == "invalid_field");

    const fs::path real = dir / "real.csv";
    write_file(real, "hello\n");
    const fs::path link = dir / "link.csv";
    fs::create_symlink(real, link);
    auto s = LineSource::from_path(link.string(), allow_policy());
    BOOST_REQUIRE(std::holds_alternative<LineSource>(s));
    LineSource src = std::get<LineSource>(std::move(s));
    BOOST_TEST(drain(src).size() == 1U);
}
#endif

#ifdef _WIN32
// Type: unit (Windows path rejection).
// Scenario: UNC, device-namespace, and reserved device names are rejected with
// no network or device access.
// Boundaries: \\host\share, \\.\pipe\x, CON.
// On failure first check: the Windows branch of validate_path_string.
BOOST_FIXTURE_TEST_CASE(windows_special_paths, TempDirFixture)
{
    for (const char* bad : {"\\\\host\\share\\f", "\\\\.\\pipe\\x", "CON"}) {
        auto r = LineSource::from_path(bad, allow_policy());
        BOOST_REQUIRE(std::holds_alternative<PathError>(r));
        BOOST_TEST(std::get<PathError>(r).code == "invalid_field");
    }
}
#endif

// ---------------------------------------------------------------------------
// Writing
// ---------------------------------------------------------------------------

// Type: unit (export happy path).
// Scenario: create_new writes a temp, commit publishes it, content matches.
// Boundaries: fresh target.
// On failure first check: write() / commit() rename-or-link.
BOOST_FIXTURE_TEST_CASE(write_create_new_commit, TempDirFixture)
{
    const fs::path p = dir / "out.csv";
    auto r = AtomicFileSink::create_new(p.string(), allow_policy());
    BOOST_REQUIRE(std::holds_alternative<AtomicFileSink>(r));
    AtomicFileSink sink = std::get<AtomicFileSink>(std::move(r));
    BOOST_TEST(sink.write("hello,world\n"));
    BOOST_TEST(sink.commit());
    BOOST_TEST(read_file(p) == "hello,world\n");
    BOOST_TEST(!has_temp_file(dir));
}

// Type: unit (no-overwrite).
// Scenario: create_new on an existing target is invalid_field and leaves the
// file untouched.
// Boundaries: target already present.
// On failure first check: the existence guard in create_new.
BOOST_FIXTURE_TEST_CASE(write_existing_target_rejected, TempDirFixture)
{
    const fs::path p = dir / "out.csv";
    write_file(p, "original");
    auto r = AtomicFileSink::create_new(p.string(), allow_policy());
    BOOST_REQUIRE(std::holds_alternative<PathError>(r));
    BOOST_TEST(std::get<PathError>(r).code == "invalid_field");
    BOOST_TEST(read_file(p) == "original");
    BOOST_TEST(!has_temp_file(dir));
}

// Type: unit.
// Scenario: a target under a non-existent parent is invalid_field.
// Boundaries: missing directory.
// On failure first check: the parent is_directory check.
BOOST_FIXTURE_TEST_CASE(write_missing_parent_rejected, TempDirFixture)
{
    const fs::path p = dir / "nodir" / "out.csv";
    auto r = AtomicFileSink::create_new(p.string(), allow_policy());
    BOOST_REQUIRE(std::holds_alternative<PathError>(r));
    BOOST_TEST(std::get<PathError>(r).code == "invalid_field");
}

// Type: unit (cleanup on abandon).
// Scenario: a sink dropped without commit removes its temp and creates no
// target.
// Boundaries: destructor path.
// On failure first check: the destructor's abort().
BOOST_FIXTURE_TEST_CASE(write_abort_on_destruction, TempDirFixture)
{
    const fs::path p = dir / "out.csv";
    {
        auto r = AtomicFileSink::create_new(p.string(), allow_policy());
        AtomicFileSink sink = std::get<AtomicFileSink>(std::move(r));
        BOOST_TEST(sink.write("partial"));
    }
    BOOST_TEST(!fs::exists(p));
    BOOST_TEST(!has_temp_file(dir));
}

// Type: unit (commit race / failure).
// Scenario: if the target appears before commit, commit returns false and the
// temp is removed; the external file is kept.
// Boundaries: target created between create_new and commit.
// On failure first check: the create-new link/rename guard in commit().
BOOST_FIXTURE_TEST_CASE(write_commit_failure_removes_temp, TempDirFixture)
{
    const fs::path p = dir / "out.csv";
    auto r = AtomicFileSink::create_new(p.string(), allow_policy());
    AtomicFileSink sink = std::get<AtomicFileSink>(std::move(r));
    BOOST_TEST(sink.write("mine"));
    write_file(p, "external");  // target appears before commit
    BOOST_TEST(!sink.commit());
    BOOST_TEST(read_file(p) == "external");
    BOOST_TEST(!has_temp_file(dir));
}

// Type: unit (one-committer race, threat model T-99).
// Scenario: two sinks for the same target each commit; exactly one succeeds and
// its content wins.
// Boundaries: concurrent create-new on one path.
// On failure first check: the create-new semantics of commit().
BOOST_FIXTURE_TEST_CASE(write_two_sinks_one_commits, TempDirFixture)
{
    const fs::path p = dir / "out.csv";
    auto ra = AtomicFileSink::create_new(p.string(), allow_policy());
    auto rb = AtomicFileSink::create_new(p.string(), allow_policy());
    AtomicFileSink a = std::get<AtomicFileSink>(std::move(ra));
    AtomicFileSink b = std::get<AtomicFileSink>(std::move(rb));
    BOOST_TEST(a.write("aaa"));
    BOOST_TEST(b.write("bbb"));
    BOOST_TEST(a.commit());
    BOOST_TEST(!b.commit());
    BOOST_TEST(read_file(p) == "aaa");
    BOOST_TEST(!has_temp_file(dir));
}

// ---------------------------------------------------------------------------
// Capability and root confinement
// ---------------------------------------------------------------------------

// Type: unit (default-deny capability, threat model T-95).
// Scenario: the default PathPolicy (and an explicit allow_paths=false) refuse a
// path for read and write with unsupported and no path echoed in the message.
// Boundaries: capability absent.
// On failure first check: the capability gate runs before any filesystem call.
BOOST_FIXTURE_TEST_CASE(capability_default_deny, TempDirFixture)
{
    const std::string path = (dir / "nodir" / "secret.csv").string();

    auto read_default = LineSource::from_path(path, PathPolicy {});
    BOOST_REQUIRE(std::holds_alternative<PathError>(read_default));
    const PathError& re = std::get<PathError>(read_default);
    BOOST_TEST(re.code == "unsupported");
    BOOST_TEST(re.message.find(path) == std::string::npos);
    BOOST_TEST(re.message.find("secret") == std::string::npos);

    auto write_default = AtomicFileSink::create_new(path, PathPolicy {});
    BOOST_REQUIRE(std::holds_alternative<PathError>(write_default));
    BOOST_TEST(std::get<PathError>(write_default).code == "unsupported");

    PathPolicy explicit_deny;
    explicit_deny.allow_paths = false;
    auto read_explicit = LineSource::from_path(path, explicit_deny);
    BOOST_TEST(std::get<PathError>(read_explicit).code == "unsupported");
}

// Type: unit (root jail).
// Scenario: with a root set, an in-root file is accepted; a ".." escape and a
// string-prefix sibling (rx vs r) are both rejected.
// Boundaries: inside root, parent escape, prefix trap.
// On failure first check: is_within_root's component-wise compare.
BOOST_FIXTURE_TEST_CASE(root_confinement, TempDirFixture)
{
    const fs::path root = dir / "r";
    fs::create_directories(root);
    const fs::path inside = root / "a.csv";
    write_file(inside, "ok\n");

    auto good = LineSource::from_path(inside.string(), rooted_policy(root));
    BOOST_REQUIRE(std::holds_alternative<LineSource>(good));

    const fs::path escape = root / ".." / "x.csv";
    write_file(dir / "x.csv", "nope\n");
    auto esc = LineSource::from_path(escape.string(), rooted_policy(root));
    BOOST_TEST(std::get<PathError>(esc).code == "invalid_field");

    const fs::path sibling = dir / "rx";
    fs::create_directories(sibling);
    write_file(sibling / "a.csv", "nope\n");
    auto sib = LineSource::from_path((sibling / "a.csv").string(),
                                     rooted_policy(root));
    BOOST_TEST(std::get<PathError>(sib).code == "invalid_field");
}

#ifndef _WIN32
// Type: unit (root jail symlink escape, POSIX).
// Scenario: a symlink inside the root whose final component is a symlink is
// rejected (O_NOFOLLOW), whether it targets inside or outside the root.
// Boundaries: in-root symlink to an outside file.
// On failure first check: the O_NOFOLLOW openat in the rooted read branch.
BOOST_FIXTURE_TEST_CASE(root_symlink_escape_rejected, TempDirFixture)
{
    const fs::path root = dir / "r";
    fs::create_directories(root);
    const fs::path outside = dir / "outside.csv";
    write_file(outside, "secret\n");
    const fs::path evil = root / "evil.csv";
    fs::create_symlink(outside, evil);

    auto r = LineSource::from_path(evil.string(), rooted_policy(root));
    BOOST_REQUIRE(std::holds_alternative<PathError>(r));
    BOOST_TEST(std::get<PathError>(r).code == "invalid_field");
}

// Type: unit (rooted TOCTOU post-open re-assertion, threat model T-96).
// Scenario: a rooted target whose final component is a symlink (symlink-swap
// stand-in) is rejected via O_NOFOLLOW with no read; a target that is a FIFO
// (special-file swap stand-in) is rejected via the post-open S_ISREG check.
// Boundaries: in-root symlink and FIFO on the opened name.
// On failure first check: O_NOFOLLOW ELOOP mapping and the fstat S_ISREG guard.
BOOST_FIXTURE_TEST_CASE(rooted_toctou_post_open, TempDirFixture)
{
    const fs::path root = dir / "r";
    fs::create_directories(root);

    const fs::path real = root / "real.csv";
    write_file(real, "data\n");
    const fs::path link = root / "link.csv";
    fs::create_symlink(real, link);
    auto swapped_symlink =
        LineSource::from_path(link.string(), rooted_policy(root));
    BOOST_REQUIRE(std::holds_alternative<PathError>(swapped_symlink));
    BOOST_TEST(std::get<PathError>(swapped_symlink).code == "invalid_field");

    const fs::path fifo = root / "pipe.csv";
    BOOST_REQUIRE_EQUAL(::mkfifo(fifo.c_str(), 0600), 0);
    auto swapped_fifo =
        LineSource::from_path(fifo.string(), rooted_policy(root));
    BOOST_REQUIRE(std::holds_alternative<PathError>(swapped_fifo));
    BOOST_TEST(std::get<PathError>(swapped_fifo).code == "invalid_field");
}

// Type: unit (rooted intermediate-parent swap, threat model T-96).
// Scenario: a validated in-root target (root/sub/file.csv) whose intermediate
// parent (root/sub) is replaced by a symlink to an outside directory holding a
// same-named file is rejected for BOTH read and export, value-free, with no
// access to the outside file. The swap is done before the call because the
// symlink-free openat walk refuses any symlinked component regardless of
// timing, which is exactly what makes the check-to-open race unwinnable.
// Boundaries: symlinked intermediate directory component.
// On failure first check: the O_DIRECTORY|O_NOFOLLOW intermediate walk in
// open_rooted_parent (ELOOP on the symlinked "sub").
BOOST_FIXTURE_TEST_CASE(rooted_intermediate_parent_swap, TempDirFixture)
{
    const fs::path root = dir / "r";
    const fs::path sub = root / "sub";
    fs::create_directories(sub);
    write_file(sub / "file.csv", "in-root\n");

    // An outside directory the swapped symlink will point at, with a file of
    // the same name the attacker hopes to read/overwrite.
    const fs::path outside = dir / "outside";
    fs::create_directories(outside);
    write_file(outside / "file.csv", "secret\n");

    // Swap the intermediate parent for a symlink to the outside directory.
    fs::remove_all(sub);
    fs::create_symlink(outside, sub);

    const fs::path target = sub / "file.csv";  // root/sub/file.csv

    auto rd = LineSource::from_path(target.string(), rooted_policy(root));
    BOOST_REQUIRE(std::holds_alternative<PathError>(rd));
    const PathError& re = std::get<PathError>(rd);
    BOOST_TEST(re.code == "invalid_field");
    BOOST_TEST(re.message.find("secret") == std::string::npos);
    BOOST_TEST(re.message.find(target.string()) == std::string::npos);

    auto wr = AtomicFileSink::create_new(target.string(), rooted_policy(root));
    BOOST_REQUIRE(std::holds_alternative<PathError>(wr));
    BOOST_TEST(std::get<PathError>(wr).code == "invalid_field");

    // The outside file was neither read nor overwritten.
    BOOST_TEST(read_file(outside / "file.csv") == "secret\n");
}

// Type: unit (rooted write confinement).
// Scenario: with a root set, a create-new write inside the root succeeds and a
// ".." escape is rejected.
// Boundaries: inside root vs. parent escape.
// On failure first check: the write-side root prefix check in create_new.
BOOST_FIXTURE_TEST_CASE(rooted_write_confinement, TempDirFixture)
{
    const fs::path root = dir / "r";
    fs::create_directories(root);

    auto good = AtomicFileSink::create_new((root / "o.csv").string(),
                                           rooted_policy(root));
    BOOST_REQUIRE(std::holds_alternative<AtomicFileSink>(good));
    AtomicFileSink sink = std::get<AtomicFileSink>(std::move(good));
    BOOST_TEST(sink.write("x"));
    BOOST_TEST(sink.commit());

    auto esc = AtomicFileSink::create_new((root / ".." / "e.csv").string(),
                                          rooted_policy(root));
    BOOST_REQUIRE(std::holds_alternative<PathError>(esc));
    BOOST_TEST(std::get<PathError>(esc).code == "invalid_field");
}
#endif
